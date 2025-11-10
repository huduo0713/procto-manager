/**
 * @file proto_bacnet_driver.cpp
 * @brief BACnet 驱动管理类实现 - 使用现代 C++ RAII 模式
 */

#include "proto_bacnet_internal.hpp"
#include <cstdlib>  // std::atexit
#include <stdexcept>

namespace bacnet {

// 声明辅助函数（在 proto_bacnet_core.cpp 中实现）
extern proto_status_t initialize_context(BacnetContext *context);
extern void cleanup_context(BacnetContext *context);
extern proto_status_t connect_device(BacnetContext *context);
extern void disconnect_device(BacnetContext *context);

// 声明读写函数（在 proto_bacnet_io.cpp 中实现）
extern proto_status_t execute_read_property(BacnetContext *context, bacnet_read_t *req, uint8_t *invoke_id_out);
extern proto_status_t execute_write_property(BacnetContext *context, const bacnet_write_t *req, uint8_t *invoke_id_out);

/* -------------------------------------------------------------------------- */
/* 单例访问                                                                   */
/* -------------------------------------------------------------------------- */

BacnetDriver& BacnetDriver::instance() {
    static BacnetDriver instance;
    return instance;
}

/* -------------------------------------------------------------------------- */
/* 析构函数 - RAII 自动清理                                                   */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 初始化驱动                                                                 */
/* -------------------------------------------------------------------------- */

int BacnetDriver::initialize(proto_ctx_t* ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    log_info("[BACnet][Driver] Initializing driver...");
    
    // 直接使用单例的初始化方法
    auto& context = BacnetContext::instance();
    proto_status_t status = context.initialize(ctx);
    
    if (status != PROTO_SUCCESS) {
        log_error("[BACnet][Driver] Failed to initialize context");
        return status;
    }
    
    // 注册退出时自动清理（只注册一次）
    static bool cleanup_registered = false;
    if (!cleanup_registered) {
        std::atexit([]() {
            log_info("[BACnet] Program exiting, auto-cleanup resources...");
            bacnet_cleanup();
        });
        cleanup_registered = true;
    }
    
    log_info("[BACnet][Driver] Initialization completed successfully");
    log_info("[BACnet][Driver] Local device instance: {}", 
             context.config.bacnet.local_device.instance_id);
    
    // 启动热配置监控（自动检测配置文件变化）
    if (!config_path_.empty() && context.config.bacnet.hot_config.enabled) {
        // 定义配置变化时的回调函数（lambda 转换为函数指针）
        static auto on_config_changed = +[]() {
            log_warn("[BACnet][HotConfig] Config file changed, reloading...");
            bacnet_reload_config();  // 自动重载配置
        };
        
        int ret = hot_config::init(config_path_.c_str(), on_config_changed, nullptr);
        if (ret == 0) {
            // 设置轮询间隔（从配置文件读取）
            hot_config::set_polling_interval(context.config.bacnet.hot_config.polling_interval_ms);
            
            log_info("[BACnet][Driver] Hot config monitoring started for: {}", config_path_);
            log_info("[BACnet][Driver] Polling interval: {}ms", 
                     context.config.bacnet.hot_config.polling_interval_ms);
        } else {
            log_warn("[BACnet][Driver] Failed to start hot config monitoring (error: {})", ret);
        }
    } else if (!context.config.bacnet.hot_config.enabled) {
        log_info("[BACnet][Driver] Hot config monitoring is disabled in config");
    }
    
    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 释放驱动资源                                                               */
/* -------------------------------------------------------------------------- */

void BacnetDriver::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        return;  // 已经释放
    }
    
    log_info("[BACnet][Driver] Releasing driver resources...");
    
    // 停止热配置监控
    if (hot_config::is_running()) {
        log_info("[BACnet][Driver] Stopping hot config monitoring...");
        hot_config::stop();
    }
    
    // 重置上下文（清理资源但不销毁对象）
    context.reset();
    
    log_info("[BACnet][Driver] Driver released");
}

/* -------------------------------------------------------------------------- */
/* 连接到 BACnet 网络                                                         */
/* -------------------------------------------------------------------------- */

int BacnetDriver::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_error("[BACnet][Driver] Cannot connect: driver not initialized");
        return PROTO_ERROR_INIT;
    }
    
    log_info("[BACnet][Driver] Connecting to BACnet network...");
    
    // 使用现有的连接函数
    return connect_device(&context);
}

/* -------------------------------------------------------------------------- */
/* 断开连接                                                                   */
/* -------------------------------------------------------------------------- */

void BacnetDriver::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        return;
    }
    
    log_info("[BACnet][Driver] Disconnecting from BACnet network...");
    
    // 使用现有的断开连接函数
    disconnect_device(&context);
}

/* -------------------------------------------------------------------------- */
/* 读取操作                                                                   */
/* -------------------------------------------------------------------------- */

int BacnetDriver::read(bacnet_read_t* req) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_error("[BACnet][Driver] Cannot read: driver not initialized");
        return PROTO_ERROR_INIT;
    }
    
    uint8_t invoke_id = 0;
    return execute_read_property(&context, req, &invoke_id);
}

/* -------------------------------------------------------------------------- */
/* 写入操作                                                                   */
/* -------------------------------------------------------------------------- */

int BacnetDriver::write(const bacnet_write_t* req) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_error("[BACnet][Driver] Cannot write: driver not initialized");
        return PROTO_ERROR_INIT;
    }
    
    uint8_t invoke_id = 0;
    return execute_write_property(&context, req, &invoke_id);
}

/* -------------------------------------------------------------------------- */
/* 获取配置                                                                   */
/* -------------------------------------------------------------------------- */

const bacnet_config_t* BacnetDriver::get_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        return nullptr;
    }
    
    return &context.config;
}

/* -------------------------------------------------------------------------- */
/* 热重载配置                                                                 */
/* -------------------------------------------------------------------------- */

int BacnetDriver::reload_config() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_error("[BACnet][Driver] Cannot reload config: driver not initialized");
        return PROTO_ERROR_INIT;
    }
    
    log_info("[BACnet][Driver] Reloading configuration from '{}'...", config_path_);
    
    // 加载新配置
    bacnet_config_t new_config;
    int ret = bacnet_load_config_from_yaml(config_path_.c_str(), &new_config);
    if (ret != 0) {
        log_error("[BACnet][Driver] Failed to reload config");
        return PROTO_ERROR_INIT;
    }
    
    // 应用新配置
    context.config = new_config;
    
    log_info("[BACnet][Driver] Configuration reloaded successfully");
    
    return PROTO_SUCCESS;
}

} // namespace bacnet
