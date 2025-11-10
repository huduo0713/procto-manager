#include "proto_bacnet_internal.hpp"

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* BacnetContext 单例方法实现                                                  */
/* -------------------------------------------------------------------------- */

proto_status_t BacnetContext::initialize(proto_ctx_t* ctx_param) {
    std::lock_guard<std::mutex> lock(plc_mutex);
    
    if (initialized_.load(std::memory_order_acquire)) {
        log_warn("[BACnet][Context] Already initialized, resetting...");
        reset();
    }
    
    log_info("[BACnet][Context] Initializing context...");
    
    this->ctx = ctx_param;
    
    // 调用原有的初始化函数
    proto_status_t status = initialize_context(this);
    if (status != PROTO_SUCCESS) {
        log_error("[BACnet][Context] Initialization failed");
        return status;
    }
    
    initialized_.store(true, std::memory_order_release);
    log_info("[BACnet][Context] Initialization completed");
    
    return PROTO_SUCCESS;
}

void BacnetContext::reset() {
    std::lock_guard<std::mutex> lock(plc_mutex);
    
    log_info("[BACnet][Context] Resetting context...");
    
    // 清理资源
    cleanup_context(this);
    
    // 清理所有状态
    {
        std::lock_guard<std::mutex> lock_obj(object_states_mutex);
        object_states.clear();
    }
    {
        std::lock_guard<std::mutex> lock_inv(invoke_id_to_key_mutex);
        invoke_id_to_key.clear();
    }
    {
        std::lock_guard<std::mutex> lock_write(write_pending_mutex);
        write_pending_map.clear();
    }
    
    initialized_.store(false, std::memory_order_release);
    
    log_info("[BACnet][Context] Reset completed");
}

/* -------------------------------------------------------------------------- */
/* 状态管理函数                                                               */
/* -------------------------------------------------------------------------- */

void set_connection_state(BacnetContext *context, bacnet_connection_state_t state)
{
    if (context) {
        context->connection_state.store(state, std::memory_order_release);
    }
}

bacnet_connection_state_t get_connection_state(BacnetContext *context)
{
    return context ? context->connection_state.load(std::memory_order_acquire) : BACNET_CONN_IDLE;
}

void set_operation_state(BacnetContext *context, bacnet_operation_state_t state)
{
    if (context) {
        context->operation_state.store(state, std::memory_order_release);
    }
}

bacnet_operation_state_t get_operation_state(BacnetContext *context)
{
    return context ? context->operation_state.load(std::memory_order_acquire) : BACNET_OP_IDLE;
}

/* -------------------------------------------------------------------------- */
/* 活动操作管理                                                               */
/* -------------------------------------------------------------------------- */

void reset_active_operation_locked(BacnetContext *context)
{
    if (context) {
        context->active_operation.reset();
    }
}

bool get_active_operation_snapshot(BacnetContext *context, ActiveOperation &snapshot)
{
    if (!context) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(context->operation_mutex);
    if (!context->active_operation.is_active()) {
        return false;
    }
    
    snapshot = context->active_operation;
    return true;
}


/* -------------------------------------------------------------------------- */
/* 操作完成处理                                                               */
/* -------------------------------------------------------------------------- */

void finalize_operation(BacnetContext *context, proto_status_t status)
{
    if (!context) {
        return;
    }

    void *request_ptr = nullptr;
    uint8_t invoke_id = 0;
    OperationKind op_type = OperationKind::None;
    uint32_t device_instance = 0;

    {
        std::lock_guard<std::mutex> lock(context->operation_mutex);
        if (!context->active_operation.is_active()) {
            return;
        }
        
        op_type = context->active_operation.type;
        request_ptr = context->active_operation.request;
        invoke_id = context->active_operation.invoke_id;
        device_instance = context->active_operation.device_instance;
        
        reset_active_operation_locked(context);
    }

    // 释放TSM资源
    if (invoke_id != 0) {
        tsm_free_invoke_id(invoke_id);
    }

    // 更新操作状态
    set_operation_state(context, 
                       (status == PROTO_SUCCESS) ? BACNET_OP_SUCCESS : BACNET_OP_FAILED);

    log_info("[BACnet] Operation finalized: type={}, status={}, invoke_id={}",
             static_cast<int>(op_type), static_cast<int>(status), invoke_id);
    // 触发用户回调（如果已设置）
    trigger_callback(context, "operation", status);
}

