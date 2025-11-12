#pragma once

#include "proto_bacnet.h"
#include "common/utils/one_logger.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/fmt/fmt.h>

extern "C" {
#include "bacnet/apdu.h"
#include "bacnet/bacaddr.h"
#include "bacnet/bacapp.h"
#include "bacnet/bacdef.h"
#include "bacnet/bacerror.h"
#include "bacnet/bactext.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/rp.h"
#include "bacnet/wp.h"
#include "bacnet/whois.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/sys/mstimer.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
}

/* -------------------------------------------------------------------------- */
/* fmt 格式化器定义                                                           */
/* -------------------------------------------------------------------------- */

template <>
struct fmt::formatter<BACnetObjectType> : fmt::formatter<int> {
    auto format(BACnetObjectType obj_type, format_context& ctx) const {
        return fmt::formatter<int>::format(static_cast<int>(obj_type), ctx);
    }
};

template <>
struct fmt::formatter<BACNET_PROPERTY_ID> : fmt::formatter<int> {
    auto format(BACNET_PROPERTY_ID prop_id, format_context& ctx) const {
        return fmt::formatter<int>::format(static_cast<int>(prop_id), ctx);
    }
};

template <>
struct fmt::formatter<proto_status_t> : fmt::formatter<int> {
    auto format(proto_status_t status, format_context& ctx) const {
        return fmt::formatter<int>::format(static_cast<int>(status), ctx);
    }
};

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* BACnet地址哈希和比较函数（用于unordered_map）                                     */
/* -------------------------------------------------------------------------- */

struct BACnetAddressHash {
    std::size_t operator()(const BACNET_ADDRESS& addr) const {
        std::size_t h = 0;
        h = std::hash<uint8_t>()(addr.len);
        h = h * 31 + std::hash<uint8_t>()(addr.net);
        for (int i = 0; i < MAX_MAC_LEN && i < addr.len; ++i) {
            h = h * 31 + std::hash<uint8_t>()(addr.adr[i]);
        }
        return h;
    }
};

struct BACnetAddressEqual {
    bool operator()(const BACNET_ADDRESS& a, const BACNET_ADDRESS& b) const {
        if (a.len != b.len || a.net != b.net) return false;
        for (int i = 0; i < a.len; ++i) {
            if (a.adr[i] != b.adr[i]) return false;
        }
        return true;
    }
};

/* ========================================================================== */
/* BACnet 协议驱动 - 内部实现（C++）                                          */
/* ========================================================================== */
/*
 * 架构说明：
 * 
 * 对外接口层（C 接口）:
 *   plc_proto_read()  ─┐
 *   plc_proto_write() ─┤ → 唯一对外暴露的接口
 *                      │
 * ═══════════════════════════════════════════════════════════════════════
 *                      ↓
 * 内部实现层（C++）:
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │ BacnetContext 类                                                │
 *   │  - 使用现代 C++ (智能指针、原子变量、RAII)                       │
 *   │  - 线程安全（互斥锁 + 条件变量）                                 │
 *   │  - 事件驱动（异步操作 + 事件队列）                               │
 *   └─────────────────────────────────────────────────────────────────┘
 *          │
 *          ├─→ proto_bacnet_core.cpp       (核心：初始化/连接/状态管理)
 *          ├─→ proto_bacnet_discovery.cpp  (设备发现 + 工作线程)
 *          ├─→ proto_bacnet_io.cpp         (读写操作)
 *          ├─→ proto_bacnet_callbacks.cpp  (BACnet 协议栈回调)
 *          ├─→ proto_bacnet_utils.cpp      (工具：配置加载)
 *          └─→ proto_bacnet_hot_config.cpp (热配置：文件监控)
 * 
 * 工作流程：
 *   1. plc_proto_read/write() 检查连接状态
 *   2. 自动调用 proto_connect() 建立连接
 *   3. 调用 execute_read_property() / execute_write_property()
 *   4. 工作线程处理 BACnet 协议栈事件
 *   5. 回调函数推送事件到队列
 *   6. plc_proto_read/write() 阻塞等待事件
 *   7. 返回结果给调用者
 */

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 配置默认值 - 所有常量集中管理（修改这里即可调整全局默认值）                */
/* -------------------------------------------------------------------------- */
/*
 * 说明：
 * 1. 这里定义的是兜底默认值，当 config.yaml 不存在或配置项缺失时使用
 * 2. 所有配置项都可以通过 config.yaml 文件覆盖
 * 3. 配置优先级：用户传入值 > YAML配置 > 这里的常量
 * 4. 修改默认值时，请同步更新 config.yaml 中的注释
 */

