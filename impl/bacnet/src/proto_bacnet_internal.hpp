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
/* 常量定义                                                                   */
/* -------------------------------------------------------------------------- */

inline constexpr const char *kDefaultConfigPath = "../config.yaml";
inline constexpr uint32_t kDefaultReadTimeoutMs = 6000;
inline constexpr uint32_t kDefaultWriteTimeoutMs = 6000;
inline constexpr uint32_t kDefaultDiscoveryTimeoutMs = 5000;
inline constexpr int kMaxReconnectAttempts = 5;
inline constexpr int kReconnectIntervalMs = 3000;

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
/* 异步回调函数类型（预留，暂未使用）                                          */
/* -------------------------------------------------------------------------- */

// 当前采用事件队列方式，回调机制预留待后续扩展
using AsyncCallback = std::function<void(proto_status_t status, void* userdata)>;

/* -------------------------------------------------------------------------- */
/* BACnet上下文类（使用现代C++特性实现）                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief BACnet 上下文类 - 使用现代 C++ 特性
 * 
 * 设计原则：
 * 1. RAII - 资源自动管理（智能指针、析构函数）
 * 2. 线程安全 - 原子变量 + 互斥锁 + 条件变量
 * 3. 事件驱动 - 异步操作 + 事件队列
 * 4. 模块化 - 职责清晰，易于扩展
 */
class BacnetContext {
public:
    // 构造和析构
    BacnetContext() = default;
    ~BacnetContext() {
        cleanup_resources();
    }
    // 禁止拷贝，允许移动
    BacnetContext(const BacnetContext&) = delete;
    BacnetContext& operator=(const BacnetContext&) = delete;
    BacnetContext(BacnetContext&&) noexcept = default;
    BacnetContext& operator=(BacnetContext&&) noexcept = default;

public:
    // 哈希表队列定义（使用 invoke_id 作为 key）
    struct ReadBufferItem {
        uint32_t device_instance;
        uint16_t object_type;
        uint32_t object_instance;
        uint32_t property_id;
        bacnet_data_value_t value;
        proto_status_t status;
        bool is_completed;  // 是否已由回调函数完成
        std::chrono::steady_clock::time_point timestamp;
        bacnet_read_t *original_request;  // 原始请求指针，用于事件关联
        uint8_t invoke_id;  // BACnet协议的调用ID，用于精确匹配
    };
    struct WriteBufferItem {
        uint32_t device_instance;
        uint16_t object_type;
        uint32_t object_instance;
        uint32_t property_id;
        bacnet_data_value_t value;
        uint8_t priority;
        uint32_t array_index;
        uint32_t length;
        proto_status_t status;
        bool is_completed;  // 是否已由回调函数完成
        std::chrono::steady_clock::time_point timestamp;
        bacnet_write_t *original_request;  // 原始请求指针，用于事件关联
        uint8_t invoke_id;  // BACnet协议的调用ID，用于精确匹配
    };
    
    // 读写队列：使用哈希表存储，invoke_id 作为 key，实现 O(1) 查找和删除
    std::unordered_map<uint8_t, ReadBufferItem> read_queue;
    std::mutex read_queue_mutex;
    
    std::unordered_map<uint8_t, WriteBufferItem> write_queue;
    std::mutex write_queue_mutex;
    std::condition_variable write_queue_cv;

    /* ---------------------------------------------------------------------- */
    /* 公共成员变量（内部使用）                                               */
    /* ---------------------------------------------------------------------- */
    
    // 基础上下文
    proto_ctx_t *ctx{nullptr};
    bacnet_config_t config{};
    uint32_t target_device_start{0};     // 目标设备实例范围起始
    uint32_t target_device_end{0};       // 目标设备实例范围结束

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
    };
    std::mutex callback_mutex;
    std::unordered_map<std::string, CallbackInfo> callbacks;

private:
    // 资源清理（析构时自动调用）
    void cleanup_resources() {
        if (worker_thread && worker_thread->joinable()) {
            worker_stop.store(true, std::memory_order_release);
            worker_thread->join();
        }
    }
};

/* -------------------------------------------------------------------------- */
/* 全局实例管理（用于C回调函数访问）                                          */
/* -------------------------------------------------------------------------- */

extern BacnetContext *g_ctx;

/* -------------------------------------------------------------------------- */
/* 核心功能函数声明（proto_bacnet_core.cpp）                                  */
/* -------------------------------------------------------------------------- */

BacnetContext* get_context(proto_ctx_t *ctx);
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

} // namespace bacnet