/* -------------------------------------------------------------------------- */
/* 回调管理函数                                                               */
/* -------------------------------------------------------------------------- */

void set_callback(BacnetContext *context, const char* type, AsyncCallback callback, void* userdata)
{
    if (!context || !type) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(context->callback_mutex);
    
    std::string key(type);
    context->callbacks[key] = BacnetContext::CallbackInfo(callback, userdata);
    
    log_debug("[BACnet] Callback set for type: {}", type);
}

void trigger_callback(BacnetContext *context, const char* type, proto_status_t status)
{
    if (!context || !type) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(context->callback_mutex);
    
    std::string key(type);
    auto it = context->callbacks.find(key);
    if (it != context->callbacks.end() && it->second.callback) {
        log_debug("[BACnet] Triggering callback for type: {}, status: {}", type, static_cast<int>(status));
        
        // 在新线程中执行回调，避免阻塞当前线程
        auto callback = it->second.callback;
        auto userdata = it->second.userdata;
        std::thread([callback, status, userdata]() {
            try {
                callback(status, userdata);
            } catch (const std::exception& e) {
                log_error("[BACnet] Callback execution failed: {}", e.what());
            }
        }).detach();
    }
}

/* -------------------------------------------------------------------------- */
/* 初始化BACnet上下文                                                          */
/* -------------------------------------------------------------------------- */