namespace defaults {


/* ========================================================================== */
/* System 系统配置                                                            */
/* ========================================================================== */
inline constexpr const char* kConfigPath = "../config.yaml"; // 配置文件路径


/* ========================================================================== */
/* Common 通用配置默认值                                                      */
/* ========================================================================== */
inline constexpr const char* kEnvironment = "development";  // 运行环境标识
inline constexpr const char* kLogLevel = "debug";           // 日志等级
inline constexpr const char* kLogFile = "bacnet.log";       // 日志文件路径

/* ========================================================================== */
/* Discovery 设备发现配置默认值                                               */
/* ========================================================================== */
inline constexpr uint32_t kTargetDeviceStart = 5678;        // 目标设备实例范围起始
inline constexpr uint32_t kTargetDeviceEnd = 5678;          // 目标设备实例范围结束
inline constexpr uint8_t kWhoIsRetry = 3;                   // Who-Is 重试次数
inline constexpr uint32_t kDiscoveryTimeoutMs = 5000;       // 等待 I-Am 响应超时时间(毫秒)

/* ========================================================================== */
/* LocalDevice 本地设备配置默认值                                             */
/* ========================================================================== */
inline constexpr uint32_t kLocalDeviceInstance = 4194303;   // 本地设备实例ID(BACnet最大值)
inline constexpr uint16_t kMaxApdu = 1476;                  // 最大APDU长度(BACnet/IP标准)

/* ========================================================================== */
/* Network 网络配置默认值                                                     */
/* ========================================================================== */
inline constexpr uint16_t kPort = 47808;                    // BACnet/IP UDP端口(标准端口)
inline constexpr const char* kBroadcastAddress = "255.255.255.255";  // 广播地址

/* ========================================================================== */
/* Services 服务行为配置默认值                                                */
/* ========================================================================== */
inline constexpr uint32_t kReadTimeoutMs = 6000;            // 读操作超时时间(毫秒)
inline constexpr uint32_t kWriteTimeoutMs = 6000;           // 写操作超时时间(毫秒)
inline constexpr uint8_t kDefaultPriority = 8;              // 写属性默认优先级(1-16)
inline constexpr uint32_t kCacheExpiryMs = 1000;            // 缓存过期时间(毫秒)
inline constexpr uint8_t kCacheStrategy = 0;                // 缓存策略: 0=激进 1=保守
inline constexpr uint32_t kDatalinkMaintenanceMs = 1000;    // DataLink维护定时器间隔(毫秒)

/* ========================================================================== */
/* Connection 连接管理配置默认值                                              */
/* ========================================================================== */
inline constexpr uint8_t kMaxReconnectAttempts = 5;         // 最大重连次数
inline constexpr uint32_t kReconnectIntervalMs = 3000;      // 重连间隔基准时间(毫秒)

/* ========================================================================== */
<<<<<<< HEAD
=======
/* System 系统配置                                                            */
/* ========================================================================== */
inline constexpr const char* kConfigPath = "/usr/runtime/protocol/bacnet/config.yaml"; // 配置文件路径

/* ========================================================================== */
>>>>>>> ba65310... [fix]配置文件拷贝到运行目录，方便调试
/* HotConfig 热配置监控默认值                                                 */
/* ========================================================================== */
inline constexpr uint32_t kHotConfigPollingIntervalMs = 1000; // 配置文件轮询间隔(毫秒)
inline constexpr bool kHotConfigEnabled = true;               // 是否启用热配置监控

} // namespace defaults

