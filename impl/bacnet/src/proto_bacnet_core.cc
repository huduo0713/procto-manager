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
/* 初始化所有数据链路层（多数据链路层模式）                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化所有配置的数据链路层
 * 
 * 功能说明：
 * - 当编译启用 BACDL_MULTIPLE 时，支持同时初始化多个数据链路层
 * - BACnet/IP（以太网 UDP）和 MS/TP（串口 RS-485）可以并行运行
 * - 协议栈会自动维护设备地址路由表，根据设备发现结果选择正确的数据链路层
 * - 用户可选在 bacnet_read_t/bacnet_write_t 中指定 datalink_hint 强制路由
 * 
 * 初始化顺序：
 * 1. BACnet/IP: 以太网 UDP 传输（适用于局域网设备）
 * 2. MS/TP: RS-485 串口主从令牌传递（适用于现场总线设备）
 * 
 * 配置来源：
 * - BACnet/IP 参数: interface_name, port, broadcast_address
 * - MS/TP 参数: mstp_port, mstp_baud, mstp_mac, mstp_max_master, mstp_max_frames
 * 
 * @param context BACnet 上下文指针
 * @return PROTO_SUCCESS 成功，其他值表示失败
 */
proto_status_t initialize_all_datalinks(BacnetContext *context)
{
    if (!context) {
        return PROTO_ERROR_PARAM;
    }

    log_info("[BACnet][DataLink] Initializing multiple datalink layers...");

#if defined(BACDL_MULTIPLE)
    /* ========================================================================== */
    /* 1. 初始化 BACnet/IP 数据链路层（以太网 UDP）                                */
    /* ========================================================================== */
    
    log_info("[BACnet][DataLink] [1/2] Initializing BACnet/IP (Ethernet)...");
    
    // 设置 UDP 端口
    bip_set_port(context->config.port);
    log_debug("[BACnet][DataLink][BIP] Port set to {}", context->config.port);
    
    // 初始化 BACnet/IP 数据链路层
    // 参数：
    //   - interface_name: 网络接口名称（如 "eth0"），空字符串表示自动选择
    const char *ifname = (context->config.interface_name[0] != '\0') 
                          ? context->config.interface_name 
                          : nullptr;
    
    if (!bip_init(const_cast<char*>(ifname))) {
        log_error("[BACnet][DataLink][BIP] Failed to initialize BACnet/IP on interface '{}'", 
                  ifname ? ifname : "<auto>");
        return PROTO_ERROR_INIT;
    }
    
    log_info("[BACnet][DataLink][BIP] Initialized successfully (interface: {}, port: {})",
             ifname ? ifname : "<auto>", context->config.port);

    /* ========================================================================== */
    /* 2. 初始化 MS/TP 数据链路层（RS-485 串口）                                   */
    /* ========================================================================== */
    
    // ⭐ 检查 BACnet 库是否编译了 MS/TP 支持
    // 注意：即使定义了 BACDL_MULTIPLE，也需要 BACnet 库编译时启用 MS/TP
#if defined(BACDL_MSTP)
    // 检查是否配置了 MS/TP 串口路径
    if (context->config.mstp_port[0] != '\0') {
        log_info("[BACnet][DataLink] [2/2] Initializing MS/TP (RS-485 Serial)...");
        
        // 设置波特率（常用值：9600, 19200, 38400, 76800）
        dlmstp_set_baud_rate(context->config.mstp_baud);
        log_debug("[BACnet][DataLink][MSTP] Baud rate set to {} bps", context->config.mstp_baud);
        
        // 设置 MAC 地址（0-127，唯一标识本设备）
        dlmstp_set_mac_address(context->config.mstp_mac);
        log_debug("[BACnet][DataLink][MSTP] MAC address set to {}", context->config.mstp_mac);
        
        // 设置最大主站地址（默认 127）
        dlmstp_set_max_master(context->config.mstp_max_master);
        log_debug("[BACnet][DataLink][MSTP] Max master set to {}", context->config.mstp_max_master);
        
        // 设置最大信息帧数（默认 1，决定单次令牌持有可发送的帧数）
        dlmstp_set_max_info_frames(context->config.mstp_max_frames);
        log_debug("[BACnet][DataLink][MSTP] Max info frames set to {}", context->config.mstp_max_frames);
        
        // 初始化 MS/TP 数据链路层
        // 参数：
        //   - port: 串口设备路径（如 "/dev/ttyUSB0", "/dev/ttyS1"）
        if (!dlmstp_init(const_cast<char*>(context->config.mstp_port))) {
            log_error("[BACnet][DataLink][MSTP] Failed to initialize MS/TP on port '{}'", 
                      context->config.mstp_port);
            // MS/TP 初始化失败不影响 BACnet/IP，继续运行
            log_warn("[BACnet][DataLink][MSTP] Continuing with BACnet/IP only");
        } else {
            log_info("[BACnet][DataLink][MSTP] Initialized successfully (port: {}, baud: {}, MAC: {})",
                     context->config.mstp_port, context->config.mstp_baud, context->config.mstp_mac);
        }
    } else {
        log_info("[BACnet][DataLink] [2/2] MS/TP not configured, skipping");
        log_debug("[BACnet][DataLink][MSTP] Hint: Set 'mstp.port' in config.yaml to enable");
    }
#else
    // BACnet 库未编译 MS/TP 支持
    if (context->config.mstp_port[0] != '\0') {
        log_warn("[BACnet][DataLink] MS/TP configured but library not compiled with BACDL_MSTP");
        log_warn("[BACnet][DataLink] Hint: Rebuild BACnet library with MS/TP support");
    }
    log_info("[BACnet][DataLink] [2/2] MS/TP not available (library not compiled with BACDL_MSTP)");
#endif  // BACDL_MSTP

    /* ========================================================================== */
    /* 3. 数据链路层初始化完成                                                    */
    /* ========================================================================== */
    
    log_info("[BACnet][DataLink] All datalink layers initialized successfully");
    log_info("[BACnet][DataLink] Protocol stack will auto-route requests based on device address table");
    log_info("[BACnet][DataLink] Optional: Use 'datalink_hint' field to force specific datalink");
    
    return PROTO_SUCCESS;

#else
    /* ========================================================================== */
    /* 单数据链路层模式（编译时未启用 BACDL_MULTIPLE）                              */
    /* ========================================================================== */
    
    log_warn("[BACnet][DataLink] Single datalink mode (BACDL_MULTIPLE not defined)");
    log_warn("[BACnet][DataLink] Only one datalink layer will be initialized at compile time");
    
    // 单数据链路层模式下，dlenv_init() 会根据编译宏初始化对应的数据链路层
    // - BACDL_BIP=1  -> BACnet/IP
    // - BACDL_MSTP=1 -> MS/TP
    // 这里不需要额外的初始化代码
    
    return PROTO_SUCCESS;
#endif
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
    context->target_device_start = context->config.target_device_start;
    context->target_device_end = context->config.target_device_end;

    // 设置缓存策略
    context->cache_strategy = (context->config.cache_strategy == 0) 
                              ? CacheStrategy::Aggressive 
                              : CacheStrategy::Conservative;
    context->cache_expiry_ms = context->config.cache_expiry_ms;
    
    log_debug("[BACnet] Cache strategy: {} (expiry: {}ms)", 
              cache_strategy_to_string(context->cache_strategy),
              context->cache_expiry_ms);

    log_debug("[BACnet] Initializing BACnet protocol stack...");
    // 初始化BACnet协议栈
    Device_Init(nullptr);
    log_debug("[BACnet] Device_Init completed");

    address_init();
    log_debug("[BACnet] address_init completed");

    // 初始化数据链路层环境（加载环境变量等）
    dlenv_init();
    log_debug("[BACnet] dlenv_init completed");

    // 初始化所有配置的数据链路层（BACnet/IP + MS/TP 等）
    proto_status_t dl_status = initialize_all_datalinks(context);
    if (dl_status != PROTO_SUCCESS) {
        log_error("[BACnet] Failed to initialize datalink layers (status: {})", static_cast<int>(dl_status));
        return dl_status;
    }

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
        uint8_t max_attempts = context->config.max_reconnect_attempts;
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
        
        // 检查是否有旧缓存
        bool has_old_cache = state.has_valid_cache;
        
        state.active_invoke_id = invoke_id;
        state.original_request = read_req;
        state.status = PROTO_NO_DATA;  // 请求已发送，等待响应
        
        char invoke_buf[16];
        log_debug("[PLC] Request sent: {}-{}, {} (invoke_id: {})",
                  obj_type_name, read_req->object_instance, prop_name,
                  invoke_id_to_string(invoke_id, invoke_buf, sizeof(invoke_buf)));
        
        // 🔑 关键逻辑：
        // - 有旧缓存：复制旧数据并返回 PROTO_SUCCESS（激进策略：后台更新，前台先用旧数据）
        // - 无缓存：返回 PROTO_NO_DATA（首次请求，等待响应）
        if (has_old_cache && read_req->value) {
            *read_req->value = state.cached_value;
            log_debug("[PLC] Returning cached data while updating in background (age: {}ms)",
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - state.timestamp).count());
            return PROTO_SUCCESS;
        } else {
            return PROTO_NO_DATA;
        }
    }
    
    // 发送失败，返回错误码
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

} // extern "C"

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

