#include "proto_bacnet_internal.hpp"

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* 全局上下文指针（用于C回调函数访问）                                         */
/* -------------------------------------------------------------------------- */

BacnetContext *g_ctx = nullptr;

/* -------------------------------------------------------------------------- */
/* 辅助函数：获取上下文                                                        */
/* -------------------------------------------------------------------------- */

BacnetContext* get_context(proto_ctx_t *ctx)
{
    if (!ctx || ctx->type != PROTO_TYPE_BACNET) {
        return nullptr;
    }
    return static_cast<BacnetContext *>(ctx->userdata);
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
    context->callbacks[key] = {callback, userdata};
    
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
        std::thread([callback = it->second.callback, status, userdata = it->second.userdata]() {
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

    log_debug("[BACnet] Loading configuration from {}", kDefaultConfigPath);
    // 加载配置
    int rc = bacnet_load_config_from_yaml(kDefaultConfigPath, &context->config);
    if (rc != 0) {
        log_error("[BACnet] Failed to load config from {} (rc={})", kDefaultConfigPath, rc);
        return PROTO_ERROR_INIT;
    }
    log_debug("[BACnet] Configuration loaded successfully");

    // 设置目标设备实例范围
    context->target_device_start = context->config.bacnet.discovery.target_device_start;
    context->target_device_end = context->config.bacnet.discovery.target_device_end;

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
        if (attempts < kMaxReconnectAttempts) {
            context->reconnect_attempts.fetch_add(1, std::memory_order_acq_rel);
            log_info("[BACnet] Will retry connection (attempt {}/{})", 
                     attempts + 1, kMaxReconnectAttempts);
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

extern "C" {

/* -------------------------------------------------------------------------- */
/* 初始化驱动                                                                 */
/* -------------------------------------------------------------------------- */

int proto_driver_init(proto_ctx_t *ctx)
{
    if (!ctx || ctx->type != PROTO_TYPE_BACNET) {
        return PROTO_ERROR_UNSUPPORTED;
    }

    // 检查是否已经初始化
    if (ctx->userdata != nullptr) {
        log_warn("[BACnet] Driver already initialized");
        return PROTO_SUCCESS;
    }

    // 创建BACnet上下文（使用new，后续用delete释放）
    auto *context = new BacnetContext();
    context->ctx = ctx;

    // 初始化上下文
    proto_status_t status = initialize_context(context);
    if (status != PROTO_SUCCESS) {
        delete context;
        return status;
    }

    // 设置上下文指针
    ctx->userdata = context;
    ctx->config = &context->config;
    g_ctx = context;

    log_info("[BACnet] Driver initialized successfully");
    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 释放驱动资源                                                               */
/* -------------------------------------------------------------------------- */

void proto_driver_release(proto_ctx_t *ctx)
{
    BacnetContext *context = get_context(ctx);
    if (!context) {
        return;
    }

    log_info("[BACnet] Releasing driver...");

    // 清理上下文
    cleanup_context(context);

    // 清除指针
    ctx->userdata = nullptr;
    ctx->config = nullptr;
    g_ctx = nullptr;

    // 释放内存
    delete context;

    log_info("[BACnet] Driver released successfully");
}

/* -------------------------------------------------------------------------- */
/* 连接设备                                                                   */
/* -------------------------------------------------------------------------- */

int proto_connect(proto_ctx_t *ctx)
{
    BacnetContext *context = get_context(ctx);
    if (!context) {
        return PROTO_ERROR_UNSUPPORTED;
    }

    return connect_device(context);
}

/* -------------------------------------------------------------------------- */
/* 断开连接                                                                   */
/* -------------------------------------------------------------------------- */

void proto_disconnect(proto_ctx_t *ctx)
{
    BacnetContext *context = get_context(ctx);
    if (!context) {
        return;
    }

    disconnect_device(context);
}

/* -------------------------------------------------------------------------- */
/* 读取属性                                                                   */
/* -------------------------------------------------------------------------- */

int bacnet_proto_read(proto_ctx_t *ctx, bacnet_read_t *req)
{
    BacnetContext *context = get_context(ctx);
    if (!context) {
        return PROTO_ERROR_UNSUPPORTED;
    }

    return execute_read_property(context, req);
}

/* -------------------------------------------------------------------------- */
/* 写入属性                                                                   */
/* -------------------------------------------------------------------------- */

int bacnet_proto_write(proto_ctx_t *ctx, const bacnet_write_t *req)
{
    BacnetContext *context = get_context(ctx);
    if (!context) {
        return PROTO_ERROR_UNSUPPORTED;
    }

    return execute_write_property(context, req);
}

/* -------------------------------------------------------------------------- */
/* PLC接口：读取                                                              */
/* -------------------------------------------------------------------------- */

static std::mutex g_plc_mutex;
static bool g_plc_initialized = false;
static proto_ctx_t g_plc_ctx{PROTO_TYPE_BACNET, nullptr, nullptr, nullptr};

static int ensure_init_and_connect_locked()
{
    log_debug("[PLC] ensure_init_and_connect_locked called, g_plc_initialized={}", g_plc_initialized);
    
    if (!g_plc_initialized) {
        log_info("[PLC] Initializing BACnet driver...");
        int rc = proto_driver_init(&g_plc_ctx);
        if (rc != PROTO_SUCCESS) {
            log_error("[PLC] Driver initialization failed: {}", rc);
            return rc;
        }
        g_plc_initialized = true;
        log_info("[PLC] Driver initialized successfully");
        
        // 不启动热配置监控线程，改用信号触发
        log_info("[BACnet] Driver initialized, use bacnet_reload_config() to reload");
    }

    BacnetContext *context = get_context(&g_plc_ctx);
    if (!context) {
        log_error("[PLC] Failed to get context");
        return PROTO_ERROR_INIT;
    }

    bacnet_connection_state_t conn_state = get_connection_state(context);
    log_debug("[PLC] Current connection state: {}", static_cast<int>(conn_state));
    
    if (conn_state != BACNET_CONN_CONNECTED) {
        log_info("[PLC] Connecting to device...");
        int rc = proto_connect(&g_plc_ctx);
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
    
    log_debug("[PLC] plc_proto_read called for device {}", read_req->device_instance);

    int rc;
    {
        std::lock_guard<std::mutex> lock(g_plc_mutex);
        rc = ensure_init_and_connect_locked();
    }
    if (rc != PROTO_SUCCESS) {
        log_error("[PLC] Initialization/connection failed: {}", rc);
        return rc;
    }

    BacnetContext *context = get_context(&g_plc_ctx);
    if (!context) {
        log_error("[PLC] Failed to get context in plc_proto_read");
        return PROTO_ERROR_INIT;
    }

    // 优先从读队列取数据
    {
        std::lock_guard<std::mutex> lock(context->read_queue_mutex);
        if (context->read_count > 0) {
            log_debug("[PLC] Found data in read queue, count={}", context->read_count);
            auto &item = context->read_queue[context->read_head];
            read_req->device_instance = item.device_instance;
            read_req->object_type = item.object_type;
            read_req->object_instance = item.object_instance;
            read_req->property_id = item.property_id;
            if (read_req->value) {
                *read_req->value = item.value;
                // 注意：数据已复制到read_req->value，调用者负责释放内存
            }
            // 释放队列中item的动态内存
            bacnet_data_value_free(&item.value);
            context->read_head = (context->read_head + 1) % BacnetContext::kReadQueueSize;
            context->read_count--;
            log_debug("[PLC] Returning data from queue");
            return PROTO_SUCCESS;
        }
    }

    // 队列无数据，发起底层读请求，立即返回无数据
    log_debug("[PLC] No data in queue, initiating read request");
    rc = bacnet_proto_read(&g_plc_ctx, read_req);
    log_debug("[PLC] bacnet_proto_read returned: {}", rc);
    return -7; // PROTO_NO_DATA
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

    int rc;
    {
        std::lock_guard<std::mutex> lock(g_plc_mutex);
        rc = ensure_init_and_connect_locked();
    }
    if (rc != PROTO_SUCCESS) {
        return rc;
    }

    BacnetContext *context = get_context(&g_plc_ctx);
    if (!context) {
        return PROTO_ERROR_INIT;
    }

    // 只入队写请求，不直接下发
    {
        std::lock_guard<std::mutex> lock(context->write_queue_mutex);
        if (context->write_count < BacnetContext::kWriteQueueSize) {
            auto &item = context->write_queue[context->write_tail];
            item.device_instance = write_req->device_instance;
            item.object_type = write_req->object_type;
            item.object_instance = write_req->object_instance;
            item.property_id = write_req->property_id;
            item.value = write_req->value;
            item.priority = write_req->priority;
            item.array_index = write_req->array_index;
            item.length = 0; // TODO: 从 value 中推导长度
            item.status = PROTO_SUCCESS;
            item.timestamp = std::chrono::steady_clock::now();
            context->write_tail = (context->write_tail + 1) % BacnetContext::kWriteQueueSize;
            context->write_count++;
            
            // 通知工作线程处理写队列
            context->write_queue_cv.notify_one();
            
            return PROTO_SUCCESS;
        } else {
            log_warn("[BACnet] Write queue full, discarding write request");
            return PROTO_ERROR_WRITE;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 热配置重载接口（信号触发）                                                 */
/* -------------------------------------------------------------------------- */

int bacnet_reload_config(void)
{
    std::lock_guard<std::mutex> lock(g_plc_mutex);
    
    log_info("[BACnet] Config reload triggered by signal");
    
    if (g_plc_initialized) {
        proto_driver_release(&g_plc_ctx);
        g_plc_initialized = false;
    }
    
    // 下次调用 plc_proto_read/write 时会自动重新初始化
    return PROTO_SUCCESS;
}

} // extern "C"