/* -------------------------------------------------------------------------- */
/* 配置来源追踪（C++ 内部使用）                                               */
/* -------------------------------------------------------------------------- */

// 配置项来源标记
enum class ConfigSource {
    Default,    // 使用代码默认值
    Yaml,       // 从 YAML 文件加载
    UserParam   // 用户通过参数传入（未来扩展）
};

// 配置元数据：追踪每个配置项的来源
struct ConfigMetadata {
    // Common
    ConfigSource environment{ConfigSource::Default};
    ConfigSource log_level{ConfigSource::Default};
    ConfigSource log_file{ConfigSource::Default};
    
    // Discovery
    ConfigSource target_device_start{ConfigSource::Default};
    ConfigSource target_device_end{ConfigSource::Default};
    ConfigSource whois_retry{ConfigSource::Default};
    ConfigSource response_timeout_ms{ConfigSource::Default};
    
    // LocalDevice
    ConfigSource instance_id{ConfigSource::Default};
    ConfigSource max_apdu{ConfigSource::Default};
    
    // Network
    ConfigSource interface_name{ConfigSource::Default};
    ConfigSource port{ConfigSource::Default};
    ConfigSource broadcast_address{ConfigSource::Default};
    
    // Services
    ConfigSource read_timeout_ms{ConfigSource::Default};
    ConfigSource write_timeout_ms{ConfigSource::Default};
    ConfigSource default_priority{ConfigSource::Default};
    ConfigSource cache_expiry_ms{ConfigSource::Default};
    ConfigSource cache_strategy{ConfigSource::Default};
    ConfigSource datalink_maintenance_ms{ConfigSource::Default};
    
    // Connection
    ConfigSource max_reconnect_attempts{ConfigSource::Default};
    ConfigSource reconnect_interval_ms{ConfigSource::Default};
    
    // HotConfig
    ConfigSource hot_config_enabled{ConfigSource::Default};
    ConfigSource hot_config_polling_interval_ms{ConfigSource::Default};
    
    // BACnet enabled
    ConfigSource bacnet_enabled{ConfigSource::Default};
};

/* -------------------------------------------------------------------------- */
/* 操作类型枚举                                                               */
/* -------------------------------------------------------------------------- */

enum class OperationKind {
    None = 0,
    Read,
    Write,
    Discovery
};

/* -------------------------------------------------------------------------- */
/* 缓存策略枚举                                                               */
/* -------------------------------------------------------------------------- */

enum class CacheStrategy {
    Aggressive = 0,  // 激进策略：每次都发送请求，尽可能获取最新数据
    Conservative     // 保守策略：有缓存且未过期时直接返回缓存
};

/* -------------------------------------------------------------------------- */
/* 对象键定义（用于缓存查找）                                                 */
/* -------------------------------------------------------------------------- */

struct ObjectKey {
    uint32_t device_instance;
    uint16_t object_type;
    uint32_t object_instance;
    uint32_t property_id;

    bool operator==(const ObjectKey& other) const {
        return device_instance == other.device_instance &&
               object_type == other.object_type &&
               object_instance == other.object_instance &&
               property_id == other.property_id;
    }
};