proto_status_t initialize_context(BacnetContext *context)
{
    if (!context) {
        return PROTO_ERROR_PARAM;
    }

    log_debug("[BACnet] Loading configuration from {}", bacnet::defaults::kConfigPath);
    // 加载配置
    int rc = bacnet_load_config_from_yaml(bacnet::defaults::kConfigPath, &context->config);
    if (rc != 0) {
        log_error("[BACnet] Failed to load config from {} (rc={})", bacnet::defaults::kConfigPath, rc);
        return PROTO_ERROR_INIT;
    }
    log_debug("[BACnet] Configuration loaded successfully");

    // 设置目标设备实例范围
    context->target_device_start = context->config.bacnet.discovery.target_device_start;
    context->target_device_end = context->config.bacnet.discovery.target_device_end;

    // 设置缓存策略
    context->cache_strategy = (context->config.bacnet.services.cache_strategy == 0) 
                              ? CacheStrategy::Aggressive 
                              : CacheStrategy::Conservative;
    context->cache_expiry_ms = context->config.bacnet.services.cache_expiry_ms;
    
    log_debug("[BACnet] Cache strategy: {} (expiry: {}ms)", 
              cache_strategy_to_string(context->cache_strategy),
              context->cache_expiry_ms);

    log_debug("[BACnet] Initializing BACnet protocol stack...");
    // 初始化BACnet协议栈
    Device_Init(nullptr);
    log_debug("[BACnet] Device_Init completed");

    address_init();
    log_debug("[BACnet] address_init completed");

    dlenv_init();
    log_debug("[BACnet] dlenv_init completed");

    // 注册回调处理函数
    register_bacnet_handlers(context);
    log_debug("[BACnet] BACnet handlers registered");

    // 重置状态
    reset_active_operation_locked(context);
    set_connection_state(context, BACNET_CONN_IDLE);
    set_operation_state(context, BACNET_OP_IDLE);
    
    context->target_found.store(false, std::memory_order_release);
    context->reconnect_attempts.store(0, std::memory_order_release);
    
    // 重置工作线程标志（重要！用于热配置重载后重新启动）
    context->worker_stop.store(false, std::memory_order_release);
    context->worker_running.store(false, std::memory_order_release);

    log_info("[BACnet] Context initialized successfully (target device range: {}-{})", 
             context->target_device_start, context->target_device_end);
    
    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 清理BACnet上下文                                                            */
/* -------------------------------------------------------------------------- */

void cleanup_context(BacnetContext *context)
{
    if (!context) {
        return;
    }

    log_info("[BACnet] Cleaning up context...");

    // 停止工作线程
    stop_worker_thread(context);

    // 清除活动操作
    {
        std::lock_guard<std::mutex> lock(context->operation_mutex);
        reset_active_operation_locked(context);
    }

    // ...existing code...

    // 移除设备缓存（清理范围内的所有设备）
    for (uint32_t dev_id = context->target_device_start; dev_id <= context->target_device_end; ++dev_id) {
        address_remove_device(dev_id);
    }

    // 清理数据链路层
    bip_cleanup();
    datalink_cleanup();

    log_info("[BACnet] Context cleanup completed");
}

/* -------------------------------------------------------------------------- */
/* 连接到目标设备                                                             */
/* -------------------------------------------------------------------------- */

proto_status_t connect_device(BacnetContext *context)
{
    if (!context) {
        return PROTO_ERROR_PARAM;
    }

    // 检查当前连接状态
    bacnet_connection_state_t current_state = get_connection_state(context);
    if (current_state == BACNET_CONN_CONNECTED) {
        log_info("[BACnet] Already connected to device range {}-{}", 
                 context->target_device_start, context->target_device_end);
        return PROTO_SUCCESS;
    }

    if (current_state == BACNET_CONN_CONNECTING) {
        log_info("[BACnet] Connection already in progress");
        return PROTO_SUCCESS;
    }

    log_info("[BACnet] Connecting to device range {}-{}...", 
             context->target_device_start, context->target_device_end);

    // 执行设备发现
    set_connection_state(context, BACNET_CONN_CONNECTING);
    proto_status_t status = discover_target_device(context);
    if (status != PROTO_SUCCESS) {
        log_error("[BACnet] Failed to discover devices in range {}-{} (status: {})", 
                  context->target_device_start, context->target_device_end, static_cast<int>(status));
        
        set_connection_state(context, BACNET_CONN_DISCONNECTED);
        
        // 检查是否需要重连
        int attempts = context->reconnect_attempts.load(std::memory_order_acquire);
        uint8_t max_attempts = context->config.bacnet.connection.max_reconnect_attempts;
        if (attempts < max_attempts) {
            context->reconnect_attempts.fetch_add(1, std::memory_order_acq_rel);
            log_info("[BACnet] Will retry connection (attempt {}/{})", 
                     attempts + 1, max_attempts);
        } else {
            log_error("[BACnet] Maximum reconnection attempts reached");
            trigger_callback(context, "connect", PROTO_ERROR_CONNECT);
        }
        
        return status;
    }

    set_connection_state(context, BACNET_CONN_CONNECTED);

    // 启动工作线程
    start_worker_thread(context);

    // 重置重连计数器
    context->reconnect_attempts.store(0, std::memory_order_release);

    log_info("[BACnet] Connected to device range {}-{} successfully", 
             context->target_device_start, context->target_device_end);
    
    // 触发连接成功回调
    trigger_callback(context, "connect", PROTO_SUCCESS);
    
    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 断开与目标设备的连接                                                        */
/* -------------------------------------------------------------------------- */

void disconnect_device(BacnetContext *context)
{
    if (!context) {
        return;
    }

    log_info("[BACnet] Disconnecting from device range {}-{}...", 
             context->target_device_start, context->target_device_end);

    // 停止工作线程
    stop_worker_thread(context);

    // 清除活动操作
    {
        std::lock_guard<std::mutex> lock(context->operation_mutex);
        reset_active_operation_locked(context);
    }

    // 移除设备缓存（清理范围内的所有设备）
    for (uint32_t dev_id = context->target_device_start; dev_id <= context->target_device_end; ++dev_id) {
        address_remove_device(dev_id);
    }

    // 重置状态
    context->target_found.store(false, std::memory_order_release);
    set_connection_state(context, BACNET_CONN_DISCONNECTED);

    log_info("[BACnet] Disconnected from device range {}-{}", 
             context->target_device_start, context->target_device_end);
    
    // 触发断开连接回调
    trigger_callback(context, "disconnect", PROTO_SUCCESS);
}

} // namespace bacnet

/* ========================================================================== */
/* C接口实现                                                                  */
/* ========================================================================== */

using namespace bacnet;

/* PLC 全局状态管理 */
static std::mutex g_plc_mutex;

extern "C" {

/* 读写属性的实现在 proto_bacnet_io.cpp 中 */

/* -------------------------------------------------------------------------- */
/* PLC接口：辅助函数（初始化和连接管理）                                      */
/* -------------------------------------------------------------------------- */

static int ensure_init_and_connect_locked()
{
    auto& driver = bacnet::BacnetDriver::instance();
    
    // 初始化驱动（如果未初始化）
    if (!driver.is_initialized()) {
        log_info("[PLC] Initializing BACnet driver...");
        proto_ctx_t ctx{PROTO_TYPE_BACNET, nullptr, nullptr, nullptr};
        int rc = driver.initialize(&ctx);
        if (rc != PROTO_SUCCESS) {
            log_error("[PLC] Driver initialization failed: {}", rc);
            return rc;
        }
        log_info("[PLC] Driver initialized successfully");
    }
    
    // 获取上下文
    BacnetContext *context = driver.get_context();
    if (!context) {
        log_error("[PLC] Failed to get context");
        return PROTO_ERROR_INIT;
    }
    
    // 检查连接状态
    bacnet_connection_state_t conn_state = get_connection_state(context);
    log_debug("[PLC] Current connection state: {} ({})", 
              static_cast<int>(conn_state), 
              connection_state_to_string(conn_state));
    
    if (conn_state != BACNET_CONN_CONNECTED) {
        log_info("[PLC] Connecting to device...");
        int rc = driver.connect();
        if (rc != PROTO_SUCCESS) {
            log_error("[PLC] Connection failed: {}", rc);
            return rc;
        }
        log_info("[PLC] Connected successfully");
    }
    
    return PROTO_SUCCESS;
}

int plc_proto_read(void *req)
{
    if (!req) {
        return PROTO_ERROR_PARAM;
    }
    auto *read_req = static_cast<bacnet_read_t *>(req);
    
    const char *obj_type_name = bactext_object_type_name(read_req->object_type);
    const char *prop_name = bactext_property_name(read_req->property_id);
    
    log_debug("[PLC] plc_proto_read: device={}, {}-{}, {}",
              read_req->device_instance,
              obj_type_name,
              read_req->object_instance,
              prop_name);

    // ✨ 检查是否有待重载的配置（在获取锁之前）
    auto& driver = bacnet::BacnetDriver::instance();
    if (driver.is_initialized()) {
        auto* ctx = driver.get_context();
        if (ctx && ctx->pending_reload) {
            std::lock_guard<std::mutex> hot_lock(ctx->hot_config_mutex);
            if (ctx->pending_reload) {  // 双重检查
                log_warn("[BACnet] Pending config reload detected, releasing driver...");
                ctx->pending_reload = false;
                
                std::lock_guard<std::mutex> global_lock(g_plc_mutex);
                driver.release();
                // 下面会自动重新初始化
            }
        }
    }

    int rc;
    {
        std::lock_guard<std::mutex> lock(g_plc_mutex);
        rc = ensure_init_and_connect_locked();
    }
    if (rc != PROTO_SUCCESS) {
        log_error("[PLC] Initialization/connection failed: {}", rc);
        return rc;
    }

    // 获取上下文（使用前面已声明的 driver）
    BacnetContext *context = driver.get_context();
    if (!context) {
        log_error("[PLC] Failed to get context in plc_proto_read");
        return PROTO_ERROR_INIT;
    }

    // 构造对象键
    ObjectKey key{
        read_req->device_instance,
        read_req->object_type,
        read_req->object_instance,
        read_req->property_id
    };

    std::lock_guard<std::mutex> lock(context->object_states_mutex);
    auto it = context->object_states.find(key);
    
    // 检查缓存策略
    if (context->cache_strategy == CacheStrategy::Conservative && it != context->object_states.end()) {
        auto &state = it->second;
        auto now = std::chrono::steady_clock::now();
        auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.timestamp).count();
        
        // 保守策略：如果有有效缓存且未过期，直接返回
        if (state.has_valid_cache && age_ms < context->cache_expiry_ms) {
            if (read_req->value) {
                *read_req->value = state.cached_value;
            }
            log_debug("[PLC] Cache hit (conservative): {}-{}, {} (age: {}ms)",
                      obj_type_name, read_req->object_instance, prop_name, age_ms);
            return PROTO_SUCCESS;
        }
    }

    // 激进策略 OR 无缓存 OR 缓存已过期：发送请求
    // 但先检查是否有未完成的请求
    if (it != context->object_states.end() && it->second.active_invoke_id != 0) {
        // 已有活跃请求，返回 PENDING
        char invoke_buf[16];
        log_debug("[PLC] Request pending: {}-{}, {} (active invoke_id: {})",
                  obj_type_name, read_req->object_instance, prop_name,
                  invoke_id_to_string(it->second.active_invoke_id, invoke_buf, sizeof(invoke_buf)));
        
        // 如果有缓存，返回缓存数据（即使可能过期）
        if (it->second.has_valid_cache && read_req->value) {
            *read_req->value = it->second.cached_value;
        }
        return PROTO_NO_DATA;  // 请求进行中，暂无新数据
    }

    // 发起新请求
    log_debug("[PLC] Initiating read request: {}-{}, {}",
              obj_type_name, read_req->object_instance, prop_name);
    
    uint8_t invoke_id = 0;
    rc = execute_read_property(context, read_req, &invoke_id);
    
    if (rc == PROTO_SUCCESS) {
        // 创建或更新对象状态
        BacnetContext::ObjectState &state = context->object_states[key];
        state.active_invoke_id = invoke_id;
        state.original_request = read_req;
        state.status = PROTO_NO_DATA;  // 请求已发送，等待响应
        
        // 如果有旧缓存，可以立即返回（但标记为NO_DATA）
        if (state.has_valid_cache && read_req->value) {
            *read_req->value = state.cached_value;
        }
        
        char invoke_buf[16];
        log_debug("[PLC] Request sent: {}-{}, {} (invoke_id: {})",
                  obj_type_name, read_req->object_instance, prop_name,
                  invoke_id_to_string(invoke_id, invoke_buf, sizeof(invoke_buf)));
    }
    
    return rc;
}

/* -------------------------------------------------------------------------- */
/* PLC接口：写入                                                              */
/* -------------------------------------------------------------------------- */

int plc_proto_write(void *req)
{
    if (!req) {
        return PROTO_ERROR_PARAM;
    }
    auto *write_req = static_cast<bacnet_write_t *>(req);

    // ✨ 检查是否有待重载的配置
    auto& driver = bacnet::BacnetDriver::instance();
    if (driver.is_initialized()) {
        auto* ctx = driver.get_context();
        if (ctx && ctx->pending_reload) {
            std::lock_guard<std::mutex> hot_lock(ctx->hot_config_mutex);
            if (ctx->pending_reload) {  // 双重检查
                log_warn("[BACnet] Pending config reload detected, releasing driver...");
                ctx->pending_reload = false;
                
                std::lock_guard<std::mutex> global_lock(g_plc_mutex);
                driver.release();
                // 下面会自动重新初始化
            }
        }
    }

    int rc;
    {
        std::lock_guard<std::mutex> lock(g_plc_mutex);
        rc = ensure_init_and_connect_locked();
    }
    if (rc != PROTO_SUCCESS) {
        return rc;
    }

    // 获取上下文（使用前面已声明的 driver）
    BacnetContext *context = driver.get_context();
    if (!context) {
        return PROTO_ERROR_INIT;
    }

    // 直接执行写操作，返回invoke_id
    // 写操作不需要入队，因为它是同步的，回调函数会直接处理ACK
    uint8_t invoke_id = 0;
    rc = execute_write_property(context, write_req, &invoke_id);
    if (rc == PROTO_SUCCESS) {
        write_req->invoke_id = invoke_id;  // 返回invoke_id
        return PROTO_SUCCESS;
    } else {
        return rc;
    }
}

/* -------------------------------------------------------------------------- */
/* 热配置重载接口（信号触发）                                                 */
/* -------------------------------------------------------------------------- */

int bacnet_reload_config(void)
{
    log_info("[BACnet] Config reload triggered");
    
    // 使用驱动单例重载配置
    auto& driver = bacnet::BacnetDriver::instance();
    
    if (!driver.is_initialized()) {
        log_warn("[BACnet] Driver not initialized, nothing to reload");
        return PROTO_SUCCESS;
    }
    
    // 设置待重载标志（线程安全）
    auto* ctx = driver.get_context();
    if (ctx) {
        std::lock_guard<std::mutex> lock(ctx->hot_config_mutex);
        ctx->pending_reload = true;
        log_info("[BACnet] Config reload flag set, will reload on next read/write request");
    }
    
    return PROTO_SUCCESS;
}

} // extern "C"

/* -------------------------------------------------------------------------- */
/* 内部清理函数（C++ 链接，在 atexit 中调用）                                */
/* -------------------------------------------------------------------------- */

int bacnet_cleanup(void)
{
    log_info("[BACnet] Explicit cleanup requested");
    
    // 使用驱动单例的 release 方法清理资源
    auto& driver = bacnet::BacnetDriver::instance();
    
    if (driver.is_initialized()) {
        driver.release();
        log_info("[BACnet] Cleanup completed successfully");
    } else {
        log_info("[BACnet] Driver not initialized, nothing to cleanup");
    }
    
    return PROTO_SUCCESS;
}