// ObjectKey 的哈希函数
struct ObjectKeyHash {
    std::size_t operator()(const ObjectKey& key) const {
        std::size_t h1 = std::hash<uint32_t>()(key.device_instance);
        std::size_t h2 = std::hash<uint16_t>()(key.object_type);
        std::size_t h3 = std::hash<uint32_t>()(key.object_instance);
        std::size_t h4 = std::hash<uint32_t>()(key.property_id);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

/* -------------------------------------------------------------------------- */
/* 活动操作结构                                                               */
/* -------------------------------------------------------------------------- */

struct ActiveOperation {
    OperationKind type{OperationKind::None};
    void *request{nullptr};              // 指向 bacnet_read_t 或 bacnet_write_t
    BACNET_ADDRESS target_address{};
    uint8_t invoke_id{0};
    uint32_t device_instance{0};
    std::chrono::steady_clock::time_point start_time;
    uint32_t timeout_ms{0};
    
    void reset() {
        type = OperationKind::None;
        request = nullptr;
        std::memset(&target_address, 0, sizeof(target_address));
        invoke_id = 0;
        device_instance = 0;
        timeout_ms = 0;
    }
    
    bool is_active() const {
        return type != OperationKind::None;
    }
    
    bool is_timeout() const {
        if (!is_active()) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        return elapsed >= timeout_ms;
    }
};

/* -------------------------------------------------------------------------- */
/* 异步回调函数类型定义                                                       */
/* -------------------------------------------------------------------------- */

// 异步回调函数类型（必须在 BacnetContext 之前定义）
using AsyncCallback = std::function<void(proto_status_t, void*)>;

/* -------------------------------------------------------------------------- */
/* BACnet上下文类（单例模式 - 线程安全）                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief BACnet 上下文类 - 单例模式
 * 
 * 设计原则：
 * 1. 单例模式 - 全局唯一实例，线程安全（C++11 保证）
 * 2. RAII - 资源自动管理（智能指针、析构函数）
 * 3. 线程安全 - 原子变量 + 互斥锁 + 条件变量
 * 4. 事件驱动 - 异步操作 + 事件队列
 * 5. 可重置 - 支持热配置重载（reset 而非销毁）
 */
class BacnetContext {
public:
    // ============ 单例访问接口 ============
    static BacnetContext& instance() {
        static BacnetContext ctx;  // C++11 保证线程安全的初始化
        return ctx;
    }
    
    // 初始化上下文（相当于构造）
    proto_status_t initialize(proto_ctx_t* ctx);
    
    // 重置上下文（用于热配置重载）
    void reset();
    
    // 检查是否已初始化
    bool is_initialized() const { 
        return initialized_.load(std::memory_order_acquire); 
    }
    
    // 禁止拷贝和移动
    BacnetContext(const BacnetContext&) = delete;
    BacnetContext& operator=(const BacnetContext&) = delete;
    BacnetContext(BacnetContext&&) = delete;
    BacnetContext& operator=(BacnetContext&&) = delete;

private:
    // 私有构造和析构（单例模式）
    BacnetContext() = default;
    ~BacnetContext() {
        // ⚠️ 析构函数在程序退出时调用
        // 不做任何清理，原因：
        // 1. 其他全局对象（日志、BACnet 协议栈）可能已被销毁
        // 2. 工作线程可能在访问 Context 成员，join 可能死锁或崩溃
        // 3. 操作系统会自动回收所有资源（线程、内存、网络端口）
        // 
        // 完整的资源清理应该在程序退出前显式调用 driver.release()
    }
    
    // 初始化标志
    std::atomic<bool> initialized_{false};

public:
    // 对象状态定义（用于缓存）
    struct ObjectState {
        bacnet_data_value_t cached_value;          // 缓存的值
        proto_status_t status;                     // 最后一次操作的状态
        uint8_t active_invoke_id;                  // 当前活跃的请求 invoke_id (0表示无活跃请求)
        std::chrono::steady_clock::time_point timestamp;  // 缓存更新时间
        bool has_valid_cache;                      // 是否有有效缓存
        bacnet_read_t *original_request;           // 原始请求指针，用于事件关联
    };

    // 主缓存表：对象 -> 状态
    std::unordered_map<ObjectKey, ObjectState, ObjectKeyHash> object_states;
    std::mutex object_states_mutex;

    // 反向映射表：invoke_id -> ObjectKey（用于回调查找）
    std::unordered_map<uint8_t, ObjectKey> invoke_id_to_key;
    std::mutex invoke_id_to_key_mutex;

    // 写操作队列（写操作不需要缓存，仍用 invoke_id 作为 key）
    // 写操作待确认项
    // 保留必要字段用于详细日志记录、调试和超时清理
    struct WriteBufferItem {
    // 🔍 基础标识字段（必须）
    uint32_t device_instance;      // 设备实例
    uint16_t object_type;          // 对象类型
    uint32_t object_instance;      // 对象实例
    uint32_t property_id;          // 属性 ID
    uint8_t invoke_id;             // BACnet 调用 ID
    
    // 📊 详细信息字段（用于完整日志）
    bacnet_data_value_t value;     // ✅ 写入的值（显示在日志中）
    uint8_t priority;              // ✅ 写入优先级（显示在日志中）
    uint32_t array_index;          // ✅ 数组索引（显示在日志中）
    
    // ⏱️ 时间戳和超时配置（用于超时检测）
    std::chrono::steady_clock::time_point timestamp;
    uint32_t timeout_ms;           // 该请求的超时时间（三层优先级后的最终值）
};
    
    // 写操作待确认哈希表（invoke_id -> WriteBufferItem）
    // 用于跟踪已发送但未收到 ACK 的写请求，O(1) 查找复杂度
    // 收到 ACK/Error 后立即删除，避免内存泄漏
    std::unordered_map<uint8_t, WriteBufferItem> write_pending_map;
    std::mutex write_pending_mutex;
    std::condition_variable write_pending_cv;

    /* ---------------------------------------------------------------------- */
    /* 公共成员变量（内部使用）                                               */
    /* ---------------------------------------------------------------------- */
    
    // 基础上下文
    proto_ctx_t *ctx{nullptr};
    bacnet_config_t config{};
    uint32_t target_device_start{0};     // 目标设备实例范围起始
    uint32_t target_device_end{0};       // 目标设备实例范围结束

    // 缓存配置
    CacheStrategy cache_strategy{CacheStrategy::Aggressive};  // 默认激进策略
    uint32_t cache_expiry_ms{bacnet::defaults::kCacheExpiryMs};  // 缓存过期时间

    // 连接状态（原子变量 - 无锁读写）
    std::atomic<bacnet_connection_state_t> connection_state{BACNET_CONN_IDLE};
    std::atomic<bacnet_operation_state_t> operation_state{BACNET_OP_IDLE};
    std::atomic<bool> target_found{false};
    std::atomic<int> reconnect_attempts{0};

    // 活动操作（互斥锁保护）
    mutable std::mutex operation_mutex;
    ActiveOperation active_operation{};

    // 工作线程（智能指针管理）
    std::unique_ptr<std::thread> worker_thread{nullptr};
    std::atomic<bool> worker_running{false};
    std::atomic<bool> worker_stop{false};

    // 设备地址映射
    std::unordered_map<BACNET_ADDRESS, uint32_t, BACnetAddressHash, BACnetAddressEqual> device_address_to_id;

    // 接收缓冲区
    uint8_t rx_buffer[1500]{};

    // 热配置
    std::mutex hot_config_mutex;
    bool pending_reload{false};

    // 回调管理
    struct CallbackInfo {
        AsyncCallback callback;
        void* userdata;
        
        // 默认构造函数
        CallbackInfo() : callback(nullptr), userdata(nullptr) {}
        
        // 带参数构造函数
        CallbackInfo(AsyncCallback cb, void* ud) : callback(cb), userdata(ud) {}
    };
    std::mutex callback_mutex;
    std::unordered_map<std::string, CallbackInfo> callbacks;

private:
    // 资源清理（析构时自动调用）
    void cleanup_resources() noexcept {
        // ⚠️ 重要说明：
        // 析构函数在程序退出时调用，此时其他全局对象（日志系统、BACnet 全局状态）
        // 可能已被销毁。因此只做最基本的清理，避免访问可能无效的外部资源。
        // 
        // 完整的资源清理应该通过 reset() 在程序正常运行时完成。
        // 程序退出时，操作系统会自动回收所有资源（内存、网络端口等）。
        
        if (!initialized_.load(std::memory_order_acquire)) {
            return;  // 未初始化，无需清理
        }
        
        try {
            // 只停止工作线程 - 这是安全且必须的
            if (worker_thread) {
                worker_stop.store(true, std::memory_order_release);
                if (worker_thread->joinable()) {
                    worker_thread->join();
                }
                worker_thread.reset();
            }
            
            // ❌ 不调用以下函数，避免 Bus error：
            // - address_remove_device() - 可能访问已销毁的地址缓存
            // - bip_cleanup() - 可能访问已销毁的 BIP 全局状态
            // - datalink_cleanup() - 可能访问已销毁的 datalink 全局状态
            // 
            // 原因：程序退出时这些全局对象可能已被销毁，访问会导致 Bus error
            // 操作系统会自动回收网络端口和内存资源
            
            initialized_.store(false, std::memory_order_release);
            
        } catch (...) {
            // 析构函数不能抛出异常，静默处理
        }
    }
    
    // plc_mutex 用于保护初始化和重置操作
    std::mutex plc_mutex;
};

/* -------------------------------------------------------------------------- */
/* 核心功能函数声明（proto_bacnet_core.cpp）                                  */
/* -------------------------------------------------------------------------- */

proto_status_t initialize_context(BacnetContext *context);
void cleanup_context(BacnetContext *context);
proto_status_t connect_device(BacnetContext *context);
void disconnect_device(BacnetContext *context);

/* -------------------------------------------------------------------------- */
/* 设备发现函数声明（proto_bacnet_discovery.cpp）                           */
/* -------------------------------------------------------------------------- */

proto_status_t discover_target_device(BacnetContext *context);
void worker_loop_function(BacnetContext *context);
void start_worker_thread(BacnetContext *context);
void stop_worker_thread(BacnetContext *context);
void check_and_reconnect_if_needed(BacnetContext *context);
void cleanup_stale_requests(BacnetContext *context);

/* -------------------------------------------------------------------------- */
/* 读写操作函数声明（proto_bacnet_io.cpp）                                    */
/* -------------------------------------------------------------------------- */

proto_status_t execute_read_property(BacnetContext *context, bacnet_read_t *req, uint8_t *invoke_id_out);
proto_status_t execute_write_property(BacnetContext *context, const bacnet_write_t *req, uint8_t *invoke_id_out);
BACNET_ADDRESS resolve_target_address(BacnetContext *context, uint32_t device_instance, proto_status_t &status);

/* -------------------------------------------------------------------------- */
/* 回调处理函数声明（proto_bacnet_callbacks.cpp）                             */
/* -------------------------------------------------------------------------- */

void handle_iam_callback(uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src);
void handle_read_property_ack(uint8_t *service_request, uint16_t service_len, 
                               BACNET_ADDRESS *src, BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data);
void handle_write_property_ack(BACNET_ADDRESS *src, uint8_t invoke_id);
void handle_error_response(BACNET_ADDRESS *src, uint8_t invoke_id, 
                           BACNET_ERROR_CLASS error_class, BACNET_ERROR_CODE error_code);
void handle_abort_response(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server);
void handle_reject_response(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason);

void register_bacnet_handlers(BacnetContext *context);

/* -------------------------------------------------------------------------- */
/* 工具函数声明（proto_bacnet_utils.cpp中已有配置加载）                       */
/* -------------------------------------------------------------------------- */

proto_status_t store_application_value(bacnet_read_t *req, const BACNET_APPLICATION_DATA_VALUE &value);
bool convert_to_application_value(const bacnet_data_value_t &input, BACNET_APPLICATION_DATA_VALUE &output);

/* -------------------------------------------------------------------------- */
/* 状态转换函数（增强日志可读性）                                             */
/* -------------------------------------------------------------------------- */

const char* connection_state_to_string(bacnet_connection_state_t state);
const char* cache_strategy_to_string(CacheStrategy strategy);
bool is_invoke_id_valid(uint8_t invoke_id);
const char* invoke_id_to_string(uint8_t invoke_id, char *buffer, size_t buffer_size);

/* -------------------------------------------------------------------------- */
/* 事件和状态管理函数                                                         */
/* -------------------------------------------------------------------------- */

void reset_active_operation_locked(BacnetContext *context);
bool get_active_operation_snapshot(BacnetContext *context, ActiveOperation &snapshot);
void finalize_operation(BacnetContext *context, proto_status_t status);

void set_connection_state(BacnetContext *context, bacnet_connection_state_t state);
bacnet_connection_state_t get_connection_state(BacnetContext *context);
void set_operation_state(BacnetContext *context, bacnet_operation_state_t state);
bacnet_operation_state_t get_operation_state(BacnetContext *context);

/* -------------------------------------------------------------------------- */
/* 回调函数管理                                                               */
/* -------------------------------------------------------------------------- */

void set_callback(BacnetContext *context, const char* type, AsyncCallback callback, void* userdata);
void trigger_callback(BacnetContext *context, const char* type, proto_status_t status);

/* -------------------------------------------------------------------------- */
/* 现代 C++ 驱动管理类（RAII）                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief BACnet 驱动管理器 - 使用现代 C++ RAII 模式
 * 
 * 设计特性：
 * - RAII: 构造时初始化，析构时自动清理
 * - 单例模式: 全局唯一实例
 * - 异常安全: 使用 std::optional 和 std::unique_ptr
 * - 线程安全: 内部使用 mutex 保护
 */
class BacnetDriver {
public:
    // 获取全局单例
    static BacnetDriver& instance();
    
    // 禁止拷贝和移动
    BacnetDriver(const BacnetDriver&) = delete;
    BacnetDriver& operator=(const BacnetDriver&) = delete;
    BacnetDriver(BacnetDriver&&) = delete;
    BacnetDriver& operator=(BacnetDriver&&) = delete;
    
    // 初始化驱动（返回 proto_status_t）
    int initialize(proto_ctx_t* ctx);
    
    // 释放驱动资源
    void release();
    
    // 连接到 BACnet 网络
    int connect();
    
    // 断开 BACnet 网络连接
    void disconnect();
    
    // 读取操作
    int read(bacnet_read_t* req);
    
    // 写入操作
    int write(const bacnet_write_t* req);
    
    // 获取上下文指针（直接返回单例）
    BacnetContext* get_context() { return &BacnetContext::instance(); }
    
    // 检查是否已初始化
    bool is_initialized() const { return BacnetContext::instance().is_initialized(); }
    
    // 获取配置
    const bacnet_config_t* get_config() const;
    
    // 热重载配置
    int reload_config();
    
private:
    // 私有构造函数（单例模式）
    BacnetDriver() = default;
    ~BacnetDriver() {
        // ⚠️ Driver 是单例，析构函数在程序退出时调用
        // 不做任何清理，原因同 BacnetContext：
        // 1. Context 也是单例，可能已被销毁
        // 2. 析构顺序不确定，可能导致访问已销毁对象
        // 3. 操作系统会自动回收所有资源
        // 
        // 正确的清理方式：
        // - C API 用户：在 main() 结束前调用 plc_proto_cleanup()
        // - C++ API 用户：在程序退出前调用 driver.release()
    }
    
    // 保护并发访问
    mutable std::mutex mutex_;
    
    // 配置路径
    std::string config_path_{bacnet::defaults::kConfigPath};
};

/* -------------------------------------------------------------------------- */
/* 热配置监控接口（仅供 BacnetDriver 内部使用）                               */
/* -------------------------------------------------------------------------- */

namespace hot_config {

// 初始化热配置监控
int init(const char *config_path, void (*on_changed)(void), void *userdata);

// 停止监控线程
void stop();

// 清理资源
void cleanup();

// 检查监控线程是否在运行
bool is_running();

// 设置轮询间隔
void set_polling_interval(uint32_t interval_ms);

} // namespace hot_config

} // namespace bacnet

/* -------------------------------------------------------------------------- */
/* 内部函数声明（不对外暴露，仅内部使用 - C++ 链接）                         */
/* -------------------------------------------------------------------------- */

// 配置加载函数（内部使用）
int bacnet_load_config_from_yaml(const char *yaml_path, bacnet_config_t *cfg);

// 配置重载函数（由热配置监控自动调用）
int bacnet_reload_config(void);

// 资源清理函数（在 atexit() 中调用）
int bacnet_cleanup(void);


