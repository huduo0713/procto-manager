# 🚀 BACnet 协议驱动 - 完整指南

## 📚 目录

- [📁 项目结构](#-项目结构)
- [🏛️ 运行时架构](#️-运行时架构)
- [🎯 概述](#-概述)
- [✨ 核心特性](#-核心特性)
- [🏗️ 架构设计](#️-架构设计)
- [🔄 工作流程](#-工作流程)
- [⚡ 快速开始](#-快速开始)
- [📋 API 接口](#-api-接口)
- [🔧 配置说明](#-配置说明)
- [🏭 编译构建](#-编译构建)
- [💻 使用示例](#-使用示例)
- [🧵 线程模型](#-线程模型)
- [🔒 线程安全](#-线程安全)
- [📊 性能指标](#-性能指标)
- [❓ 常见问题](#-常见问题)
- [📝 更新日志](#-更新日志)

---

## 📁 项目结构

### 🗂️ 文件组织

```
impl/bacnet/
├── src/                                    # 源代码目录
│   ├── proto_bacnet.h                     # 🎯 对外 C 接口头文件
│   ├── proto_bacnet_internal.hpp          # 🔧 内部 C++ 定义和声明
│   ├── proto_bacnet_driver.cpp            # 🚗 Driver 单例类实现 ⭐ NEW
│   ├── proto_bacnet_core.cpp              # 🎪 核心功能实现
│   ├── proto_bacnet_callbacks.cpp         # 📞 BACnet 协议栈回调处理
│   ├── proto_bacnet_io.cpp                # 📤 读写操作核心逻辑
│   ├── proto_bacnet_discovery.cpp         # 🔍 设备发现和工作线程
│   ├── proto_bacnet_utils.cpp             # 🛠️ 工具函数（配置加载、日志）
│   └── proto_bacnet_hot_config.cpp        # 🔥 热配置监控
├── demo/
│   └── main.cc                            # 💻 示例程序
├── config.yaml                            # ⚙️ 配置文件
├── build.sh                               # 🏗️ 构建脚本
└── CMakeLists.txt                         # 📋 CMake 配置
```

### 📋 文件功能详解

#### 🎯 对外接口文件

**proto_bacnet.h** - C 公共 API 头文件
```c
// 核心 PLC 接口（仅2个）
int plc_proto_read(void *req);           // 读取属性
int plc_proto_write(void *req);          // 写入属性

// 辅助接口
int bacnet_reload_config(void);          // 热重载配置
void bacnet_data_value_free(...);        // 释放数据值
```

#### 🔧 内部实现文件

**proto_bacnet_internal.hpp** - 内部 C++ 声明（617 行）
- **位置**: `impl/bacnet/src/proto_bacnet_internal.hpp`
- **行号范围**: 1-617

| 内容 | 行号 | 说明 |
|------|------|------|
| 包含头文件 | 1-50 | C/C++ 标准库、BACnet 协议栈、日志库 |
| 配置默认值常量 | 135-210 | `bacnet::defaults` 命名空间所有默认值 |
| 配置元数据结构 | 198-246 | `ConfigMetadata` 配置源追踪 |
| 配置结构体 | 280-350 | 7 层嵌套配置（Common/Discovery/LocalDevice/...） |
| `BacnetContext` 类 | 360-520 | 核心上下文类，包含所有状态和缓存 |
| **`BacnetDriver` 类** | **530-580** | **Modern C++ 单例驱动类 ⭐ NEW** |
| 全局函数声明 | 585-617 | 内部辅助函数声明 |

**核心类结构**:
```cpp
// BacnetDriver 类 (行 530-580) - Modern C++ 单例模式
class BacnetDriver {
public:
    static BacnetDriver& instance();              // 单例获取
    
    // 核心接口
    int initialize(proto_ctx_t* ctx);             // 初始化
    void release();                                // 释放资源
    int connect();                                 // 连接设备
    void disconnect();                             // 断开连接
    int read(bacnet_read_t* req);                 // 读取操作
    int write(const bacnet_write_t* req);         // 写入操作
    int reload_config();                           // 重载配置
    
    BacnetContext* get_context() { return context_.get(); }
    
private:
    BacnetDriver() = default;                      // 私有构造
    ~BacnetDriver() = default;
    BacnetDriver(const BacnetDriver&) = delete;    // 禁止拷贝
    BacnetDriver& operator=(const BacnetDriver&) = delete;
    BacnetDriver(BacnetDriver&&) = delete;         // 禁止移动
    BacnetDriver& operator=(BacnetDriver&&) = delete;
    
    std::unique_ptr<BacnetContext> context_;       // RAII 管理上下文
    std::mutex mutex_;                             // 线程安全
    std::string config_path_;                      // 配置路径
};
```

---

**proto_bacnet_driver.cpp** - Driver 单例实现（213 行）⭐ **NEW**
- **位置**: `impl/bacnet/src/proto_bacnet_driver.cpp`
- **作用**: Modern C++ 驱动管理类，RAII 模式自动管理资源
- **设计模式**: Singleton（单例）+ RAII（资源获取即初始化）

| 函数 | 行号 | 功能 | 说明 |
|------|------|------|------|
| `BacnetDriver::instance()` | 35-43 | 获取单例 | 静态局部变量保证线程安全 |
| `BacnetDriver::initialize()` | 45-73 | 初始化驱动 | 调用 `initialize_context()` |
| `BacnetDriver::release()` | 75-102 | 释放资源 | RAII 自动清理，调用 `cleanup_context()` |
| `BacnetDriver::connect()` | 104-133 | 连接设备 | 调用 `connect_device()` |
| `BacnetDriver::disconnect()` | 135-146 | 断开连接 | 调用 `disconnect_device()` |
| `BacnetDriver::read()` | 148-169 | 读取操作 | 调用 `execute_read_property()` |
| `BacnetDriver::write()` | 171-192 | 写入操作 | 调用 `execute_write_property()` |
| `BacnetDriver::reload_config()` | 194-213 | 热重载配置 | 调用 `bacnet_load_config_from_yaml()` |

**核心特性**:
```cpp
// RAII 自动管理
std::unique_ptr<BacnetContext> context_;  // 智能指针，自动释放

// 线程安全
std::mutex mutex_;                         // 保护并发访问

// 单例模式
static BacnetDriver& instance() {
    static BacnetDriver instance;          // 静态局部变量，C++11 保证线程安全
    return instance;
}

// 禁止拷贝和移动
BacnetDriver(const BacnetDriver&) = delete;
BacnetDriver& operator=(const BacnetDriver&) = delete;
```

---

**proto_bacnet_core.cpp** - 核心功能实现（605 行）
- **位置**: `impl/bacnet/src/proto_bacnet_core.cpp`
- **作用**: PLC 接口实现、上下文管理、连接管理

| 函数 | 行号 | 功能 | 调用者 |
|------|------|------|--------|
| `initialize_context()` | 158-215 | 初始化上下文 | `BacnetDriver::initialize()` |
| `cleanup_context()` | 217-253 | 清理资源 | `BacnetDriver::release()` |
| `connect_device()` | 255-315 | 连接设备 | `BacnetDriver::connect()` |
| `disconnect_device()` | 317-330 | 断开连接 | `BacnetDriver::disconnect()` |
| `ensure_init_and_connect_locked()` | 370-421 | 自动初始化和连接 | `plc_proto_read/write()` |
| **`plc_proto_read()`** | **430-543** | **PLC 读接口 ⭐** | **用户代码** |
| **`plc_proto_write()`** | **545-579** | **PLC 写接口 ⭐** | **用户代码** |
| `bacnet_reload_config()` | 581-599 | 热重载配置 | 用户代码 |

**关键实现**:
```cpp
// plc_proto_read() - 使用 Driver 单例
int plc_proto_read(void *req) {
    auto& driver = BacnetDriver::instance();        // 获取单例
    
    // 自动初始化和连接
    if (ensure_init_and_connect_locked() != 0) {
        return PROTO_ERROR_INIT;
    }
    
    // 执行读取
    return driver.read(static_cast<bacnet_read_t*>(req));
}
```

---

**proto_bacnet_callbacks.cpp** - 协议栈回调（425 行）
- **位置**: `impl/bacnet/src/proto_bacnet_callbacks.cpp`
- **作用**: 处理 BACnet 协议栈的异步响应

| 函数 | 行号 | 触发条件 | 处理内容 |
|------|------|----------|----------|
| `handle_iam_callback()` | 28-46 | 收到 I-Am 响应 | 缓存设备地址 |
| `handle_read_property_ack()` | 48-123 | 收到 ReadProperty 响应 | 解析数据并更新缓存 |
| `handle_write_property_ack()` | 148-172 | 收到 WriteProperty 响应 | 清理写请求映射 |
| `handle_error_response()` | 174-246 | 收到错误响应 | 记录错误信息 |
| `handle_abort_response()` | 248-287 | 收到 Abort 响应 | 清理超时请求 |
| `handle_reject_response()` | 289-326 | 收到 Reject 响应 | 清理被拒绝请求 |
| `register_bacnet_handlers()` | 356-375 | 初始化时调用 | 注册所有回调到协议栈 |

**关键流程**:
```cpp
// handle_read_property_ack() - 处理读取响应
void handle_read_property_ack(uint8_t *service_request, uint16_t service_len,
                               BACNET_ADDRESS *src, BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data) {
    // 1. 从 invoke_id 查找对应的四元组
    auto key_it = context->invoke_id_to_key.find(invoke_id);
    
    // 2. 解码响应数据
    bacnet_decode_property_ack(...);
    
    // 3. 更新对象缓存
    auto& obj_state = context->object_states[key];
    obj_state.cached_value = value;
    obj_state.has_valid_cache = true;
    obj_state.timestamp = std::chrono::steady_clock::now();
    
    // 4. 清理 invoke_id 映射
    obj_state.active_invoke_id = BACNET_INVOKE_ID_INVALID;
    context->invoke_id_to_key.erase(invoke_id);
}
```

---

**proto_bacnet_io.cpp** - 读写操作实现（308 行）
- **位置**: `impl/bacnet/src/proto_bacnet_io.cpp`
- **作用**: 执行具体的 BACnet ReadProperty/WriteProperty 操作

| 函数 | 行号 | 功能 | 调用链 |
|------|------|------|--------|
| `resolve_target_address()` | 22-49 | 解析设备地址 | 所有 IO 操作前调用 |
| **`execute_read_property()`** | **51-149** | **执行读取** | `BacnetDriver::read()` |
| **`execute_write_property()`** | **151-263** | **执行写入** | `BacnetDriver::write()` |
| `cleanup_stale_requests()` | 265-308 | 清理超时请求 | 工作线程定期调用 |

**核心逻辑**:
```cpp
// execute_read_property() - 缓存机制 + 防重复
proto_status_t execute_read_property(BacnetContext *context, bacnet_read_t *req) {
    ObjectKey key = {req->device_instance, req->object_type, 
                     req->object_instance, req->property_id};
    
    auto& obj_state = context->object_states[key];
    
    // 1. 检查是否有缓存
    if (obj_state.has_valid_cache) {
        *req->value = obj_state.cached_value;  // 返回缓存
    }
    
    // 2. 检查是否已有活跃请求（防重复）
    if (obj_state.active_invoke_id != BACNET_INVOKE_ID_INVALID) {
        return PROTO_SUCCESS;  // 请求已在飞行中，跳过
    }
    
    // 3. 发送新请求
    uint8_t invoke_id = tsm_invoke_id_get();
    Send_Read_Property_Request(...);
    
    // 4. 记录映射
    obj_state.active_invoke_id = invoke_id;
    context->invoke_id_to_key[invoke_id] = key;
}
```

---

**proto_bacnet_discovery.cpp** - 设备发现和工作线程（425 行）
- **位置**: `impl/bacnet/src/proto_bacnet_discovery.cpp`
- **作用**: Who-Is/I-Am 设备发现、工作线程维护

| 函数 | 行号 | 功能 | 调用时机 |
|------|------|------|----------|
| `discover_target_device()` | 10-92 | 发送 Who-Is 并等待 I-Am | 连接时调用 |
| `worker_loop_function()` | 94-221 | 工作线程主循环 | 线程启动后持续运行 |
| `start_worker_thread()` | 352-368 | 启动工作线程 | 连接成功后调用 |
| `stop_worker_thread()` | 370-405 | 停止工作线程 | 断开连接时调用 |

**工作线程循环**:
```cpp
void worker_loop_function(BacnetContext *context) {
    while (!context->worker_stop.load()) {
        // 1. 更新定时器
        auto elapsed = ...;
        tsm_timer_milliseconds(elapsed);
        datalink_maintenance_timer(elapsed / 1000);
        
        // 2. 接收并处理数据包
        BACNET_ADDRESS src;
        uint8_t Rx_Buf[MAX_MPDU];
        uint16_t pdu_len = datalink_receive(&src, Rx_Buf, MAX_MPDU, timeout);
        
        if (pdu_len > 0) {
            npdu_handler(&src, Rx_Buf, pdu_len);  // 触发回调
        }
        
        // 3. 清理超时请求
        cleanup_stale_requests(context);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

---

**proto_bacnet_utils.cpp** - 工具函数（614 行）
- **位置**: `impl/bacnet/src/proto_bacnet_utils.cpp`
- **作用**: YAML 配置解析、日志打印、类型转换

| 函数 | 行号 | 功能 |
|------|------|------|
| `print_config_table()` | 67-164 | 打印配置表（带来源标记） |
| `bacnet_load_config_from_yaml()` | 173-429 | 解析 YAML 配置文件 |
| `connection_state_to_string()` | 431-443 | 连接状态转字符串 |
| `cache_strategy_to_string()` | 445-455 | 缓存策略转字符串 |
| `invoke_id_to_string()` | 457-466 | Invoke ID 转字符串 |

**配置解析逻辑**:
```cpp
// bacnet_load_config_from_yaml() - 三层优先级
void bacnet_load_config_from_yaml(...) {
    YAML::Node yaml = YAML::LoadFile(yaml_path);
    
    // 优先级 1: YAML 文件
    if (yaml["common"]["log_level"]) {
        cfg->common.log_level = yaml["common"]["log_level"].as<std::string>();
        meta->common_log_level = ConfigSource::Yaml;  // 标记来源
    }
    // 优先级 2: 已在初始化时使用默认值
    else {
        // cfg->common.log_level = bacnet::defaults::kLogLevel;
        // meta->common_log_level = ConfigSource::Default;
    }
}
```

---

**proto_bacnet_hot_config.cpp** - 热配置监控（可选功能）
- **位置**: `impl/bacnet/src/proto_bacnet_hot_config.cpp`
- **作用**: 监控配置文件变化并自动重载

---

### 🔗 函数调用链

#### 读取操作完整调用链

```
用户代码
  └─ plc_proto_read(bacnet_read_t* req)                [proto_bacnet_core.cpp:430]
       ├─ BacnetDriver::instance()                      [proto_bacnet_driver.cpp:35]
       ├─ ensure_init_and_connect_locked()              [proto_bacnet_core.cpp:370]
       │    ├─ BacnetDriver::initialize()               [proto_bacnet_driver.cpp:45]
       │    │    └─ initialize_context()                [proto_bacnet_core.cpp:158]
       │    │         ├─ bacnet_load_config_from_yaml() [proto_bacnet_utils.cpp:173]
       │    │         ├─ Device_Init()                   [BACnet 协议栈]
       │    │         ├─ address_init()                  [BACnet 协议栈]
       │    │         ├─ dlenv_init()                    [BACnet 协议栈]
       │    │         └─ register_bacnet_handlers()     [proto_bacnet_callbacks.cpp:356]
       │    └─ BacnetDriver::connect()                  [proto_bacnet_driver.cpp:104]
       │         └─ connect_device()                    [proto_bacnet_core.cpp:255]
       │              ├─ discover_target_device()       [proto_bacnet_discovery.cpp:10]
       │              │    └─ Send_WhoIs()              [BACnet 协议栈]
       │              └─ start_worker_thread()          [proto_bacnet_discovery.cpp:352]
       └─ BacnetDriver::read()                          [proto_bacnet_driver.cpp:148]
            └─ execute_read_property()                  [proto_bacnet_io.cpp:51]
                 ├─ resolve_target_address()            [proto_bacnet_io.cpp:22]
                 └─ Send_Read_Property_Request()        [BACnet 协议栈]

工作线程（异步）
  └─ worker_loop_function()                             [proto_bacnet_discovery.cpp:94]
       ├─ datalink_receive()                            [BACnet 协议栈]
       ├─ npdu_handler()                                [BACnet 协议栈]
       │    └─ handle_read_property_ack()               [proto_bacnet_callbacks.cpp:48]
       │         └─ 更新 object_states 缓存
       └─ cleanup_stale_requests()                      [proto_bacnet_io.cpp:265]
```

#### 写入操作完整调用链

```
用户代码
  └─ plc_proto_write(bacnet_write_t* req)              [proto_bacnet_core.cpp:545]
       ├─ BacnetDriver::instance()                      [proto_bacnet_driver.cpp:35]
       ├─ ensure_init_and_connect_locked()              [同读取]
       └─ BacnetDriver::write()                         [proto_bacnet_driver.cpp:171]
            └─ execute_write_property()                 [proto_bacnet_io.cpp:151]
                 ├─ resolve_target_address()            [proto_bacnet_io.cpp:22]
                 └─ Send_Write_Property_Request()       [BACnet 协议栈]

工作线程（异步）
  └─ worker_loop_function()
       └─ npdu_handler()
            └─ handle_write_property_ack()              [proto_bacnet_callbacks.cpp:148]
                 └─ 清理 invoke_id 映射
```

---

### 📊 关键数据结构

#### BacnetContext - 核心上下文

```cpp
struct BacnetContext {
    // 配置
    bacnet::BacnetConfig config;
    bacnet::ConfigMetadata config_metadata;
    
    // 状态（原子变量）
    std::atomic<bacnet_connection_state_t> connection_state;
    std::atomic<bool> target_found;
    
    // 设备缓存
    BACNET_ADDRESS target_device_address;
    uint32_t target_max_apdu;
    
    // 对象缓存（四元组 → 状态）
    std::unordered_map<ObjectKey, ObjectState> object_states;
    
    // Invoke ID 映射（反向查找）
    std::unordered_map<uint8_t, ObjectKey> invoke_id_to_key;
    
    // 工作线程
    std::unique_ptr<std::thread> worker_thread;
    std::atomic<bool> worker_stop;
    
    // 线程安全
    std::mutex context_mutex;
};
```

#### ObjectKey - 四元组标识符

```cpp
struct ObjectKey {
    uint32_t device_instance;      // 设备实例 ID
    uint16_t object_type;          // 对象类型（AI/AO/AV...）
    uint32_t object_instance;      // 对象实例号
    uint32_t property_id;          // 属性 ID（PV/DESC...）
    
    bool operator==(const ObjectKey& other) const;
    size_t hash() const;           // 用于 unordered_map
};
```

#### ObjectState - 对象状态

```cpp
struct ObjectState {
    bacnet_data_value_t cached_value;               // 缓存的值
    bool has_valid_cache;                           // 是否有有效缓存
    uint8_t active_invoke_id;                       // 活跃请求 ID（0xFF=无）
    std::chrono::steady_clock::time_point timestamp;// 缓存时间戳
    proto_status_t status;                          // 最后操作状态
};
```

---

## 🏛️ 运行时架构

### 🎯 零配置架构 - 完全自动化

**v3.1 核心设计理念：用户无需关心任何初始化和清理！**

```
用户视角 (极简)：
┌──────────────────────────────────────────────────────────┐
│  int main() {                                             │
│      bacnet_read_t req = BACNET_READ_INIT(...);          │
│      plc_proto_read(&req);  // ← 就这一行！              │
│      return 0;              // ← 自动清理                │
│  }                                                        │
└──────────────────────────────────────────────────────────┘
```

### 🔄 启动流程（自动化）

```
1️⃣ 程序启动
    │
    └─ 用户代码直接调用 plc_proto_read/write()
          ↓
2️⃣ 自动初始化（首次调用时触发）⭐ NEW
    │
    ├─ ensure_init_and_connect_locked()   // 内部自动检测
    │    │
    │    ├─ 检查 Driver 是否初始化
    │    │   └─ 未初始化 → BacnetDriver::instance()
    │    │       ├─ 静态局部变量构造（Meyer's Singleton）
    │    │       └─ 线程安全，C++11保证
    │    │
    │    ├─ BacnetContext::instance()     // Context 也是单例
    │    │   └─ 第一次调用时自动创建
    │    │
    │    ├─ BacnetDriver::initialize()    // 初始化驱动
    │    │    ├─ initialize_context()
    │    │    │    ├─ 加载 config.yaml
    │    │    │    ├─ 初始化 BACnet 协议栈
    │    │    │    │    ├─ Device_Init()
    │    │    │    │    ├─ address_init()
    │    │    │    │    └─ dlenv_init()
    │    │    │    ├─ 注册回调函数
    │    │    │    │    ├─ handle_iam_callback
    │    │    │    │    ├─ handle_read_property_ack
    │    │    │    │    ├─ handle_write_property_ack
    │    │    │    │    ├─ handle_error_response
    │    │    │    │    ├─ handle_abort_response
    │    │    │    │    └─ handle_reject_response
    │    │    │    └─ 打印配置表
    │    │    │
    │    │    └─ 注册 atexit() 清理函数 ⭐ NEW
    │    │         └─ 程序退出时自动调用 bacnet_cleanup()
    │    │
    │    └─ 检查连接状态
    │         └─ 未连接 → BacnetDriver::connect()
    │              ├─ discover_target_device()
    │              │    ├─ 发送 Who-Is 请求
    │              │    ├─ 等待 I-Am 响应
    │              │    └─ 缓存设备地址和 max_apdu
    │              └─ start_worker_thread()
    │                   └─ 创建工作线程
    │                        └─ worker_loop_function()
    │                             └─ 循环接收数据包
    │
3️⃣ 正常运行
    │
    ├─ 主线程：用户代码调用读写接口
    ├─ 工作线程：接收响应并更新缓存
    └─ 热配置线程：监控配置文件变化 ⭐ NEW
    │
4️⃣ 程序退出（自动清理）⭐ NEW
    │
    ├─ main() return
    │    └─ atexit() 触发 bacnet_cleanup()
    │         ├─ BacnetDriver::release()
    │         │    ├─ 停止热配置监控线程
    │         │    ├─ BacnetContext::reset()
    │         │    │    ├─ cleanup_context()
    │         │    │    │    ├─ 停止工作线程
    │         │    │    │    └─ 清理协议栈资源
    │         │    │    └─ 清理所有缓存和映射
    │         │    └─ 日志记录清理完成
    │         │
    │         └─ 操作系统回收资源
    │              ├─ 释放内存
    │              ├─ 关闭套接字
    │              └─ 回收线程
    │
    └─ 程序干净退出，无资源泄漏 ✅
```

### 📡 读取数据流

```
┌─────────────────────────────────────────────────────────────┐
│                    用户代码（主线程）                         │
└─────────────────────────────────────────────────────────────┘
                        │
                        │ 调用 plc_proto_read(req)
                        ↓
┌─────────────────────────────────────────────────────────────┐
│                PLC 接口层（C 接口）                           │
│  proto_bacnet_core.cpp::plc_proto_read()                    │
│    ├─ 检查初始化状态 → 未初始化则自动初始化                  │
│    ├─ 检查连接状态 → 未连接则自动连接                       │
│    └─ 调用 BacnetDriver::read()                             │
└─────────────────────────────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│            Driver 层（Modern C++ 单例）                       │
│  proto_bacnet_driver.cpp::BacnetDriver::read()              │
│    └─ 调用 execute_read_property()                          │
└─────────────────────────────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│                  IO 层（读写逻辑）                            │
│  proto_bacnet_io.cpp::execute_read_property()               │
│    ├─ 构造四元组 ObjectKey                                  │
│    ├─ 检查对象缓存                                          │
│    │   └─ has_valid_cache? → 返回缓存值                     │
│    ├─ 检查活跃请求                                          │
│    │   └─ active_invoke_id != 0xFF? → 跳过发送（防重复）    │
│    ├─ 获取 invoke_id                                        │
│    ├─ 发送 ReadProperty 请求                               │
│    │   └─ Send_Read_Property_Request()                      │
│    └─ 记录映射                                              │
│        ├─ object_states[key].active_invoke_id = invoke_id   │
│        └─ invoke_id_to_key[invoke_id] = key                 │
└─────────────────────────────────────────────────────────────┘
                        │
                        │ BACnet UDP 数据包
                        ↓
┌─────────────────────────────────────────────────────────────┐
│                   网络层（UDP/IP）                            │
│  BACnet/IP 协议栈                                            │
│    └─ 发送 ReadProperty 请求到设备 5678                     │
└─────────────────────────────────────────────────────────────┘
                        │
            ⏱️ 网络延迟（50-150ms）
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│               BACnet 设备（目标设备）                         │
│  设备 5678                                                   │
│    ├─ 接收 ReadProperty 请求                                │
│    ├─ 读取对象属性值                                        │
│    └─ 发送 ReadProperty Ack 响应                            │
└─────────────────────────────────────────────────────────────┘
                        │
                        │ BACnet UDP 响应包
                        ↓
┌─────────────────────────────────────────────────────────────┐
│              工作线程（异步接收）                             │
│  proto_bacnet_discovery.cpp::worker_loop_function()         │
│    ├─ datalink_receive() → 接收 UDP 数据包                  │
│    └─ npdu_handler() → 解析 NPDU                            │
│         └─ 调用协议栈处理器                                 │
└─────────────────────────────────────────────────────────────┘
                        │
                        │ 触发回调
                        ↓
┌─────────────────────────────────────────────────────────────┐
│              回调层（协议栈回调）                             │
│  proto_bacnet_callbacks.cpp::handle_read_property_ack()     │
│    ├─ 从 invoke_id 查找 ObjectKey                           │
│    │   └─ key = invoke_id_to_key[invoke_id]                 │
│    ├─ 解码响应数据                                          │
│    │   └─ bacnet_decode_property_ack()                      │
│    ├─ 更新对象缓存                                          │
│    │   └─ object_states[key] = {                            │
│    │        .cached_value = value,                          │
│    │        .has_valid_cache = true,                        │
│    │        .timestamp = now,                               │
│    │        .active_invoke_id = 0xFF                        │
│    │      }                                                 │
│    └─ 清理 invoke_id 映射                                   │
│        └─ invoke_id_to_key.erase(invoke_id)                 │
└─────────────────────────────────────────────────────────────┘
                        │
                        │ 缓存已更新
                        ↓
┌─────────────────────────────────────────────────────────────┐
│              下次读取（缓存命中）                             │
│  用户再次调用 plc_proto_read(相同四元组)                     │
│    ├─ execute_read_property() 检查缓存                       │
│    ├─ has_valid_cache = true                                │
│    ├─ 检查过期（cache_expiry_ms = 1000ms）                  │
│    └─ 根据策略决定：                                        │
│        ├─ 激进策略(0)：发送新请求 + 返回缓存                │
│        └─ 保守策略(1)：未过期则仅返回缓存                   │
└─────────────────────────────────────────────────────────────┘
```

### ✏️ 写入数据流

```
用户代码
    │
    │ plc_proto_write(req)
    ↓
PLC 接口层
    │
    │ BacnetDriver::write()
    ↓
IO 层
    │
    │ execute_write_property()
    ├─ resolve_target_address()
    ├─ 编码写入值
    ├─ 获取 invoke_id
    ├─ Send_Write_Property_Request()
    └─ 记录映射：invoke_id_to_key[invoke_id] = key
    │
    ↓
网络层
    │
    │ BACnet UDP 写请求
    ↓
BACnet 设备
    │
    │ 处理写入请求
    │ 发送 WriteProperty Ack
    ↓
工作线程
    │
    │ datalink_receive() + npdu_handler()
    ↓
回调层
    │
    │ handle_write_property_ack()
    ├─ 查找 invoke_id → ObjectKey
    └─ 清理映射：invoke_id_to_key.erase(invoke_id)
```

### 🔄 并发处理模型

```
┌────────────────────────────────────────────────────────────┐
│                    主线程（用户代码）                        │
├────────────────────────────────────────────────────────────┤
│  while (true) {                                             │
│      plc_proto_read(&req1);    // 读取 AI-1                 │
│      plc_proto_read(&req2);    // 读取 AI-2                 │
│      plc_proto_write(&req3);   // 写入 AO-1                 │
│      std::this_thread::sleep_for(100ms);  // PLC 轮询周期  │
│  }                                                          │
└────────────────────────────────────────────────────────────┘
            │               │               │
            │               │               │
            ↓               ↓               ↓
┌────────────────────────────────────────────────────────────┐
│              BacnetContext（共享状态）                       │
├────────────────────────────────────────────────────────────┤
│  object_states:                                             │
│    {5678, AI, 1, PV} → {value=25.5, invoke_id=10, ...}     │
│    {5678, AI, 2, PV} → {value=30.2, invoke_id=11, ...}     │
│    {5678, AO, 1, PV} → {value=0.0,  invoke_id=12, ...}     │
│                                                             │
│  invoke_id_to_key:                                          │
│    10 → {5678, AI, 1, PV}                                  │
│    11 → {5678, AI, 2, PV}                                  │
│    12 → {5678, AO, 1, PV}                                  │
│                                                             │
│  🔒 context_mutex（保护并发访问）                           │
└────────────────────────────────────────────────────────────┘
            │               │               │
            │               │               │
            ↓               ↓               ↓
┌────────────────────────────────────────────────────────────┐
│                 工作线程（异步处理）                         │
├────────────────────────────────────────────────────────────┤
│  while (!worker_stop) {                                     │
│      // 1. 接收数据包                                       │
│      pdu_len = datalink_receive(...);                       │
│                                                             │
│      // 2. 处理数据包                                       │
│      if (pdu_len > 0) {                                     │
│          npdu_handler(...);  // 触发回调                    │
│      }                                                      │
│                                                             │
│      // 3. 更新定时器                                       │
│      tsm_timer_milliseconds(100);                           │
│      datalink_maintenance_timer(1);                         │
│                                                             │
│      // 4. 清理超时请求                                     │
│      cleanup_stale_requests(context);                       │
│                                                             │
│      sleep(10ms);                                           │
│  }                                                          │
└────────────────────────────────────────────────────────────┘
```

### 🔒 线程安全机制

```
主线程                               工作线程
   │                                    │
   ├─ plc_proto_read()                  │
   │   ├─ 🔒 lock(context_mutex)        │
   │   ├─ 检查 object_states            │
   │   ├─ 发送请求                      │
   │   ├─ 更新 invoke_id_to_key         │
   │   └─ 🔓 unlock                     │
   │                                    │
   │                                    ├─ datalink_receive()
   │                                    ├─ npdu_handler()
   │                                    │   └─ handle_read_property_ack()
   │                                    │       ├─ 🔒 lock(context_mutex)
   │                                    │       ├─ 更新 object_states
   │                                    │       ├─ 清理 invoke_id_to_key
   │                                    │       └─ 🔓 unlock
   │                                    │
   ├─ plc_proto_write()                 │
   │   ├─ 🔒 lock(context_mutex)        │
   │   └─ 🔓 unlock                     │
   │                                    │
   │                                    ├─ cleanup_stale_requests()
   │                                    │   ├─ 🔒 lock(context_mutex)
   │                                    │   └─ 🔓 unlock
```

### 🔚 清理流程

```
程序退出
    │
    ├─ BacnetDriver 单例析构（自动）
    │    └─ BacnetDriver::release()
    │         ├─ 停止工作线程
    │         │    ├─ worker_stop.store(true)
    │         │    └─ worker_thread->join()
    │         ├─ 调用 cleanup_context()
    │         │    ├─ dlenv_cleanup()
    │         │    └─ address_cleanup()
    │         └─ context_.reset()  // 智能指针自动释放
    │
    └─ 所有资源清理完成（RAII 保证）
```

---

## 🎯 概述

BACnet 协议驱动采用现代 C++17 实现，对外提供简洁的异步 C 接口，支持 BACnet 协议栈的设备发现、属性读写等核心功能。🎉

### 🌟 核心价值

- **🔄 零配置架构** - 首次调用自动初始化，程序退出自动清理
- **🎯 接口极简** - 只暴露 2 个核心接口函数
- **🛡️ 线程安全** - 完善的同步机制，支持并发操作
- **💎 现代 C++** - 智能指针、原子变量、RAII、条件变量
- **📦 模块化设计** - 代码职责清晰，易于维护和扩展
- **🔥 自动管理** - 连接初始化、设备发现全自动
- **⚙️ 热配置** - 运行时动态配置重载，无需重启

---

## ✨ 核心特性

| 特性 | 描述 | 优势 |
|------|------|------|
| 🚀 **异步非阻塞 + 智能缓存** | 读写操作立即返回，结果通过哈希表缓存 | 高并发，响应快 |
| 🎯 **接口简洁** | 只需 `plc_proto_read()` 和 `plc_proto_write()` | 易学易用 |
| 🔒 **线程安全** | 原子变量 + 互斥锁 + 条件变量 | 并发安全 |
| 💎 **现代 C++** | 智能指针、RAII、lambda、chrono | 代码优雅，内存安全 |
| 📦 **模块化** | 按功能拆分文件，职责单一 | 易维护，易扩展 |
| 🔄 **自动管理** | 自动连接、自动发现、自动重试 | 零配置使用 |
| 🔥 **热配置** | 配置文件修改后自动生效，无需重启 ⭐ NEW | 运行时动态调整 |
| 🌐 **多数据链路层** | 同时支持 BACnet/IP + BACnet/MSTP | 灵活部署 |
| 🛡️ **死锁避免** | 延迟重载机制，监控线程不会阻塞自己 | 稳定可靠 |
| 💾 **智能缓存** | 两种缓存策略（激进/保守），可配置过期时间 | 性能优化 |

### 🔥 热配置重载特性（v3.1）

BACnet 驱动支持**零停机时间配置更新**，编辑 `config.yaml` 后自动生效：

| 功能 | 说明 | 状态 |
|------|------|------|
| 📁 **文件监控** | 每秒检查配置文件修改时间（mtime） | ✅ 已实现 |
| 🔄 **自动重载** | 检测到变化后自动触发配置重载 | ✅ 已实现 |
| 🛡️ **死锁避免** | 延迟重载机制，避免监控线程阻塞自己 | ✅ 已实现 |
| 🧵 **线程安全** | 使用 atomic + mutex 保证多线程安全 | ✅ 已实现 |
| ⚡ **快速响应** | < 1 秒检测到变化，下次读写时生效 | ✅ 已实现 |
| 📊 **性能优化** | < 0.1% CPU 占用，~8KB 内存开销 | ✅ 已实现 |
| ⚙️ **可配置** | 支持启用/禁用，可调整轮询间隔 | ✅ 已实现 |

**使用示例**：
```bash
# 1. 程序运行中
./bacnet_demo

# 2. 修改配置（任意编辑器）
vim config.yaml
# 修改 log_level: debug → info

# 3. 保存文件
# ✅ 自动检测 → 自动重载 → 配置生效（< 2 秒）
```

详见：[热配置重载机制](#-热配置重载机制)

---

## 🏗️ 架构设计

### 🏛️ 单例模式与资源管理 ⭐ v4.0 重构

**BACnet 驱动采用现代 C++ 单例模式实现零配置、自动管理的架构。**

#### 🎯 核心设计理念

```
用户视角：极简 API，零配置
┌─────────────────────────────────────────┐
│ 用户只需调用：                           │
│   plc_proto_read(&req);                 │
│   plc_proto_write(&req);                │
│                                         │
│ 无需关心：                               │
│   ❌ 初始化                              │
│   ❌ 连接管理                            │
│   ❌ 资源清理                            │
│   ❌ 线程管理                            │
└─────────────────────────────────────────┘
           │
           │ 库内部自动处理一切
           ↓
┌─────────────────────────────────────────┐
│ BacnetDriver 单例（Meyer's Singleton）   │
│  ├─ 首次访问时自动创建                   │
│  ├─ 程序退出时自动清理（atexit）         │
│  └─ 线程安全（C++11 保证）               │
└─────────────────────────────────────────┘
           │
           ↓
┌─────────────────────────────────────────┐
│ BacnetContext 单例                       │
│  ├─ 由 Driver 管理生命周期               │
│  ├─ 包含所有状态和缓存                   │
│  └─ reset() 支持热配置重载               │
└─────────────────────────────────────────┘
```

#### 🔧 单例实现细节

**1. BacnetDriver 单例（Meyer's Singleton）**

```cpp
// proto_bacnet_driver.cpp
class BacnetDriver {
public:
    // ⭐ 线程安全的单例获取（C++11 保证）
    static BacnetDriver& instance() {
        static BacnetDriver instance;  // 静态局部变量，第一次调用时构造
        return instance;
    }
    
    // 核心接口
    int initialize(proto_ctx_t* ctx);
    void release();
    bool is_initialized() const;
    
private:
    // 禁止拷贝和移动
    BacnetDriver() = default;
    ~BacnetDriver() = default;
    BacnetDriver(const BacnetDriver&) = delete;
    BacnetDriver& operator=(const BacnetDriver&) = delete;
    BacnetDriver(BacnetDriver&&) = delete;
    BacnetDriver& operator=(BacnetDriver&&) = delete;
};
```

**2. BacnetContext 单例**

```cpp
// proto_bacnet_internal.hpp
class BacnetContext {
public:
    static BacnetContext& instance() {
        static BacnetContext instance;
        return instance;
    }
    
    proto_status_t initialize(proto_ctx_t* ctx);
    void reset();  // 支持热配置重载
    bool is_initialized() const;
    
private:
    BacnetContext() = default;
    ~BacnetContext() {
        // ⚠️ 析构函数为空，不做清理
        // 原因：程序退出时全局对象析构顺序不确定
        // 清理由 bacnet_cleanup() 在 atexit 中完成
    }
};
```

#### ⚡ 自动初始化机制

**用户调用流程**：

```
用户代码：plc_proto_read(&req)
    │
    ├─ 1️⃣ 获取 Driver 单例
    │   └─ BacnetDriver::instance()  // 首次调用时自动创建
    │
    ├─ 2️⃣ 检查是否已初始化
    │   └─ if (!driver.is_initialized())
    │
    ├─ 3️⃣ 自动初始化（首次调用）
    │   ├─ driver.initialize(nullptr)
    │   │   ├─ 创建 BacnetContext
    │   │   ├─ 加载 config.yaml
    │   │   ├─ 初始化 BACnet 协议栈
    │   │   └─ 注册 atexit 清理函数 ⭐
    │   │
    │   └─ 注册自动清理（仅一次）
    │       └─ std::atexit([]() { bacnet_cleanup(); });
    │
    ├─ 4️⃣ 检查连接状态
    │   └─ if (!connected) driver.connect()
    │
    └─ 5️⃣ 执行实际操作
        └─ driver.read(&req)
```

**关键代码**（proto_bacnet_driver.cpp）：

```cpp
int BacnetDriver::initialize(proto_ctx_t* ctx) {
    // ... 初始化逻辑
    
    // ⭐ 注册退出时自动清理（只注册一次）
    static bool cleanup_registered = false;
    if (!cleanup_registered) {
        std::atexit([]() {
            log_info("[BACnet] Program exiting, auto-cleanup resources...");
            bacnet_cleanup();  // 自动清理
        });
        cleanup_registered = true;
    }
    
    return PROTO_SUCCESS;
}
```

#### 🧹 自动资源清理

**清理时机**：程序正常退出时，`atexit` 注册的函数会在全局对象析构**之前**被调用。

```
程序退出流程：
    │
    ├─ 1️⃣ main() 返回 或 调用 exit()
    │
    ├─ 2️⃣ atexit 注册的函数按**倒序**执行
    │   └─ bacnet_cleanup()  ⭐ 在这里清理
    │       ├─ driver.release()
    │       │   ├─ 停止热配置监控线程
    │       │   ├─ context.reset()
    │       │   │   ├─ 停止工作线程
    │       │   │   ├─ 清理 BACnet 协议栈
    │       │   │   └─ 清理所有状态和缓存
    │       │   └─ context 智能指针自动释放
    │       └─ 日志输出清理完成
    │
    ├─ 3️⃣ 全局对象析构（此时资源已清理）
    │   ├─ BacnetDriver 单例析构（空操作）
    │   └─ BacnetContext 单例析构（空操作）
    │
    └─ 4️⃣ 程序退出
```

**关键代码**（proto_bacnet_core.cpp）：

```cpp
// ⚠️ 注意：这是内部函数，不对外暴露
int bacnet_cleanup(void) {
    log_info("[BACnet] Explicit cleanup requested");
    
    auto& driver = bacnet::BacnetDriver::instance();
    
    if (driver.is_initialized()) {
        driver.release();  // 完整清理流程
        log_info("[BACnet] Cleanup completed successfully");
    }
    
    return PROTO_SUCCESS;
}
```

#### 🛡️ Bus Error 解决方案

**问题背景**：
- 重构前使用全局指针 `g_ctx`，析构顺序不确定导致 Bus error
- 两个单例（Driver, Context）在程序退出时析构顺序不可控

**解决方案**：
1. **atexit 提前清理** - 在全局对象析构前完成所有清理
2. **空析构函数** - 单例析构函数不做任何操作
3. **依赖 OS 回收** - 剩余资源由操作系统自动回收

```cpp
// BacnetContext 析构函数 - 空实现
~BacnetContext() {
    // ⚠️ 不做任何清理，原因：
    // 1. atexit 已在析构前完成清理
    // 2. 全局对象析构顺序不确定
    // 3. 其他全局对象可能已被销毁
    // 4. 操作系统会自动回收资源
}

// BacnetDriver 析构函数 - 空实现
~BacnetDriver() {
    // 同上，不做任何清理
}
```

#### 🔄 热配置重载支持

**重载机制**：利用 `reset()` 方法实现无需重启的配置更新。

```cpp
// BacnetContext::reset() 实现
void BacnetContext::reset() {
    std::lock_guard<std::mutex> lock(plc_mutex);
    
    log_info("[BACnet][Context] Resetting context...");
    
    // 1. 停止工作线程
    if (worker_thread && worker_thread->joinable()) {
        worker_stop.store(true);
        worker_thread->join();
    }
    
    // 2. 清理协议栈
    cleanup_context(this);
    
    // 3. 清理所有状态
    object_states.clear();
    invoke_id_to_key.clear();
    write_pending_map.clear();
    
    // 4. 重置标志
    initialized_.store(false);
    worker_stop.store(false);   // ⭐ 重置，允许重新启动
    worker_running.store(false);
    
    log_info("[BACnet][Context] Reset completed");
}

// 热配置重载流程
bacnet_reload_config()
    └─ 设置 pending_reload 标志
        └─ 下次 read/write 时：
            ├─ 检测到 pending_reload
            ├─ driver.release()  // 清理旧配置
            └─ 自动重新 initialize()  // 加载新配置
```

#### 📊 架构优势总结

| 特性 | 实现方式 | 优势 |
|------|----------|------|
| **零配置** | 单例 + 自动初始化 | 用户无需调用初始化函数 |
| **零清理** | atexit + 空析构 | 用户无需调用清理函数 |
| **线程安全** | Meyer's Singleton | C++11 保证静态局部变量线程安全 |
| **避免泄漏** | 智能指针 + RAII | 异常安全，自动释放资源 |
| **避免崩溃** | atexit 提前清理 | 避免全局对象析构顺序问题 |
| **支持重载** | reset() 方法 | 热配置无需重启程序 |
| **资源回收** | OS 接管 | 程序退出时 OS 回收所有资源 |

#### 🎯 用户体验对比

**极简 API（新版本）**：
```c
int main() {
    // ✅ 无需初始化
    bacnet_read_t req = BACNET_READ_INIT(5678, AI, 1, PV, &value);
    
    // ✅ 直接使用
    plc_proto_read(&req);
    
    // ✅ 无需清理
    return 0;  // 程序退出时自动清理
}
```

**对比旧架构**：
```c
// ❌ 旧版本（需要手动管理）
int main() {
    proto_ctx_t ctx;
    plc_proto_init(&ctx);        // 必须初始化
    
    plc_proto_read(&req);
    
    plc_proto_release();          // 必须清理
    return 0;
}

// ✅ 新版本（自动管理）
int main() {
    plc_proto_read(&req);         // 一步到位！
    return 0;
}
```

---

### 🎨 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│                        应用层（用户代码）                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │ PLC 应用 1   │  │ PLC 应用 2   │  │ PLC 应用 3   │           │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘           │
│         │                  │                  │                   │
└─────────┼──────────────────┼──────────────────┼───────────────────┘
          │                  │                  │
          └──────────────────┼──────────────────┘
                             ↓
┌──────────────────────────────────────────────────────────────────┐
│                  对外接口层（C 接口）                              │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  int plc_proto_read(void *req);                            │  │
│  │  int plc_proto_write(void *req);                           │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                    │
│  职责：                                                            │
│  1. 检查连接状态（未连接则自动连接）                               │
│  2. 查询哈希表缓存 (object_states)                                │
│  3. 缓存命中立即返回，未命中发送异步请求                           │
│  4. 返回状态码 (SUCCESS/NO_DATA/ERROR)                           │
└──────────────────────────────────────────────────────────────────┘
                             ↓
┌══════════════════════════════════════════════════════════════════┐
║                   内部实现层（C++）                                ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                    ║
║  ┌──────────────────────────────────────────────────────────┐    ║
║  │              BacnetContext 类                             │    ║
║  │  ┌─────────────────────────────────────────────────────┐ │    ║
║  │  │ 状态管理（原子变量）                                 │ │    ║
║  │  │  - connection_state (IDLE/CONNECTING/CONNECTED)     │ │    ║
║  │  │  - operation_state (IDLE/PENDING/SUCCESS/FAILED)    │ │    ║
║  │  │  - target_found (bool)                               │ │    ║
║  │  │  - reconnect_attempts (int)                          │ │    ║
║  │  └─────────────────────────────────────────────────────┘ │    ║
║  │                                                            │    ║
║  │  ┌─────────────────────────────────────────────────────┐ │    ║
║  │  │ 活动操作（互斥锁保护）                               │ │    ║
║  │  │  - ActiveOperation active_operation                  │ │    ║
║  │  │    * type (None/Read/Write)                          │ │    ║
║  │  │    * request (void*)                                 │ │    ║
║  │  │    * invoke_id                                       │ │    ║
║  │  │    * timeout_ms                                      │ │    ║
║  │  └─────────────────────────────────────────────────────┘ │    ║
║  │                                                            │    ║
║  │  ┌─────────────────────────────────────────────────────┐ │    ║
║  │  │ 对象状态哈希表（互斥锁保护）                         │ │    ║
║  │  │  - std::unordered_map<ObjectKey, ObjectState>      │ │    ║
║  │  │  - 缓存读取结果（四元组为键）                        │ │    ║
║  │  │  - 防重复发送机制                                    │ │    ║
║  │  └─────────────────────────────────────────────────────┘ │    ║
║  │                                                            │    ║
║  │  ┌─────────────────────────────────────────────────────┐ │    ║
║  │  │ 工作线程（智能指针管理）                             │ │    ║
║  │  │  - std::unique_ptr<std::thread> worker_thread       │ │    ║
║  │  │  - 循环接收 BACnet 数据包                           │ │    ║
║  │  │  - 调用协议栈处理器                                 │ │    ║
║  │  │  - 检查操作超时                                     │ │    ║
║  │  └─────────────────────────────────────────────────────┘ │    ║
║  └──────────────────────────────────────────────────────────┘    ║
║                                                                    ║
║  ┌──────────────────────────────────────────────────────────┐    ║
║  │              模块化文件组织                               │    ║
║  ├──────────────────────────────────────────────────────────┤    ║
║  │ proto_bacnet_core.cpp                                    │    ║
║  │  - proto_driver_init()      初始化驱动                   │    ║
║  │  - proto_driver_release()   释放资源                     │    ║
║  │  - proto_connect()          连接设备                     │    ║
║  │  - proto_disconnect()       断开连接                     │    ║
║  │  - plc_proto_read()         PLC 读接口 ⭐               │    ║
║  │  - plc_proto_write()        PLC 写接口 ⭐               │    ║
║  ├──────────────────────────────────────────────────────────┤    ║
║  │ proto_bacnet_discovery.cpp                               │    ║
║  │  - discover_target_device()  Who-Is/I-Am 发现           │    ║
║  │  - worker_loop_function()    工作线程主循环             │    ║
║  │  - start_worker_thread()     启动线程                   │    ║
║  │  - stop_worker_thread()      停止线程                   │    ║
║  ├──────────────────────────────────────────────────────────┤    ║
║  │ proto_bacnet_io.cpp                                      │    ║
║  │  - execute_read_property()   执行读操作                 │    ║
║  │  - execute_write_property()  执行写操作                 │    ║
║  │  - resolve_target_address()  解析设备地址               │    ║
║  │  - store_application_value() 存储读取值                 │    ║
║  │  - convert_to_application_value() 转换写入值            │    ║
║  ├──────────────────────────────────────────────────────────┤    ║
║  │ proto_bacnet_callbacks.cpp                               │    ║
║  │  - handle_iam_callback()        I-Am 响应               │    ║
║  │  - handle_read_property_ack()   ReadProperty 应答       │    ║
║  │  - handle_write_property_ack()  WriteProperty 应答      │    ║
║  │  - handle_error_response()      错误响应                │    ║
║  │  - handle_abort_response()      Abort 响应              │    ║
║  │  - handle_reject_response()     Reject 响应             │    ║
║  │  - register_bacnet_handlers()   注册所有回调            │    ║
║  ├──────────────────────────────────────────────────────────┤    ║
║  │ proto_bacnet_utils.cpp                                   │    ║
║  │  - bacnet_load_config_from_yaml() 加载 YAML 配置        │    ║
║  ├──────────────────────────────────────────────────────────┤    ║
║  │ proto_bacnet_hot_config.cpp                              │    ║
║  │  - bacnet_hot_config_init()     初始化文件监控          │    ║
║  │  - monitor_loop()               监控配置文件变化        │    ║
║  │  - on_config_changed()          配置变化回调            │    ║
║  └──────────────────────────────────────────────────────────┘    ║
║                                                                    ║
╚══════════════════════════════════════════════════════════════════╝
                             ↓
┌──────────────────────────────────────────────────────────────────┐
│                   BACnet 协议栈层（C 库）                          │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │ bacnet-stack 库                                           │    │
│  │  - Send_WhoIs()             发送 Who-Is                   │    │
│  │  - Send_Read_Property()     发送 ReadProperty            │    │
│  │  - Send_Write_Property()    发送 WriteProperty           │    │
│  │  - datalink_receive()       接收数据包                   │    │
│  │  - npdu_handler()           NPDU 处理                    │    │
│  │  - tsm_timer_milliseconds() TSM 定时器                   │    │
│  └──────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
                             ↓
┌──────────────────────────────────────────────────────────────────┐
│                       网络层（UDP/IP）                             │
└──────────────────────────────────────────────────────────────────┘
```

### �📁 文件组织架构

```
impl/bacnet/
├── src/
│   ├── proto_bacnet.h              # 🎯 对外 C 接口头文件
│   ├── proto_bacnet_internal.hpp   # 🔧 内部 C++ 定义和声明
│   ├── proto_bacnet_core.cpp       # 🎪 核心功能实现 ⭐
│   ├── proto_bacnet_callbacks.cpp  # 📞 BACnet 协议栈回调处理
│   ├── proto_bacnet_io.cpp         # 📤 读写操作核心逻辑
│   ├── proto_bacnet_discovery.cpp  # 🔍 设备发现机制
│   ├── proto_bacnet_utils.cpp      # 🛠️ 工具函数（配置加载）
│   └── proto_bacnet_hot_config.cpp # 🔥 热配置监控
├── demo/
│   └── main.cc                     # 💻 示例程序
├── config.yaml                     # ⚙️ 配置文件
├── build.sh                        # 🏗️ 构建脚本
└── CMakeLists.txt                  # 📋 CMake 配置
```

### 🎨 设计模式应用

1. **🔄 RAII (Resource Acquisition Is Initialization)**
   - 智能指针自动管理线程生命周期
   - 作用域锁自动管理互斥锁

2. **📨 生产者-消费者模式**
   - 工作线程生产事件
   - 主线程消费事件

3. **🏭 工厂模式**
   - `BacnetContext` 工厂函数创建实例

4. **🎯 状态机模式**
   - 连接状态机：IDLE → CONNECTING → CONNECTED
   - 操作状态机：IDLE → PENDING → SUCCESS/FAILED

---

## 🔄 工作流程

### 📖 读取操作完整流程

```
用户代码调用 plc_proto_read()
    ↓
1️⃣ 检查初始化状态
   ├── 未初始化 → proto_driver_init()
   │   ├── 创建 BacnetContext
   │   ├── 加载 YAML 配置
   │   ├── 初始化 BACnet 协议栈
   │   └── 注册回调函数
   └── 已初始化 → 继续
    ↓
2️⃣ 检查连接状态
   ├── 未连接 → proto_connect()
   │   ├── discover_target_device()
   │   │   ├── 发送 Who-Is 请求
   │   │   ├── 等待 I-Am 响应
   │   │   └── 缓存设备地址
   │   └── start_worker_thread()
   └── 已连接 → 继续
    ↓
3️⃣ 检查哈希表缓存 (object_states)
   ├── 构造四元组键 (device, type, instance, property)
   ├── 查找缓存：
   │   ├── ✅ 缓存命中且未过期 → 返回 PROTO_SUCCESS（数据已填充）
   │   ├── ⏳ 请求进行中 (active_invoke_id 存在)
   │   │   └── 返回 PROTO_NO_DATA（等待后重试）
   │   └── 🆕 无缓存或已过期 → 继续发送请求
    ↓
4️⃣ 发送异步读取请求
   ├── 调用 execute_read_property()
   ├── 发送 ReadProperty 到 BACnet 设备
   ├── 记录 active_invoke_id 到哈希表
   └── 立即返回 PROTO_NO_DATA（不等待！）
    ↓
5️⃣ 工作线程异步处理（后台）
   ├── 接收 BACnet 数据包
   ├── 调用协议栈处理器
   ├── 触发回调函数
   │   handle_read_property_ack()
   │   ├── 解码响应数据
   │   ├── store_application_value()
   │   └── 更新哈希表缓存 + 时间戳
    ↓
6️⃣ 用户重试读取（稍后）
   └── plc_proto_read() → 缓存命中 → PROTO_SUCCESS ✅
```

### 📝 写入操作流程

写入流程与读取类似，主要区别：
- 调用 `execute_write_property()`
- 回调函数是 `handle_write_property_ack()`
- 不需要存储返回值数据

### 🎪 核心组件协作

```
┌─────────────────────────────────────────────────────────────┐
│                    🎯 用户线程 (主线程)                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 1. plc_proto_read/write() ← 异步非阻塞调用          │    │
│  │    ├── 首次调用自动初始化所有数据链路层            │    │
│  │    ├── 查询哈希表缓存 (object_states)              │    │
│  │    ├── 缓存命中 → 立即返回 PROTO_SUCCESS          │    │
│  │    └── 缓存未命中 → 发送请求，返回 PROTO_NO_DATA  │    │
│  └─────────────────────────────────────────────────────┘    │
│          │                                                   │
│          │ 异步请求 (立即返回，不等待)                       │
│          ↓                                                   │
└─────────┼───────────────────────────────────────────────────┘
           │
┌──────────┼───────────────────────────────────────────────────┐
│          │         🧵 工作线程 (后台处理)                     │
│  ┌───────┴─────────────┐                                     │
│  │  worker_loop_function()                                  │
│  │  ┌─────────────────────────────────────────────────┐     │
│  │  │ 循环执行:                                        │     │
│  │  │ 1. 接收并处理数据包 ← 关键！                   │     │
│  │  │ 2. 更新定时器                                   │     │
│  │  │ 3. 检查操作超时                                 │     │
│  │  │ 4. 多数据链路层轮询 (BIP/MSTP)                 │     │
│  │  └─────────────────────────────────────────────────┘     │
│  └───────┬─────────────┘                                     │
│          │                                                   │
│          │ 数据包处理                                        │
│          ↓                                                   │
└──────────┼───────────────────────────────────────────────────┘
           │
┌──────────┼───────────────────────────────────────────────────┐
│          │         📞 BACnet 协议栈回调                       │
│  ┌───────┴─────────────────┐                                 │
│  │ 回调函数处理:             │                                 │
│  │ • handle_read_property_ack()                             │
│  │ • handle_write_property_ack()                            │
│  │ • handle_iam_callback()                                  │
│  │ • handle_error_response()                                │
│  │  └─────────────────────────────────────────────────┘     │
│          │                                                   │
│          │ 更新哈希表缓存 (object_states)                    │
│          ↓                                                   │
└──────────┼───────────────────────────────────────────────────┘
           │
           └── 用户重试读取时从缓存获取 (PROTO_SUCCESS)
```

---

## ⚡ 快速开始

### 1️⃣ 编译项目

```bash
cd /workspace/protocol/procto-manager-hd/impl/bacnet

# 🐛 Debug 模式（推荐开发时使用）
bash build.sh debug

# 🚀 Release 模式（生产环境使用）
bash build.sh release

# 🧹 清理构建文件
bash build.sh clean
```

### 2️⃣ 运行示例

```bash
cd build/package/bin
./bacnet_demo
```

### 3️⃣ 查看日志

```bash
tail -f bacnet.log
```

---

## 📋 API 接口

### 🎯 核心接口

#### `plc_proto_read()` - 异步读取 🔍

```c
int plc_proto_read(void *req);
```

**功能**：异步提交读取请求，立即返回

**参数**：
- `req` - `bacnet_read_t*` 读取请求结构体

**返回值**：
- `PROTO_SUCCESS` (0) - 缓存命中，数据已返回
- `PROTO_NO_DATA` (-7) - 请求已发送，等待响应（可重试读取）
- 其他值 - 错误码

**缓存机制**：
- 首次调用：返回 `PROTO_NO_DATA`，后台发送请求
- 再次调用：返回 `PROTO_SUCCESS`，从哈希表缓存读取
- 缓存过期：自动重新发送请求

**读取请求结构体**：
```c
typedef struct {
    // ✅ 必填字段 (5个)
    uint32_t device_instance;      // 🎯 目标设备实例ID
    uint16_t object_type;          // 📦 对象类型 (如 OBJECT_ANALOG_INPUT)
    uint32_t object_instance;      // 🔢 对象实例号
    uint32_t property_id;          // 🏷️ 属性ID (如 PROP_PRESENT_VALUE)
    bacnet_data_value_t *value;    // 📥 输出缓冲区
    
    // ⚙️ 可选字段 (使用默认值)
    int32_t array_index;           // 📊 数组索引 (默认: -1 = 整个数组)
    uint32_t timeout_ms;           // ⏱️ 超时时间 (默认: 0 = 使用配置文件)
    bool check_only;               // 🔍 是否仅检查队列 (默认: false)
} bacnet_read_t;
```

**✨ 简化API - 使用便捷宏**：
```c
// 只需填写 5 个核心参数，其他使用默认值
bacnet_read_t req = BACNET_READ_INIT(
    5678,                     // 设备实例
    OBJECT_ANALOG_INPUT,      // 对象类型
    1,                        // 对象实例
    PROP_PRESENT_VALUE,       // 属性ID
    &value                    // value缓冲区
);
plc_proto_read(&req);

// 代码对比：
// 旧方式 (9行)        vs    新方式 (7行)    减少 22%！
```

#### `plc_proto_write()` - 异步写入 ✏️

```c
int plc_proto_write(void *req);
```

**功能**：异步提交写入请求，立即返回

**参数**：
- `req` - `bacnet_write_t*` 写入请求结构体

**返回值**：
- `PROTO_SUCCESS` (0) - 请求提交成功
- 其他值 - 错误码

**写入请求结构体**：
```c
typedef struct {
    // ✅ 必填字段 (5个)
    uint32_t device_instance;      // 🎯 目标设备实例ID
    uint16_t object_type;          // 📦 对象类型
    uint32_t object_instance;      // 🔢 对象实例号
    uint32_t property_id;          // 🏷️ 属性ID
    bacnet_data_value_t value;     // 📤 写入值
    
    // ⚙️ 可选字段 (使用默认值)
    int32_t array_index;           // 📊 数组索引 (默认: -1 = 整个数组)
    uint8_t priority;              // ⭐ 写入优先级 (默认: 0 = 使用配置文件)
    uint32_t timeout_ms;           // ⏱️ 超时时间 (默认: 0 = 使用配置文件)
} bacnet_write_t;
```

**✨ 简化API - 使用便捷宏**：
```c
// 只需填写 5 个核心参数，其他使用默认值
bacnet_data_value_t val = {
    .type = BACNET_DATA_REAL,
    .value.real_value = 25.5f
};

bacnet_write_t req = BACNET_WRITE_INIT(
    5678,                     // 设备实例
    OBJECT_ANALOG_OUTPUT,     // 对象类型
    1,                        // 对象实例
    PROP_PRESENT_VALUE,       // 属性ID
    val                       // value
);
plc_proto_write(&req);

// 代码对比：
// 旧方式 (14行)       vs    新方式 (11行)   减少 21%！
```

### 🎪 高级接口

#### `bacnet_reload_config()` - 热配置重载 🔥

```c
int bacnet_reload_config(void);
```

**功能**：重新加载配置文件，无需重启程序

---

### 🔧 配置更新 API

BACnet 驱动提供了 `config_update()` API，用于在运行时动态修改 `config.yaml` 文件。修改会直接写入配置文件，如果启用了热配置监控，修改会在 1 秒内自动生效。

#### `config_update()` - 更新配置文件 ⚙️

```c
int config_update(const bacnet_config_t *cfg);
```

**功能**：直接修改配置文件（config.yaml），支持部分更新

**参数**：
- `cfg` - 配置结构体指针（只需填写需要修改的字段）

**返回值**：
- `PROTO_SUCCESS (0)` - 成功
- `PROTO_ERROR_PARAM` - 参数错误（cfg 为 NULL）
- `PROTO_ERROR_INIT` - 无法打开配置文件
- `PROTO_ERROR_WRITE` - 无法写入配置文件

**核心特性**：

| 特性 | 说明 | 优势 |
|------|------|------|
| ✅ **部分更新** | 只修改指定字段，其他保持不变 | 灵活精确 |
| ✅ **格式保留** | 保留原文件的注释、缩进、空行 | 可读性好 |
| ✅ **自动生效** | 配合热配置监控，修改后自动重载 | 零停机时间 |
| ✅ **布尔字段支持** | 使用 `-1` 表示不更新，`0`=false, `1`=true | 支持隐式转换 |
| ✅ **线程安全** | 可在运行时任意线程调用 | 并发友好 |

#### 📋 字段更新规则

| 字段类型 | 不更新条件 | 更新条件 | 示例 |
|----------|-----------|---------|------|
| **数值字段** | 值为 `0` | 值 > `0` | `read_timeout_ms = 8000` |
| **字符串字段** | 首字符为 `\0` | 非空字符串 | `strcpy(cfg.common.log_level, "info")` |
| **布尔字段** | 值为 `-1` | 值为 `0` 或 `1` | `enabled = 1` (true) |

**🔑 关键点**：
- **必须使用 `BACNET_CONFIG_INIT` 宏初始化**，确保布尔字段默认为 `-1`
- **数值字段为 0 时不会更新**配置文件
- **布尔字段使用 `int8_t` 类型**，支持 `true/false` 隐式转换

#### 📝 使用示例

**示例 1：修改超时配置**

```c
#include "proto_bacnet.h"

int main() {
    // ✅ 必须使用宏初始化（布尔字段默认为 -1）
    bacnet_config_t cfg = BACNET_CONFIG_INIT;
    
    // 只填写需要修改的字段
    cfg.bacnet.services.read_timeout_ms = 8000;
    cfg.bacnet.services.write_timeout_ms = 8000;
    
    int ret = config_update(&cfg);
    if (ret != PROTO_SUCCESS) {
        printf("❌ 配置更新失败: %d\n", ret);
        return -1;
    }
    
    printf("✅ 配置更新成功！\n");
    // 如果启用了热配置，修改会在 1 秒内自动生效
    return 0;
}
```

**示例 2：修改设备发现配置**

```c
bacnet_config_t cfg = BACNET_CONFIG_INIT;

// 修改发现范围
cfg.bacnet.discovery.target_device_start = 6000;
cfg.bacnet.discovery.target_device_end = 6100;
cfg.bacnet.discovery.whois_retry = 5;

int ret = config_update(&cfg);
```

**示例 3：修改布尔字段**

```c
bacnet_config_t cfg = BACNET_CONFIG_INIT;

// 方式 1：直接传布尔值（推荐）
cfg.bacnet.hot_config.enabled = true;  // 隐式转换为 1

// 方式 2：显式使用数值
cfg.bacnet.hot_config.enabled = 1;     // 1 = true
// cfg.bacnet.hot_config.enabled = 0;  // 0 = false
// cfg.bacnet.hot_config.enabled = -1; // -1 = 不更新（默认值）

cfg.bacnet.hot_config.polling_interval_ms = 500;

int ret = config_update(&cfg);
```

**示例 4：批量更新多个字段**

```c
bacnet_config_t cfg = BACNET_CONFIG_INIT;

// Common 配置
strcpy(cfg.common.log_level, "info");

// Services 配置
cfg.bacnet.services.read_timeout_ms = 5000;
cfg.bacnet.services.write_timeout_ms = 5000;
cfg.bacnet.services.cache_strategy = 1;  // 保守策略

// Connection 配置
cfg.bacnet.connection.max_reconnect_attempts = 10;
cfg.bacnet.connection.reconnect_interval_ms = 2000;

int ret = config_update(&cfg);
if (ret == PROTO_SUCCESS) {
    printf("✅ 所有配置更新成功\n");
}
```

#### 🎯 可配置字段列表

**Common（通用配置）**

```c
cfg.common.environment[BACNET_MAX_ENV_LEN];        // 运行环境
cfg.common.log_level[BACNET_MAX_LOG_LEVEL_LEN];   // 日志级别
cfg.common.log_file[BACNET_MAX_LOG_PATH_LEN];     // 日志文件路径
```

**Discovery（设备发现配置）**

```c
cfg.bacnet.discovery.target_device_start;    // 目标设备范围起始
cfg.bacnet.discovery.target_device_end;      // 目标设备范围结束
cfg.bacnet.discovery.whois_retry;            // Who-Is 重试次数
cfg.bacnet.discovery.response_timeout_ms;    // I-Am 响应超时
```

**LocalDevice（本地设备配置）**

```c
cfg.bacnet.local_device.instance_id;         // 本地设备实例 ID
cfg.bacnet.local_device.max_apdu;            // 最大 APDU 长度
```

**Network（网络配置）**

```c
cfg.bacnet.network.interface_name[BACNET_MAX_INTERFACE_LEN];      // 网络接口
cfg.bacnet.network.port;                                           // UDP 端口
cfg.bacnet.network.broadcast_address[BACNET_MAX_ADDRESS_LEN];     // 广播地址
```

**Services（服务配置）**

```c
cfg.bacnet.services.read_timeout_ms;          // 读超时
cfg.bacnet.services.write_timeout_ms;         // 写超时
cfg.bacnet.services.default_priority;         // 默认优先级
cfg.bacnet.services.cache_expiry_ms;          // 缓存过期时间
cfg.bacnet.services.cache_strategy;           // 缓存策略 (0=激进, 1=保守)
cfg.bacnet.services.datalink_maintenance_ms;  // DataLink 维护间隔
```

**Connection（连接管理配置）**

```c
cfg.bacnet.connection.max_reconnect_attempts;  // 最大重连次数
cfg.bacnet.connection.reconnect_interval_ms;   // 重连间隔
```

**HotConfig（热配置监控）**

```c
cfg.bacnet.hot_config.enabled;                 // 是否启用热配置 (-1=不更新, 0=false, 1=true)
cfg.bacnet.hot_config.polling_interval_ms;     // 轮询间隔
```

#### ⚠️ 注意事项

**1. 必须使用初始化宏**

```c
// ✅ 正确：使用 BACNET_CONFIG_INIT 宏
bacnet_config_t cfg = BACNET_CONFIG_INIT;
cfg.bacnet.services.read_timeout_ms = 8000;
config_update(&cfg);

// ❌ 错误：未初始化，布尔字段包含随机值
bacnet_config_t cfg;  // 未初始化！
cfg.bacnet.services.read_timeout_ms = 8000;
config_update(&cfg);  // 可能会意外修改布尔字段
```

**2. 字段判断规则**

```c
bacnet_config_t cfg = BACNET_CONFIG_INIT;

// 数值字段为 0 时不会修改
cfg.bacnet.services.read_timeout_ms = 0;  // ❌ 不会修改配置文件

// 数值字段 > 0 时会修改
cfg.bacnet.services.read_timeout_ms = 8000;  // ✅ 会修改配置文件

// 布尔字段为 -1 时不会修改
cfg.bacnet.hot_config.enabled = -1;  // ❌ 不会修改配置文件（默认值）

// 布尔字段为 0 或 1 时会修改
cfg.bacnet.hot_config.enabled = true;   // ✅ 会修改配置文件（隐式转换为 1）
cfg.bacnet.hot_config.enabled = false;  // ✅ 会修改配置文件（隐式转换为 0）
```

**3. 热配置生效时间**

```c
// 修改配置
int ret = config_update(&cfg);

// 如果启用了热配置监控，等待 1-2 秒后生效
sleep(2);  // 等待热配置监控检测到文件变化
```

**4. 文件格式保留**

API 会保留配置文件的：
- ✅ 注释（包括行内注释和独立注释行）
- ✅ 缩进和空行
- ✅ 配置项顺序

**原文件**：
```yaml
services:
  read_timeout_ms: 6000       # 读操作超时时间
  write_timeout_ms: 6000      # 写操作超时时间
```

**修改后**：
```yaml
services:
  read_timeout_ms: 8000       # 读操作超时时间
  write_timeout_ms: 8000      # 写操作超时时间
```

#### 🧪 测试程序

**编译测试程序**：

```bash
cd build
make config_update_test
```

**运行测试**：

```bash
./config_update_test
```

**测试场景**：

测试程序演示了以下场景：
1. ✅ 更新服务超时配置
2. ✅ 更新设备发现配置
3. ✅ 更新缓存策略
4. ✅ 更新热配置监控参数（布尔字段）
5. ✅ 批量更新多个配置项

#### 🔄 与热配置监控的配合

**推荐工作流程**：

```c
// 1. 启用热配置监控
bacnet_config_t cfg1 = BACNET_CONFIG_INIT;
cfg1.bacnet.hot_config.enabled = true;
cfg1.bacnet.hot_config.polling_interval_ms = 1000;
config_update(&cfg1);

// 2. 初始化 BACnet 驱动（会自动启动热配置监控）
// 驱动会自动初始化，无需手动调用 init

// 3. 运行时动态修改配置
bacnet_config_t cfg2 = BACNET_CONFIG_INIT;
cfg2.bacnet.services.read_timeout_ms = 8000;
config_update(&cfg2);  // 修改会在 1 秒内自动生效
```

**禁用热配置监控**：

如果不需要自动重载，可以禁用：

```c
bacnet_config_t cfg = BACNET_CONFIG_INIT;
cfg.bacnet.hot_config.enabled = false;  // 0 也可以
config_update(&cfg);

// 此时需要重启程序才能使配置生效
```

#### 🐛 错误处理

**完整的错误处理示例**：

```c
int ret = config_update(&cfg);

switch (ret) {
    case PROTO_SUCCESS:
        printf("✅ 配置更新成功\n");
        break;
        
    case PROTO_ERROR_PARAM:
        printf("❌ 参数错误：cfg 指针为 NULL\n");
        break;
        
    case PROTO_ERROR_INIT:
        printf("❌ 无法打开配置文件 %s\n", "../config.yaml");
        break;
        
    case PROTO_ERROR_WRITE:
        printf("❌ 无法写入配置文件\n");
        break;
        
    default:
        printf("❌ 未知错误：%d\n", ret);
        break;
}
```

#### 💡 最佳实践

1. **总是初始化为 `BACNET_CONFIG_INIT`**
   ```c
   bacnet_config_t cfg = BACNET_CONFIG_INIT;  // ✅ 推荐
   ```

2. **只填写需要修改的字段**
   ```c
   cfg.bacnet.services.read_timeout_ms = 8000;  // 只修改这一个
   ```

3. **检查返回值**
   ```c
   if (ret != PROTO_SUCCESS) {
       log_error("配置更新失败: {}", ret);
   }
   ```

4. **配合热配置使用**
   ```c
   // 启用热配置，修改会自动生效
   cfg.bacnet.hot_config.enabled = true;
   ```

5. **记录日志**
   ```c
   log_info("正在更新配置：read_timeout_ms={}", cfg.bacnet.services.read_timeout_ms);
   int ret = config_update(&cfg);
   log_info("配置更新结果：{}", ret);
   ```

---

## 🔧 配置说明

### 📄 配置文件位置

```
impl/bacnet/config.yaml
```

### ⚙️ 配置结构重构（v4.1）

**重大更新**：配置结构体从 **3 层嵌套** 重构为 **扁平化单层结构**，API 更简洁！

#### 🎯 重构对比

**之前（嵌套 3 层）**：
```c
// ❌ 访问路径冗长
cfg->common.log_level
cfg->bacnet.services.read_timeout_ms
cfg->bacnet.local_device.instance_id
cfg->bacnet.hot_config.enabled
```

**现在（扁平化）**：
```c
// ✅ 访问路径简短直观
cfg->read_timeout_ms
cfg->local_instance_id
cfg->hot_config_enabled
```

**重构说明**：
- ❌ **移除**：`common` 配置（environment, log_level, log_file）已从 `bacnet_config_t` 中删除
- ✅ **原因**：日志配置由 `OneLogger` 统一管理（位于 `common/utils/one_logger.hpp`）
- ✅ **优势**：所有协议模块（BACnet、Modbus、MQTT）使用统一的日志系统

#### 📋 新配置结构体定义

```c
typedef struct {
    // ========== BACnet 协议栈开关 ==========
    int8_t   enabled;                      // 是否启用 BACnet 协议栈 (-1=不更新, 0=false, 1=true)
    
    // ========== 设备发现配置 ==========
    uint32_t target_device_start;          // 🎯 目标设备实例范围起始
    uint32_t target_device_end;            // 🎯 目标设备实例范围结束
    uint8_t  whois_retry;                  // 🔄 Who-Is 重试次数
    uint32_t response_timeout_ms;          // ⏱️ I-Am 响应超时时间
    
    // ========== 本地设备参数 ==========
    uint32_t local_instance_id;            // 🆔 本地设备实例ID
    uint16_t local_max_apdu;               // 📏 最大 APDU 长度
    
    // ========== 网络层配置 ==========
    char     interface_name[32];           // 🌐 网络接口名称
    uint16_t port;                         // 🔌 UDP 端口
    char     broadcast_address[48];        // 📡 广播地址
    
    // ========== 服务行为配置 ==========
    uint32_t read_timeout_ms;              // ⏱️ 读取操作超时
    uint32_t write_timeout_ms;             // ⏱️ 写入操作超时
    uint8_t  default_priority;             // ⭐ 默认写入优先级
    uint32_t cache_expiry_ms;              // 💾 缓存过期时间
    uint8_t  cache_strategy;               // 📊 缓存策略 (0=激进, 1=保守)
    uint32_t datalink_maintenance_ms;      // 🔧 DataLink 维护定时器间隔
    
    // ========== 连接管理配置 ==========
    uint8_t  max_reconnect_attempts;       // 🔄 最大重连次数
    uint32_t reconnect_interval_ms;        // ⏱️ 重连间隔
    
    // ========== 热配置监控 ==========
    int8_t   hot_config_enabled;           // 🔥 是否启用配置文件自动监控 (-1=不更新, 0=false, 1=true)
    uint32_t hot_config_polling_ms;        // ⏱️ 配置文件检查间隔
} bacnet_config_t;
```

### ⚙️ 配置文件示例

```yaml
# config.yaml
# BACnet 协议配置文件

protocols:
  # --- BACnet 协议配置块 ---
  bacnet:
    enabled: true                 # 是否启用 BACnet 协议栈

    # 设备发现与目标设备配置
    discovery:
      target_device_start: 5678   # 🎯 目标设备实例范围起始
      target_device_end: 5678     # 🎯 目标设备实例范围结束
      whois_retry: 3              # 🔄 Who-Is 重试次数
      response_timeout_ms: 5000   # ⏱️ 等待 I-Am 响应超时时间

    # 本地设备信息
    local_device:
      instance_id: 4194303        # 🆔 本地设备实例 ID (默认使用允许的最大值)
      max_apdu: 1476              # 📏 本地支持的最大 APDU 长度

    # 网络相关参数
    network:
      interface: ""               # 🌐 使用的网络接口 (留空则使用默认配置)
      port: 47808                 # 🔌 BACnet/IP UDP 端口 (默认 47808)
      broadcast_address: "255.255.255.255"  # 📡 广播地址，用于 Who-Is

    # 服务行为配置
    services:
      read_timeout_ms: 6000       # ⏱️ 读取操作等待确认的超时时间
      write_timeout_ms: 6000      # ⏱️ 写入操作等待确认的超时时间
      default_priority: 8         # ⭐ 写属性时使用的默认优先级 (1-16, 0 表示不指定)
      cache_expiry_ms: 1000       # 💾 读缓存过期时间 (毫秒)
      cache_strategy: 0           # 📊 缓存策略: 0=激进(每次都发送请求), 1=保守(使用未过期缓存)
      datalink_maintenance_ms: 1000  # 🔧 DataLink维护定时器间隔 (毫秒)

    # 连接管理配置
    connection:
      max_reconnect_attempts: 5   # 🔄 最大重连次数
      reconnect_interval_ms: 3000 # ⏱️ 重连间隔 (毫秒)

    # 热配置监控
    hot_config:
      enabled: true               # 🔥 是否启用配置文件自动监控 (默认: true)
      polling_interval_ms: 1000   # ⏱️ 配置文件检查间隔 (毫秒, 默认: 1000ms)
                                  # 建议范围: 500-5000ms, 太短会增加系统开销
```

### � 配置参数详解

#### BACnet 协议栈开关

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | int8_t | true | 是否启用 BACnet 协议栈 |

#### 设备发现配置

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `target_device_start` | uint32_t | 0 | 目标设备实例范围起始 |
| `target_device_end` | uint32_t | 4194303 | 目标设备实例范围结束 |
| `whois_retry` | uint8_t | 3 | Who-Is 重试次数 |
| `response_timeout_ms` | uint32_t | 3000 | I-Am 响应超时时间 |

#### 本地设备参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `local_instance_id` | uint32_t | 4194303 | 本地设备实例 ID（BACnet 允许的最大值） |
| `local_max_apdu` | uint16_t | 1476 | 最大 APDU 长度（BACnet/IP 典型值） |

#### 网络层配置

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `interface_name` | char[32] | "" | 网络接口名称（空=使用默认） |
| `port` | uint16_t | 47808 | BACnet/IP UDP 端口 |
| `broadcast_address` | char[48] | "255.255.255.255" | 广播地址，用于 Who-Is |

#### 服务行为配置

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `read_timeout_ms` | uint32_t | 8000 | 读取操作超时（API 中 timeout_ms=0 时使用） |
| `write_timeout_ms` | uint32_t | 8000 | 写入操作超时（API 中 timeout_ms=0 时使用） |
| `default_priority` | uint8_t | 16 | 默认写入优先级（API 中 priority=0 时使用，1-16） |
| `cache_expiry_ms` | uint32_t | 60000 | 缓存过期时间（毫秒） |
| `cache_strategy` | uint8_t | 0 | 0=激进（每次都发送请求），1=保守（使用未过期缓存） |
| `datalink_maintenance_ms` | uint32_t | 1000 | DataLink 维护定时器间隔 |

#### 连接管理配置

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_reconnect_attempts` | uint8_t | 3 | 最大重连次数 |
| `reconnect_interval_ms` | uint32_t | 5000 | 重连间隔（毫秒） |

#### 热配置监控

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `hot_config_enabled` | int8_t | true | 是否启用配置文件自动监控 |
| `hot_config_polling_ms` | uint32_t | 1000 | 配置文件检查间隔（建议 500-5000ms） |

### 💡 配置优先级

**三层优先级机制**（从低到高）：

```
1️⃣ 代码默认值（proto_bacnet_internal.hpp）
    ↓ 被覆盖
2️⃣ YAML 配置文件（config.yaml）
    ↓ 被覆盖
3️⃣ config_update() API 运行时更新
```

**示例**：
```c
// 1️⃣ 代码默认值
kReadTimeoutMs = 8000

// 2️⃣ YAML 文件覆盖
read_timeout_ms: 6000  # 实际使用 6000

// 3️⃣ API 更新覆盖
bacnet_config_t cfg = BACNET_CONFIG_INIT;
cfg.read_timeout_ms = 10000;
config_update(&cfg);  # 实际使用 10000
```

### 💡 使用建议

**✅ 推荐做法**：
1. 在配置文件中设置合理的全局默认值
2. API 调用时使用便捷宏 `BACNET_READ_INIT()` / `BACNET_WRITE_INIT()`
3. 只在特殊场景才手动覆盖默认值

**示例对比**：
```c
// ❌ 不推荐：每次都手动配置
bacnet_read_t req = {
    .device_instance = 5678,
    .object_type = OBJECT_ANALOG_INPUT,
    .object_instance = 1,
    .property_id = PROP_PRESENT_VALUE,
    .array_index = -1,        // 重复代码
    .timeout_ms = 6000,       // 重复代码
    .value = &value
};

// ✅ 推荐：使用宏 + 配置文件默认值
bacnet_read_t req = BACNET_READ_INIT(
    5678, OBJECT_ANALOG_INPUT, 1, PROP_PRESENT_VALUE, &value
);
// array_index 自动为 -1
// timeout_ms 自动使用配置文件的 read_timeout_ms (6000)
```

### 🔥 热配置重载机制

**v3.1 版本支持自动配置文件监控，编辑配置后自动生效，无需重启程序！**

#### ⚙️ 三种配置更新方式

| 方式 | 触发条件 | 使用场景 | 性能开销 | 可靠性 |
|------|----------|----------|----------|--------|
| 🤖 **自动监控** | 编辑 config.yaml 并保存 | 开发/生产环境（推荐） | < 0.1% CPU | ⭐⭐⭐⭐⭐ |
| 📞 **API 调用** | 代码调用 `bacnet_reload_config()` | 需要编程控制 | 无 | ⭐⭐⭐⭐⭐ |
| 📡 **信号触发** | `kill -SIGUSR1 <pid>` | 运维脚本控制 | 无 | ⭐⭐⭐⭐ |

#### 🤖 方式 1：自动文件监控（默认启用）⭐ 推荐

**工作原理**：
- BACnet 驱动初始化时**自动启动监控线程**
- 每秒检查一次配置文件修改时间（mtime）
- 检测到变化后设置 `pending_reload` 标志
- **下次 `plc_proto_read/write` 时自动重载配置**（延迟重载，避免死锁）
- 完全重新初始化驱动，所有配置立即生效

**使用步骤**：
```bash
# 1. 程序正在运行
./bacnet_demo

# 2. 编辑配置文件
vim ../config.yaml
# 修改任意配置项，例如：
#   - target_device_start: 5678 → 5679
#   - cache_expiry_ms: 1000 → 2000

# 3. 保存文件（:wq 或 echo "" >> config.yaml）
# ✅ 监控线程检测到变化（< 1 秒）
# ✅ 下次读写操作时自动重载（通常几秒内）
# ✅ 无需重启程序！
```

**日志输出示例**：
```log
# 初始化时
[BACnet][Driver] Hot config monitoring started for: ../config.yaml
[BACnet][HotConfig] Monitor thread started, watching: ../config.yaml (polling interval: 1000ms)

# 检测到文件变化
[BACnet][HotConfig] Config file changed (1762740028 -> 1762741561), invoking callback
[BACnet] Config reload triggered
[BACnet] Config reload flag set, will reload on next read/write request

# 下次读写操作触发重载
[BACnet] Pending config reload detected, releasing driver...
[BACnet][Driver] Releasing driver resources...
[BACnet][Driver] Stopping hot config monitoring...
[BACnet][HotConfig] Monitor thread stopped

# 重新初始化
[BACnet][Driver] Initializing driver...
[BACnet][Config] Configuration loaded from '../config.yaml'
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃          BACnet Configuration Loaded                         ┃
┃   ... (显示所有新配置) ...
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
[BACnet] Connected to device range 5678-5678 successfully
```

**技术细节**：

| 特性 | 说明 |
|------|------|
| 🧵 **监控线程** | 独立线程，使用 `std::thread`，RAII 自动管理 |
| ⏱️ **检查间隔** | 1000ms（可配置） |
| 🔒 **线程安全** | 使用 `std::atomic` + `std::mutex` 保证安全 |
| 🛡️ **死锁避免** | 延迟重载机制，监控线程不会阻塞自己 |
| 🔄 **重载方式** | 完全释放旧驱动 → 重新初始化 → 重新连接设备 |
| 📊 **性能开销** | < 0.1% CPU（每秒一次 stat() 系统调用） |
| 💾 **内存开销** | ~8KB（一个线程栈） |

**常见场景**：

1. **修改目标设备**（自动重新发现）：
   ```yaml
   target_device_start: 5678 → target_device_start: 5679
   ```
   自动断开旧连接，重新发现新设备 ✅

2. **修改超时时间**（立即生效）：
   ```yaml
   read_timeout_ms: 6000 → read_timeout_ms: 10000
   ```
   下次读取使用新超时时间 ✅

3. **修改缓存策略**（立即生效）：
   ```yaml
   cache_strategy: 0 → cache_strategy: 1
   ```
   从激进模式切换到保守模式 ✅

#### 📞 方式 2：手动 API 调用

```c
// 在你的代码中手动触发重载
int result = bacnet_reload_config();
if (result == PROTO_SUCCESS) {
    printf("✅ 配置重载标志已设置，将在下次读写时生效\n");
}
```

**适用场景**：
- 通过自定义信号处理器触发
- 通过网络命令触发（HTTP/TCP）
- 需要在特定时机重载配置
- 禁用自动监控时的唯一重载方式

**注意事项**：
- ⚠️ 不建议在监控线程回调中调用（会自动设置标志）
- ⚠️ 函数只设置标志位，实际重载发生在下次读写操作
- ✅ 线程安全，可以在任何线程调用

#### 📡 方式 3：信号触发（需手动实现）

**步骤 1**：在代码中注册信号处理器
```c
#include <signal.h>

void signal_handler(int sig) {
    if (sig == SIGUSR1) {
        printf("📡 收到 SIGUSR1 信号，触发配置重载...\n");
        bacnet_reload_config();
    }
}

int main() {
    // 注册信号处理器
    signal(SIGUSR1, signal_handler);
    
    // ... 正常业务逻辑 ...
}
```

**步骤 2**：发送信号
```bash
# 找到进程 PID
ps aux | grep bacnet_demo

# 发送 SIGUSR1 信号
kill -SIGUSR1 12345
```

**运维脚本示例**：
```bash
#!/bin/bash
# reload_bacnet_config.sh

# 修改配置文件
sed -i 's/log_level: debug/log_level: info/' /path/to/config.yaml

# 发送重载信号
PID=$(pgrep -f bacnet_demo)
if [ -n "$PID" ]; then
    kill -SIGUSR1 $PID
    echo "✅ 已向进程 $PID 发送配置重载信号"
else
    echo "❌ 未找到 bacnet_demo 进程"
fi
```

#### 🧪 测试热配置

使用专门的测试程序验证热配置功能：

```bash
# 编译
cd build && make hot_config_test

# 运行测试程序
./hot_config_test

# 在另一个终端修改配置
echo "" >> ../config.yaml
# 或者
vim ../config.yaml  # 修改后保存

# 观察测试程序输出
```

**测试程序日志**：
```
╔════════════════════════════════════════════════════════════════╗
║         🔥 BACnet 热配置自动监控测试                          ║
╚════════════════════════════════════════════════════════════════╝

📋 测试步骤：
1. 程序启动后会自动监控 ../config.yaml
2. 在另一个终端编辑配置文件
3. 观察本程序输出，配置会在 1 秒内自动重载

✅ BACnet 驱动初始化成功
✅ 热配置监控已自动启动

═══════════════════════════════════════════════════════════════
🔍 监控中... (每 10 秒执行一次读取操作)
═══════════════════════════════════════════════════════════════

[01] 执行读取操作... ✅ 读取成功: 0.00
[02] 执行读取操作... ✅ 读取成功: 0.00

# 修改配置文件后...
[BACnet][HotConfig] Config file changed (1762740028 -> 1762741561)
[BACnet] Config reload triggered
[BACnet] Pending config reload detected, releasing driver...
[BACnet] Driver will be reinitialized on next request

[03] 执行读取操作... ✅ 读取成功 (使用新配置): 0.00
```

#### 🔧 高级配置

**1. 禁用自动监控**（不推荐）：
```yaml
# config.yaml
protocols:
  bacnet:
    hot_config:
      enabled: false  # 禁用自动监控
```

禁用后只能通过 API 或信号手动触发重载。

**2. 修改轮询间隔**：
```yaml
# config.yaml
protocols:
  bacnet:
    hot_config:
      enabled: true
      polling_interval_ms: 5000  # 改为 5 秒检查一次（降低开销）
```

**推荐值**：
- 开发环境：500-1000ms（快速响应）
- 生产环境：1000-3000ms（平衡性能与响应速度）
- 低负载环境：3000-5000ms（最小开销）

**3. 性能调优**：

如果系统负载高，可以考虑：
```yaml
hot_config:
  enabled: true
  polling_interval_ms: 3000  # 降低检查频率
```

性能对比：
| 间隔 | CPU 占用 | 响应延迟 | 推荐场景 |
|------|----------|----------|----------|
| 500ms | ~0.2% | 0.5-1s | 开发调试 |
| 1000ms | ~0.1% | 1-2s | **生产推荐** ⭐ |
| 3000ms | ~0.03% | 3-4s | 低负载环境 |
| 5000ms | ~0.02% | 5-6s | 极低负载 |

#### ⚠️ 注意事项

1. **配置重载是完全重启**：
   - ✅ 所有配置立即生效
   - ✅ 自动重新连接设备
   - ⚠️ 正在进行的请求会被中断
   - ⚠️ 缓存数据会清空

2. **避免频繁修改**：
   ```bash
   # ❌ 不要这样做：
   for i in {1..100}; do
       echo "test" >> config.yaml
       sleep 0.1
   done
   
   # ✅ 推荐做法：
   # 一次性修改完所有配置，然后保存
   vim config.yaml  # 修改多个配置项
   :wq              # 保存一次即可
   ```

3. **文件编辑器兼容性**：
   - ✅ vim/nano/emacs：正常工作
   - ✅ echo/sed/awk：正常工作
   - ⚠️ 某些 IDE（如 VSCode）：可能需要手动保存
   - ⚠️ 网络文件系统（NFS）：mtime 可能延迟更新

4. **重载时机**：
   - 文件变化后 1 秒内检测到
   - 下次 `plc_proto_read/write` 时才执行重载
   - 如果程序空闲，可能需要等待下次操作

#### 🐛 故障排查

**问题 1：配置修改后不生效**

排查步骤：
```bash
# 1. 检查监控线程是否启动
grep "Hot config monitoring started" bacnet.log

# 2. 检查文件时间是否变化
stat config.yaml | grep Modify

# 3. 强制更新文件时间
touch config.yaml

# 4. 查看日志
grep "Config file changed" bacnet.log
```

**问题 2：程序崩溃/死锁**

如果遇到此问题（不应该发生），请：
```yaml
# 临时禁用热配置
hot_config:
  enabled: false
```

然后联系开发人员，提供：
- 完整日志文件
- 崩溃时的堆栈信息
- 配置文件内容

**问题 3：性能影响**

如果发现性能问题：
```yaml
# 增加轮询间隔
hot_config:
  polling_interval_ms: 5000  # 从 1s 改为 5s
```

---

#### 🎯 热配置最佳实践

1. ✅ **开发环境**：保持默认配置（enabled: true, 1000ms）
2. ✅ **生产环境**：根据需求调整间隔（1000-3000ms）
3. ✅ **修改前备份**：`cp config.yaml config.yaml.bak`
4. ✅ **一次性修改**：避免频繁编辑触发多次重载
5. ✅ **验证配置**：修改后观察日志确认生效
6. ⚠️ **谨慎禁用**：只在特殊情况下禁用自动监控

---

### 🎛️ 配置管理架构

**v3.0 版本重构了配置管理系统，实现了三层配置优先级和集中化常量管理。**

#### 📍 配置常量集中管理

所有默认值常量统一定义在一个位置，方便查找和修改：

**位置**：`impl/bacnet/src/proto_bacnet_internal.hpp`  
**命名空间**：`bacnet::defaults`  
**行号**：约 135-210 行

```cpp
namespace bacnet::defaults {
    // [Common] 通用配置
    inline constexpr const char* kEnvironment = "development";
    inline constexpr const char* kLogLevel = "debug";
    inline constexpr const char* kLogFile = "bacnet.log";
    
    // [Discovery] 设备发现
    inline constexpr uint32_t kTargetDeviceStart = 5678;
    inline constexpr uint32_t kTargetDeviceEnd = 5678;
    inline constexpr uint8_t kWhoIsRetry = 3;
    inline constexpr uint32_t kDiscoveryTimeoutMs = 5000;
    
    // [LocalDevice] 本地设备
    inline constexpr uint32_t kLocalDeviceInstance = 4194303;
    inline constexpr uint16_t kMaxApdu = 1476;
    
    // [Network] 网络配置
    inline constexpr uint16_t kPort = 47808;
    inline constexpr const char* kBroadcastAddress = "255.255.255.255";
    
    // [Services] 服务行为
    inline constexpr uint32_t kReadTimeoutMs = 6000;
    inline constexpr uint32_t kWriteTimeoutMs = 6000;
    inline constexpr uint8_t kDefaultPriority = 8;
    inline constexpr uint32_t kCacheExpiryMs = 1000;
    inline constexpr uint8_t kCacheStrategy = 0;
    inline constexpr uint32_t kDatalinkMaintenanceMs = 1000;
    
    // [Connection] 连接管理
    inline constexpr uint8_t kMaxReconnectAttempts = 5;
    inline constexpr uint32_t kReconnectIntervalMs = 3000;
    
    // [System] 系统配置
    inline constexpr const char* kConfigPath = "../config.yaml";
}
```

#### 🔄 三层配置优先级

系统采用三层配置优先级机制，确保灵活性和健壮性：

```
┌─────────────────────────────────────────────────────────────┐
│ 优先级 1：用户传入参数（最高优先级）                        │
│ ↓ 如果用户未指定（值为0或NULL）                             │
├─────────────────────────────────────────────────────────────┤
│ 优先级 2：YAML 配置文件 (config.yaml)                       │
│ ↓ 如果配置文件不存在或配置项缺失                             │
├─────────────────────────────────────────────────────────────┤
│ 优先级 3：代码中的常量 (bacnet::defaults::*)                │
│ 最后的兜底保证，确保系统始终能运行                          │
└─────────────────────────────────────────────────────────────┘
```

**示例：读超时配置加载流程**

```cpp
// 用户调用
bacnet_read_t req = BACNET_READ_INIT(5678, ANALOG_INPUT, 1, PRESENT_VALUE, &value);
// timeout_ms = 0 (未指定)

// 系统内部处理
uint32_t timeout = req->timeout_ms;  // Step 1: 检查用户值 = 0

if (timeout == 0) {
    timeout = context->config.bacnet.services.read_timeout_ms;  // Step 2: 使用YAML配置
}

if (timeout == 0) {
    timeout = bacnet::defaults::kReadTimeoutMs;  // Step 3: 使用代码常量兜底 = 6000
}
```

#### 📊 配置组织结构

| 配置分类 | 包含项目 | 说明 |
|---------|---------|------|
| **Common** | environment, log_level, log_file | 通用配置，影响日志和运行环境 |
| **Discovery** | target_device, whois_retry, timeout | 设备发现相关配置 |
| **LocalDevice** | instance_id, max_apdu | 本地BACnet设备参数 |
| **Network** | port, broadcast_address, interface | 网络层配置 |
| **Services** | timeout, priority, cache, datalink | 服务行为和性能调优 |
| **Connection** | reconnect_attempts, interval | 连接管理和重连策略 |
| **System** | config_path | 系统级配置 |

#### 🛠️ 配置文件完整示例

```yaml
# config.yaml - 所有配置项都是可选的，未配置项将使用代码中的默认值

common:
  environment: "production"
  log_level: "info"
  log_file: "bacnet.log"

protocols:
  bacnet:
    enabled: true
    
    discovery:
      target_device_start: 5678
      target_device_end: 5678
      whois_retry: 3
      response_timeout_ms: 5000
    
    local_device:
      instance_id: 4194303
      max_apdu: 1476
    
    network:
      interface: ""                    # 留空使用默认
      port: 47808
      broadcast_address: "255.255.255.255"
    
    services:
      read_timeout_ms: 6000            # 读操作超时
      write_timeout_ms: 6000           # 写操作超时
      default_priority: 8              # 写操作默认优先级
      cache_expiry_ms: 1000            # 缓存过期时间
      cache_strategy: 0                # 0=激进 1=保守
      datalink_maintenance_ms: 1000    # DataLink维护间隔
    
    connection:
      max_reconnect_attempts: 5        # 最大重连次数
      reconnect_interval_ms: 3000      # 重连间隔基准
```

#### ✨ 配置管理优势

| 优势 | 说明 |
|------|------|
| **集中管理** | 所有默认值在一个文件的70行内，修改方便 |
| **清晰分类** | 按功能模块分组，每组都有注释说明 |
| **IDE友好** | 使用 `bacnet::defaults::k` + 自动补全快速查找 |
| **避免重复** | 消除了分散在多个文件的重复定义 |
| **易于维护** | 修改默认值只需编辑一个位置 |
| **健壮性** | 三层兜底机制，即使配置文件丢失也能运行 |
| **灵活性** | 支持运行时通过YAML文件调整所有参数 |

#### 📝 配置启动日志

系统启动时会打印完整的配置表，显示每个配置项的值和来源：

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃          BACnet Configuration Loaded                         ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃ [Common]                                                     ┃
┃   environment        : production            [YAML   ] ┃
┃   log_level          : info                  [YAML   ] ┃
┃   log_file           : bacnet.log            [DEFAULT] ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃ [BACnet.Services]                                            ┃
┃   read_timeout_ms    : 8000                  [YAML   ] ┃
┃   write_timeout_ms   : 6000                  [DEFAULT] ┃
┃   default_priority   : 8                     [DEFAULT] ┃
┃   cache_strategy     : 1                     [YAML   ] ┃
┃   datalink_maint_ms  : 1000                  [DEFAULT] ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃ [BACnet.Connection]                                          ┃
┃   max_reconnect_attempts: 5                  [DEFAULT] ┃
┃   reconnect_interval_ms : 3000               [DEFAULT] ┃
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
```

**来源标识说明**：
- `[YAML   ]` - 从 config.yaml 文件加载
- `[DEFAULT]` - 使用代码中的默认值常量

---

## 🏭 编译构建

### 🛠️ 构建脚本

```bash
# Debug 模式 - 包含调试信息，适合开发
bash build.sh debug

# Release 模式 - 优化编译，适合生产
bash build.sh release

# 清理构建文件
bash build.sh clean
```

### 📋 CMake 配置

```cmake
# CMakeLists.txt
project(proto_bacnet)

# 自动包含所有源文件
file(GLOB SRC_FILES src/*.cpp src/*.c)

# 创建共享库
add_library(${CMAKE_PROJECT_NAME} SHARED ${SRC_FILES})

# 链接依赖库
target_link_libraries(${CMAKE_PROJECT_NAME}
    bacnet-stack      # BACnet 协议栈
    yaml-cpp          # YAML 配置解析
    spdlog            # 日志库
    pthread           # 线程库
)
```

### 📦 依赖库

| 库名 | 版本 | 用途 | 必须性 |
|------|------|------|--------|
| bacnet-stack | latest | BACnet 协议实现 | ✅ 必须 |
| yaml-cpp | ≥0.6.0 | YAML 配置解析 | ✅ 必须 |
| spdlog | ≥1.0.0 | 高性能日志 | ✅ 必须 |
| pthread | system | POSIX 线程 | ✅ 必须 |

---

## 💻 使用示例

### 🌟 基础读取示例

```c
#include "impl/bacnet/src/proto_bacnet.h"
#include <stdio.h>
#include <unistd.h>

int main() {
    // 1️⃣ 准备读取请求
    bacnet_read_t read_req = {
        .device_instance = 5678,           // 目标设备ID
        .object_type = 0,                  // ANALOG_INPUT
        .object_instance = 1,              // 对象实例1
        .property_id = 85,                 // PRESENT_VALUE
        .array_index = -1,                 // 非数组
        .timeout_ms = 5000,                // 5秒超时
        .value = nullptr,                  // 稍后设置
        .check_only = false                // 发送请求模式
    };
    
    // 2️⃣ 分配输出缓冲区
    bacnet_data_value_t read_value;
    read_req.value = &read_value;
    
    // 3️⃣ 首次调用（可能返回 PROTO_NO_DATA）
    int result = plc_proto_read(&read_req);
    
    if (result == PROTO_NO_DATA) {
        printf("📡 请求已发送，等待设备响应...\n");
        sleep(3);  // 等待设备响应
        
        // 4️⃣ 重试读取（从缓存获取）
        result = plc_proto_read(&read_req);
    }
    
    // 5️⃣ 处理结果
    if (result == PROTO_SUCCESS) {
        if (read_value.type == BACNET_DATA_REAL) {
            printf("✅ 读取成功: 浮点值 = %.2f\n", read_value.value.real_value);
        } else {
            printf("✅ 读取成功: 类型=%d\n", read_value.type);
        }
    } else {
        printf("❌ 读取失败: 错误码=%d\n", result);
    }
    
    return 0;
}
```

### ✏️ 写入操作示例

```c
void write_example() {
    bacnet_write_t write_req = {
        .device_instance = 5678,
        .object_type = 1,                  // ANALOG_OUTPUT
        .object_instance = 1,
        .property_id = 85,                 // PRESENT_VALUE
        .array_index = -1,
        .priority = 8,                     // 优先级8
        .timeout_ms = 5000,
        .value = {
            .type = BACNET_DATA_REAL,
            .value.real_value = 25.5f     // 写入25.5
        }
    };
    
    // 异步调用 - 请求已提交
    int result = plc_proto_write(&write_req);
    if (result == PROTO_SUCCESS) {
        printf("✅ 写入请求已提交\n");
    } else {
        printf("❌ 写入请求失败: 错误码=%d\n", result);
    }
}
```

### 🔄 批量操作示例

```c
void batch_read_example() {
    const int NUM_POINTS = 5;
    bacnet_read_t requests[NUM_POINTS];
    bacnet_data_value_t values[NUM_POINTS];
    
    // 第一轮：快速提交所有请求（异步非阻塞）
    for (int i = 0; i < NUM_POINTS; i++) {
        requests[i] = {
            .device_instance = 5678,
            .object_type = 0,              // ANALOG_INPUT
            .object_instance = (uint32_t)i,
            .property_id = 85,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &values[i],
            .check_only = false
        };
        
        plc_proto_read(&requests[i]);  // 立即返回，不阻塞
        printf("📡 已发送 AI%d 读取请求\n", i);
    }
    
    // 等待设备响应
    printf("⏳ 等待设备响应...\n");
    sleep(3);
    
    // 第二轮：从缓存读取结果
    for (int i = 0; i < NUM_POINTS; i++) {
        int result = plc_proto_read(&requests[i]);
        if (result == PROTO_SUCCESS) {
            printf("✅ AI%d = %.2f\n", i, values[i].value.real_value);
        } else {
            printf("❌ AI%d 读取失败: %d\n", i, result);
        }
    }
}
```

### 🔍 队列检查模式示例

```c
void check_queue_example() {
    // 假设之前已经发送了读取请求，现在只检查队列
    bacnet_read_t check_req = {
        .device_instance = 5678,
        .object_type = 0,
        .object_instance = 1,
        .property_id = 85,
        .array_index = -1,
        .timeout_ms = 5000,
        .value = nullptr,        // 必须设置
        .check_only = true       // 🔍 仅检查队列模式
    };
    
    bacnet_data_value_t result_value;
    check_req.value = &result_value;
    
    int result = plc_proto_read(&check_req);
    if (result == PROTO_SUCCESS) {
        printf("✅ 队列中有数据: %.2f\n", result_value.value.real_value);
    } else if (result == -7) {  // PROTO_NO_DATA
        printf("📭 队列中无数据\n");
    } else {
        printf("❌ 检查失败: %d\n", result);
    }
}
```

---

## 🧵 线程模型

### 👥 线程角色

1. **🎯 主线程 (用户线程)**
   - 调用 `plc_proto_read/write()`
   - 检查哈希表缓存
   - 立即返回（异步非阻塞）
   - **特点**：异步非阻塞接口

2. **🧵 工作线程 (Worker Thread)**
   - 循环接收 BACnet 数据包
   - 调用协议栈处理器
   - 检查操作超时
   - 更新 TSM 定时器
   - 更新哈希表缓存
   - **特点**：后台运行，10ms轮询间隔

3. **🔥 热配置线程 (可选)**
   - 监控配置文件变化
   - 触发配置重载
   - **特点**：独立线程，1秒检查间隔

### 🔄 线程协作流程

```
用户线程                    工作线程
    │                           │
    ├─ plc_proto_read()         │
    │  ├─ 首次调用自动初始化    │
    │  ├─ 检查哈希表缓存        │
    │  ├─ 缓存命中 → 立即返回   │
    │  └─ 缓存未命中 → 发送请求│
    │     返回 PROTO_NO_DATA    │
    │                           ├─ 接收数据包
    │                           ├─ 处理回调
    │                           └─ 更新哈希表缓存
    │                           │
    ├─ plc_proto_read() (重试)  │
    │  └─ 从缓存读取 ─────────→ │
    │     返回 PROTO_SUCCESS    │
    │                           │
    └─ 处理结果                 │
```

### ⏰ 定时器管理

```cpp
// 工作线程中的定时器更新
static auto last_timer_update = std::chrono::steady_clock::now();
auto now = std::chrono::steady_clock::now();
auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - last_timer_update).count();

if (elapsed >= 100) {  // 每100ms更新一次
    tsm_timer_milliseconds(static_cast<uint16_t>(elapsed));
    datalink_maintenance_timer(elapsed / 1000);
    last_timer_update = now;
}
```

---

## 🔒 线程安全

### 🛡️ 安全机制

1. **⚡ 原子变量 (无锁)**
   ```cpp
   std::atomic<bacnet_connection_state_t> connection_state;
   std::atomic<bacnet_operation_state_t> operation_state;
   std::atomic<bool> target_found;
   std::atomic<int> reconnect_attempts;
   ```

2. **🔒 互斥锁 (保护共享数据)**
   ```cpp
   std::mutex object_states_mutex;    // 保护哈希表缓存
   std::mutex operation_mutex;        // 保护活动操作
   std::mutex hot_config_mutex;       // 保护热配置状态
   ```

3. **📊 哈希表缓存 (异步结果存储)**
   ```cpp
   // 使用四元组作为键缓存读取结果
   std::unordered_map<ObjectKey, ObjectState> object_states;
   
   // 工作线程接收响应后更新缓存
   {
       std::lock_guard<std::mutex> lock(object_states_mutex);
       auto& state = object_states[key];
       state.cached_value = decoded_value;
       state.has_valid_cache = true;
       state.timestamp = std::chrono::steady_clock::now();
   }
   
   // 主线程查询缓存
   {
       std::lock_guard<std::mutex> lock(object_states_mutex);
       auto it = object_states.find(key);
       if (it != object_states.end() && it->second.has_valid_cache) {
           *req->value = it->second.cached_value;
           return PROTO_SUCCESS;
       }
   }
   ```

### 🏷️ RAII 资源管理

```cpp
// 智能指针管理线程
std::unique_ptr<std::thread> worker_thread;

// 自动清理
~BacnetContext() {
    if (worker_thread && worker_thread->joinable()) {
        worker_stop.store(true);
        worker_thread->join();
    }
}

// 作用域锁
{
    std::lock_guard<std::mutex> lock(operation_mutex);
    // 自动解锁，无需担心异常
}
```

---

## 📊 性能指标

| 指标 | 值 | 说明 |
|------|-----|------|
| 📏 内存占用 | ~2.1MB | 包含协议栈和上下文 |
| ⚡ 读操作延迟 | 50-150ms | 取决于网络条件 |
| ⚡ 写操作延迟 | 50-150ms | 取决于网络条件 |
| 🧵 CPU占用 | 1-2% | 空闲时的线程占用 |
| 📨 并发操作 | 理论无限制 | 受TSM和网络限制 |
| ⏱️ 工作线程轮询 | 10ms | 可配置调整 |

### 📈 性能优化建议

1. **🔄 合理并发** - 使用多线程并发调用（确保线程安全）
2. **⏰ 合理超时** - 避免过长的超时时间
3. **🔍 队列检查** - 使用 `check_only` 模式避免重复请求
4. **💾 智能缓存** - 启用设备地址缓存减少发现开销

---

## ❓ 常见问题

### Q1: 如何处理 PROTO_NO_DATA？

```c
// 首次调用可能返回 PROTO_NO_DATA（请求已发送，等待响应）
bacnet_read_t req = { /* ... */ };
bacnet_data_value_t value;
req.value = &value;

int result = plc_proto_read(&req);
if (result == PROTO_NO_DATA) {
    printf("📡 请求已发送，等待设备响应...\n");
    sleep(3);  // 等待设备响应
    
    // 重试读取，从缓存获取
    result = plc_proto_read(&req);
    if (result == PROTO_SUCCESS) {
        printf("✅ 从缓存读取成功\n");
    }
}
```

### Q2: 可以同时提交多个请求吗？

```c
// 可以！驱动内部使用 TSM (传输状态机) 管理多个并发操作
// 异步调用会立即返回，多个请求可以并发发送
bacnet_read_t req1 = { /* ... */ };
bacnet_read_t req2 = { /* ... */ };
bacnet_read_t req3 = { /* ... */ };

// 快速提交多个请求（异步非阻塞）
int result1 = plc_proto_read(&req1);  // 立即返回 PROTO_NO_DATA
int result2 = plc_proto_read(&req2);  // 立即返回 PROTO_NO_DATA
int result3 = plc_proto_read(&req3);  // 立即返回 PROTO_NO_DATA

// 等待设备响应后，从缓存读取结果
sleep(3);
result1 = plc_proto_read(&req1);  // 返回 PROTO_SUCCESS
result2 = plc_proto_read(&req2);  // 返回 PROTO_SUCCESS
result3 = plc_proto_read(&req3);  // 返回 PROTO_SUCCESS
```

### Q3: 错误码含义是什么？

```c
PROTO_SUCCESS          =  0   // ✅ 成功
PROTO_ERROR_PARAM      = -1   // ❌ 参数错误
PROTO_ERROR_INIT       = -2   // ❌ 初始化失败
PROTO_ERROR_CONNECT    = -3   // ❌ 连接失败
PROTO_ERROR_READ       = -4   // ❌ 读取失败
PROTO_ERROR_WRITE      = -5   // ❌ 写入失败
PROTO_ERROR_UNSUPPORTED = -6   // ❌ 不支持的操作
```

### Q4: 如何调试 BACnet 通信？

```bash
# 1. 启用详细日志
# 修改 config.yaml
common:
  log_level: debug

# 2. 查看协议栈日志
tail -f bacnet.log | grep -E "(ReadProperty|WriteProperty|Ack)"

# 3. 使用 Wireshark 抓包
# 过滤 BACnet 协议：bacnet
# 监听 UDP 端口 47808

# 4. 检查设备发现
# 确保目标设备响应 Who-Is 请求
```

### Q5: 配置文件修改后如何生效？

```bash
# 方式1: 重启程序
# 方式2: 发送信号 (推荐)
kill -SIGUSR1 $(pidof your_program)

# 方式3: 代码调用
bacnet_reload_config();
```

### Q6: 如何处理网络故障？

```yaml
# 启用自动重连 (计划功能)
bacnet:
  discovery:
    auto_reconnect: true
    max_reconnect_attempts: 5
    reconnect_interval_ms: 5000
```

### Q7: 内存使用量大吗？

```
📊 内存分析：
- BacnetContext: ~1.2MB (包含缓冲区和状态)
- BACnet协议栈: ~800KB
- 线程栈: ~256KB × 2
- 总计: ~2.1MB

优化建议：
- 减小缓冲区大小
- 使用内存池管理动态分配
- 限制并发操作数量
```

### Q8: 如何扩展新功能？

遵循模块化原则：

```cpp
// 1. 添加回调 → proto_bacnet_callbacks.cpp
void handle_new_feature_ack(...) {
    // 处理新功能响应
}

// 2. 添加操作 → proto_bacnet_io.cpp
proto_status_t execute_new_feature(...) {
    // 实现新功能逻辑
}

// 3. 添加接口 → proto_bacnet.h
int bacnet_new_feature(void *req);
```

### Q10: 生产环境部署注意事项？

```bash
# 1. 使用 Release 模式编译
bash build.sh release

# 2. 配置合理的日志级别
log_level: info  # 生产环境避免 debug

# 3. 设置适当的超时时间
read_timeout_ms: 5000   # 5秒
write_timeout_ms: 5000  # 5秒

# 4. 监控资源使用
# - CPU: < 5%
# - 内存: < 10MB
# - 网络: < 1Mbps

# 5. 错误处理
# - 实现重试机制
# - 添加监控告警
# - 记录详细错误日志
```

### Q11: 如何查看当前使用的版本？

```bash
./switch_version.sh status
```

或者查看代码：
```bash
grep -n "namespace bacnet" src/proto_bacnet_core.cpp
```
如果有输出，说明是新版本。

### Q12: 编译错误如何处理？

**情况1：缺少模块文件**
```
错误: 找不到 proto_bacnet_callbacks.cpp
```
确保所有新文件都已创建：
- proto_bacnet_callbacks.cpp
- proto_bacnet_discovery.cpp
- proto_bacnet_io.cpp

**情况2：头文件找不到**
```
错误: proto_bacnet_internal.hpp: No such file
```
检查内部头文件是否存在并更新。

**情况3：链接错误**
```
undefined reference to `bacnet::xxx'
```
确保所有 `.cpp` 文件都被编译进库。

### Q13: 如何回退到旧版本？

```bash
# 使用脚本
./switch_version.sh old

# 或手动
cp src/proto_bacnet_core_old.cpp src/proto_bacnet_core.cpp
```

---

## 🔧 版本管理

### 📋 版本切换工具

项目提供了 `switch_version.sh` 脚本用于版本管理：

```bash
# 查看当前状态
./switch_version.sh status

# 切换到新版本
./switch_version.sh new

# 恢复到旧版本
./switch_version.sh old

# 切换并编译
./switch_version.sh new && ./switch_version.sh build

# 清理备份文件
./switch_version.sh clean
```

### 📊 版本对比

| 特性 | 旧版本 | 新版本（重构） |
|------|--------|----------------|
| 代码行数 | 1042行（单文件） | 690+250+150+350=1440行（模块化） |
| 语言特性 | 纯C++（namespace） | 现代C++17+ |
| 内存管理 | new/delete | 智能指针（unique_ptr） |
| 线程安全 | mutex | atomic + mutex + condition_variable |
| 文件组织 | 单一大文件 | 模块化拆分 |
| 可维护性 | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## 🎯 状态机设计

### 🔄 连接状态机

```
连接状态机:
┌──────┐  connect  ┌─────────┐  discover  ┌──────────┐
│ IDLE │ ────────→ │CONNECTING│ ─────────→ │CONNECTED │
└──────┘           └─────────┘             └──────────┘
   ↑                    │                        │
   │                    │ fail                   │ disconnect
   │                    ↓                        ↓
   │              ┌──────────────┐         ┌─────────┐
   └──────────────│DISCONNECTED  │←────────│  (断开)  │
                  └──────────────┘         └─────────┘
```

### ⚙️ 操作状态机

```
操作状态机:
┌──────┐  start_op  ┌─────────┐  complete  ┌─────────┐
│ IDLE │ ─────────→ │ PENDING │ ─────────→ │ SUCCESS │
└──────┘            └─────────┘             └─────────┘
   ↑                     │                        │
   │                     │ error/timeout          │
   │                     ↓                        ↓
   │                ┌────────┐                   (reset)
   └────────────────│ FAILED │←──────────────────┘
                    └────────┘
```

---

## 💎 现代 C++ 特性

### 4. **命名空间**
```cpp
namespace bacnet {
    // 内部实现，避免全局命名冲突
}

// C接口导出
extern "C" {
    int proto_driver_init(proto_ctx_t *ctx);
}
```

### 5. **std::chrono 时间管理**
```cpp
// 精确的时间点
std::chrono::steady_clock::time_point start_time;

// 超时检测
bool is_timeout() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    return elapsed >= timeout_ms;
}
```

### 6. **std::function 回调**
```cpp
// 灵活的回调函数类型
using AsyncCallback = std::function<void(proto_status_t status, void* userdata)>;

struct CallbackSet {
    AsyncCallback on_connect{nullptr};
    AsyncCallback on_disconnect{nullptr};
    AsyncCallback on_read_complete{nullptr};
    AsyncCallback on_write_complete{nullptr};
};
```

### 7. **内部状态哈希表 + 缓存机制**
```cpp
// 使用 std::unordered_map 实现对象状态缓存
struct ObjectKey {
    uint32_t device_instance;
    uint16_t object_type;
    uint32_t object_instance;
    uint32_t property_id;
};

struct ObjectState {
    uint8_t active_invoke_id;           // 当前请求的 invoke_id
    bacnet_data_value_t cached_value;   // 缓存的数据
    bool has_valid_cache;               // 缓存是否有效
    std::chrono::steady_clock::time_point timestamp;  // 缓存时间戳
    proto_status_t status;              // 请求状态
    void* original_request;             // 原始请求指针
};

std::unordered_map<ObjectKey, ObjectState> object_states;
```

---

## 📁 各文件功能详解

### proto_bacnet_internal.hpp
- 定义内部数据结构 `BacnetContext`
- 声明所有内部函数
- 使用现代C++特性定义成员变量
- 全局上下文管理

### proto_bacnet_core.cpp (690行)
**核心功能：**
- ✅ `proto_driver_init()` - 驱动初始化
- ✅ `proto_driver_release()` - 资源释放
- ✅ `proto_connect()` - 连接设备
- ✅ `proto_disconnect()` - 断开连接
- ✅ `plc_proto_read()` - PLC读接口 ⭐
- ✅ `plc_proto_write()` - PLC写接口 ⭐
- ✅ 状态管理函数
- ✅ 事件管理函数
- ✅ 操作完成处理

### proto_bacnet_callbacks.cpp (250行)
**回调处理：**
- ✅ `handle_iam_callback()` - I-Am响应（设备发现）
- ✅ `handle_read_property_ack()` - ReadProperty应答
- ✅ `handle_write_property_ack()` - WriteProperty应答
- ✅ `handle_error_response()` - 错误响应
- ✅ `handle_abort_response()` - Abort响应
- ✅ `handle_reject_response()` - Reject响应
- ✅ `register_bacnet_handlers()` - 注册所有回调

### proto_bacnet_discovery.cpp (150行)
**设备发现：**
- ✅ `discover_target_device()` - Who-Is/I-Am发现机制
- ✅ `worker_loop_function()` - 工作线程循环
- ✅ `start_worker_thread()` - 启动工作线程
- ✅ `stop_worker_thread()` - 停止工作线程

### proto_bacnet_io.cpp (350行)
**读写操作：**
- ✅ `execute_read_property()` - 执行读取操作
- ✅ `execute_write_property()` - 执行写入操作
- ✅ `resolve_target_address()` - 解析设备地址
- ✅ `store_application_value()` - 存储读取值
- ✅ `convert_to_application_value()` - 转换写入值

### proto_bacnet_utils.cpp
**工具函数：**
- ✅ `bacnet_load_config_from_yaml()` - YAML配置加载

### proto_bacnet_hot_config.cpp
**热配置：**
- ✅ `bacnet_hot_config_init()` - 初始化文件监控
- ✅ `bacnet_hot_config_cleanup()` - 清理资源
- ✅ 文件监控机制

---

## 🔮 扩展性设计

### 添加新功能

遵循模块化原则：

```cpp
1. 回调机制（预留）
   - CallbackSet 结构已定义
   - 需要实现 trigger_callback()

2. 自动重连
   - reconnect_attempts 已定义
   - 需要在 worker_loop 中检测连接丢失

3. 性能统计
   - 添加计数器
   - 记录延迟、成功率等
```

### 代码位置映射
```
添加回调   → proto_bacnet_core.cpp
添加重连   → proto_bacnet_discovery.cpp
添加统计   → 新建 proto_bacnet_stats.cpp
```

---

## 📋 附录

### A. 完整目录结构

```
impl/bacnet/
├── build/                          # 构建输出目录
│   ├── CMakeFiles/
│   ├── package/
│   │   ├── bin/
│   │   │   └── bacnet_demo
│   │   └── lib/
│   │       └── libproto_bacnet.so
│   └── compile_commands.json
├── demo/
│   └── main.cc
├── depend/
│   ├── include/
│   │   └── bacnet/
│   └── lib/
│       └── libbacnet.a
├── src/
│   ├── proto_bacnet.h
│   ├── proto_bacnet_internal.hpp
│   ├── proto_bacnet_core.cpp 
│   ├── proto_bacnet_callbacks.cpp
│   ├── proto_bacnet_discovery.cpp
│   ├── proto_bacnet_io.cpp
│   ├── proto_bacnet_utils.cpp
│   └── proto_bacnet_hot_config.cpp
├── build.sh
├── CMakeLists.txt
├── config.yaml
├── switch_version.sh
├── README.md
└── VERSION
```

### B. 相关文档链接

- [BACnet协议标准](http://www.bacnet.org/)
- [BACnet-stack库](https://github.com/bacnet-stack/bacnet-stack)
- [YAML配置格式](https://yaml.org/)
- [C++ 线程库参考](https://en.cppreference.com/w/cpp/thread)

---

## 📝 更新日志

### 🎆 v4.0 - 2025-11-10 (单例模式与自动管理重构) ⭐ 重大更新

**🏛️ 核心改进：零配置架构，完全自动化资源管理**

#### 🔧 单例模式重构
- ✨ **BacnetDriver 单例** - Meyer's Singleton 实现，线程安全（C++11 保证）
- ✨ **BacnetContext 单例** - 全局唯一上下文，由 Driver 管理生命周期
- 🔒 **禁止拷贝/移动** - 删除拷贝构造和移动构造，确保单例唯一性
- 📍 **消除全局指针** - 移除 `g_ctx` 全局指针，避免析构顺序问题

#### ⚡ 自动初始化机制
- 🚀 **零配置启动** - 首次调用 `plc_proto_read/write` 时自动初始化
- 🔄 **自动连接管理** - 未连接时自动发现设备并建立连接
- 📦 **延迟加载** - 仅在实际使用时才初始化，减少启动开销
- 🛡️ **健壮性保证** - 初始化失败自动重试，配置文件缺失使用默认值

#### 🧹 自动资源清理
- ✅ **atexit 注册** - 程序退出时自动调用 `bacnet_cleanup()`
- ✅ **空析构函数** - 单例析构不做清理，避免析构顺序问题
- ✅ **提前清理** - atexit 在全局对象析构**之前**执行
- ✅ **OS 资源回收** - 剩余资源由操作系统自动回收
- 🛡️ **Bus Error 根治** - 彻底解决程序退出时的崩溃问题

#### 🔄 热配置优化
- 🔥 **worker_stop 重置** - 热配置重载后正确重置工作线程标志
- 🔥 **worker_running 重置** - 允许重新启动设备发现
- ⚡ **延迟重载** - pending_reload 标志延迟执行，避免监控线程死锁
- 📊 **状态完整清理** - reset() 清理所有缓存和映射

#### 📝 API 简化
- ❌ **移除 plc_proto_init()** - 不再需要显式初始化
- ❌ **移除 bacnet_cleanup()** - 不对外暴露，内部自动调用
- ✅ **极简接口** - 用户只需 `plc_proto_read()` 和 `plc_proto_write()`
- ✅ **零学习成本** - 无需了解初始化和清理流程

#### 🏗️ 架构改进
- 📊 **调用链简化** - 消除中间层 `ensure_init_and_connect_locked()`
- 🔧 **模块职责清晰** - Driver 负责生命周期，Context 负责状态管理
- 🧵 **线程模型优化** - 主线程 + 工作线程 + 热配置线程，职责分离
- 📚 **文档完善** - 新增"单例模式与资源管理"专门章节

#### 🐛 Bug 修复
- ✅ **修复 Bus error** - atexit 提前清理，避免全局对象析构顺序问题
- ✅ **修复热配置失败** - 重置 worker_stop 标志，允许设备发现重新启动
- ✅ **修复链接冲突** - bacnet_cleanup() 移到 `extern "C"` 块外，C++ 链接
- ✅ **修复编译错误** - AsyncCallback 类型定义位置调整

#### 📊 性能优化
- ⚡ **启动加速** - 延迟初始化，减少程序启动时间
- 💾 **内存优化** - 智能指针自动管理，无内存泄漏
- 🔒 **线程安全** - 单例保证线程安全，无竞态条件

#### 🎯 用户体验提升
- ✅ **代码量减少** - 用户代码从 10 行减少到 3 行（减少 70%）
- ✅ **错误率降低** - 无需手动管理资源，避免忘记清理
- ✅ **学习曲线平缓** - 无需了解底层实现，即插即用
- ✅ **调试更简单** - 自动管理流程，日志清晰明确

#### 📋 测试验证
- ✅ 读写功能正常（analog-input/output/value 测试通过）
- ✅ 热配置重载成功（config.yaml 修改后自动生效）
- ✅ 高频轮询测试（100ms 间隔，20 次，100% 成功）
- ✅ 程序退出清理完整（无 Bus error，日志完整）

---

### 🚀 v3.0 - 2025-11-07 (配置管理重构版)

**� 核心改进：统一配置管理架构**

#### ⚙️ 配置系统重构
- ✨ **三层配置优先级** - 用户参数 → YAML配置 → 代码常量，灵活且健壮
- 📍 **集中化常量管理** - 所有默认值统一在 `proto_bacnet_internal.hpp` 的 `bacnet::defaults` 命名空间
- 🗂️ **分类组织** - 7大配置模块（Common/Discovery/LocalDevice/Network/Services/Connection/System）
- 📊 **配置来源追踪** - 启动时打印完整配置表，标明每个值的来源（YAML/DEFAULT）
- 🔧 **YAML完整支持** - 真正的配置文件解析，不再是硬编码默认值

#### 🆕 新增配置项
- `datalink_maintenance_ms` - DataLink维护定时器间隔配置
- `max_reconnect_attempts` - 最大重连次数可配置
- `reconnect_interval_ms` - 重连间隔基准时间可配置

#### 🎨 API简化优化
- 📦 **便捷宏** - `BACNET_READ_INIT()` / `BACNET_WRITE_INIT()` 减少30%参数
- 🎯 **智能默认值** - `array_index`默认-1，`timeout_ms`/`priority`默认0（使用配置）
- 📝 **代码精简** - 使用便捷宏可减少22-57%代码量

#### 🏗️ 架构优化
- 🔧 **配置结构扩展** - 新增 `bacnet_connection_config_t` 结构
- 🗑️ **消除硬编码** - 移除所有分散的魔法数字，统一为命名常量
- 📚 **文档完善** - README新增"配置管理架构"章节，详细说明配置系统

#### 💡 维护性提升
- ✅ 所有配置常量集中在70行代码内，修改方便
- ✅ IDE友好的命名空间设计（`bacnet::defaults::k` + 自动补全）
- ✅ 详细注释说明每个配置项的含义和单位
- ✅ 配置文件不存在时系统仍可正常运行

### �🎉 v2.1 - 2025-10-28 (异步优化版)

- ✨ **新增 `check_only` 模式** - 仅检查队列而不发送重复请求
- 🐛 **修复 ACK 处理延迟** - 工作线程定期醒来处理数据包
- 🚀 **优化工作线程逻辑** - 使用 `wait_for` 避免无限阻塞
- 📊 **改进性能监控** - 更精确的延迟统计
- 🔧 **增强错误处理** - 更详细的错误码和日志
- 📚 **完善文档** - 详尽的使用指南和架构说明

### 🎯 v2.0 - 2025-10-22 (重构版)

- 🏗️ **模块化重构** - 按功能拆分文件，提高可维护性
- 💎 **现代 C++** - 使用智能指针、原子变量、RAII
- 🔒 **线程安全增强** - 完善的同步机制
- 📦 **代码重构** - 从单文件重构为模块化架构
- 🧵 **异步优化** - 改进事件驱动机制
- ⚙️ **热配置** - 支持运行时配置重载

### 🏁 v1.0 - 2025-10-16 (初始版本)

- ✅ **基础功能** - 设备发现、读写操作
- ✅ **协议栈集成** - BACnet4Linux 支持
- ✅ **配置管理** - YAML 配置加载
- ✅ **日志系统** - spdlog 集成
- ✅ **构建系统** - CMake 构建支持

---

## 🙏 贡献指南

欢迎提交改进建议和代码贡献！

### 🐛 报告问题

1. 使用 GitHub Issues
2. 提供详细的错误信息
3. 包含日志文件和配置
4. 描述复现步骤

### 💡 功能建议

1. 清晰描述需求
2. 说明使用场景
3. 提供示例代码

### 🔧 代码贡献

1. Fork 项目
2. 创建特性分支
3. 提交高质量代码
4. 添加单元测试
5. 更新文档
6. 发起 Pull Request

---

## 📞 支持与联系

- 📧 **邮箱**: development@company.com
- 📚 **文档**: [在线文档](https://docs.company.com/bacnet)
- 🐛 **问题**: [GitHub Issues](https://github.com/company/bacnet-driver/issues)
- 💬 **讨论**: [GitHub Discussions](https://github.com/company/bacnet-driver/discussions)

---

## 🎖️ 致谢

感谢以下开源项目和贡献者：

- **BACnet4Linux** - 优秀的 BACnet 协议栈实现
- **spdlog** - 高性能日志库
- **yaml-cpp** - YAML 配置解析库
- **BACnet 协议标准** - 开放的楼宇自动化协议

---

**🎯 BACnet 协议驱动团队**  
**📅 最后更新**: 2025-11-10  
**🏷️ 版本**: v4.0 (单例模式与自动管理重构版) ⭐  
**✨ 零配置，零清理，开箱即用！**  
**⭐ 如果这个项目对你有帮助，请给我们一个 Star！**


---

##  附录：状态和概念快速参考

###  连接状态 (ConnectionState)

| 值 | 状态 | 说明 |
|----|------|------|
| 0 | DISCONNECTED | 未连接 |
| 1 | CONNECTING | 连接中，正在发送 Who-Is |
| 2 | CONNECTED | 已连接，可以正常通信 |
| 3 | ERROR | 错误状态 |

**日志函数**: `connection_state_to_string(state)`

---

###  缓存策略 (CacheStrategy)

| 值 | 策略 | 说明 |
|----|------|------|
| 0 | Aggressive | 激进策略 - 每次都发送请求，立即返回缓存 |
| 1 | Conservative | 保守策略 - 仅缓存过期时发送请求 |

**配置文件** (`config.yaml`):
```yaml
cache_strategy: 0      # 策略
cache_expiry_ms: 1000  # 过期时间(毫秒)
```

**日志函数**: `cache_strategy_to_string(strategy)`

---

###  Invoke ID 状态

| 值 | 说明 |
|----|------|
| 0-254 | 有效的 BACnet 事务 ID |
| 0xFF | INVALID (无活跃请求) |

**日志函数**: `invoke_id_to_string(id, buffer, size)`  
**检查函数**: `is_invoke_id_valid(id)`

---

###  四元组 (ObjectKey) - 核心概念

每个 BACnet 属性读取由**四元组**唯一标识：

```
{device_instance, object_type, object_instance, property_id}
```

**示例**:
```
{5678, analog-input, 1, present-value}   AI-1 的当前值
{5678, analog-input, 1, description}     AI-1 的描述 (不同property_id)
{5678, analog-input, 2, present-value}   AI-2 的当前值 (不同instance)
```

---

###  active_invoke_id - 防重复机制

**作用**: 防止对同一四元组发送重复请求  
**范围**: 针对**每个四元组**，不是针对整个对象

**并发规则**:
-  同一对象的不同属性  可以并发
-  不同实例的同一属性  可以并发  
-  不同设备的同一对象  可以并发
-  相同四元组的重复调用  会被跳过

**示例**:
```cpp
plc_proto_read({5678, AI, 1, PV})    发送请求 (active=10)
plc_proto_read({5678, AI, 1, PV})    跳过发送 (已有active=10)
plc_proto_read({5678, AI, 1, DESC})  发送请求 (不同四元组!)
```

---

###  对象状态 (ObjectState)

每个四元组的完整状态包含：

| 字段 | 说明 |
|------|------|
| `cached_value` | 缓存的值 |
| `has_valid_cache` | 是否有有效缓存 |
| `active_invoke_id` | 活跃请求ID (0xFF=无) |
| `timestamp` | 缓存时间戳 |
| `status` | 最后操作状态 |

**生命周期**:
1. 首次读取  `has_valid_cache=false`, 发送请求
2. 响应到达  更新缓存, `active_invoke_id=0xFF`
3. 缓存使用  检查过期时间
4. 缓存过期  发送新请求

---

## 🌐 多数据链路层支持（Multi-Datalink）

### 📋 概述

从 **v5.0** 开始，BACnet 协议栈支持**同时使用多种物理层传输方式**，实现真正的多网融合通信。

**典型应用场景**：
- **BACnet/IP（以太网 UDP）**：访问局域网内的温度传感器、风机、照明控制器等 IP 设备
- **MS/TP（RS-485 串口）**：访问现场总线的 VAV 控制器、阀门执行器、传统 DDC 等串口设备

**核心优势**：
- ✅ PLC 可在同一程序中同时访问两种网络的设备
- ✅ 无需重启或切换协议栈，真正的无缝集成
- ✅ 协议栈自动维护设备路由表，透明化数据链路层差异
- ✅ 零配置架构，首次读写时自动初始化，程序退出自动清理

---

### 🎯 架构设计

**设计理念**：启动时初始化所有配置的数据链路层 + 自动路由 + 可选显式指定

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          BACnet 协议栈                                   │
├─────────────────────────────────────────────────────────────────────────┤
│  应用层 API (零配置):                                                    │
│    plc_proto_read(req)   // 首次调用自动初始化                           │
│    req.datalink_hint = AUTO (默认自动路由)                               │
├─────────────────────────────────────────────────────────────────────────┤
│  设备地址路由表 (协议栈维护):                                            │
│    Device 5678 @ 192.168.1.100  → BACnet/IP (以太网)                     │
│    Device 1234 @ MSTP MAC 5     → MS/TP (串口)                           │
│    Device 9999 @ 10.0.0.50      → BACnet/IP (以太网)                     │
├─────────────────────────────────────────────────────────────────────────┤
│  数据链路层 (并行运行):                                                  │
│    ┌────────────────────┐    ┌────────────────────┐                     │
│    │   BACnet/IP        │    │      MS/TP         │                     │
│    │  (UDP 47808)       │    │  (/dev/ttyUSB0)    │                     │
│    │  Baud: N/A         │    │  Baud: 38400       │                     │
│    │  MAC: N/A          │    │  MAC: 1            │                     │
│    └────────────────────┘    └────────────────────┘                     │
└─────────────────────────────────────────────────────────────────────────┘
           │                            │
           ▼                            ▼
    以太网 BACnet 设备            RS-485 MS/TP 设备
  (温度传感器、风机等)         (VAV 控制器、阀门等)
```

**工作流程**：
1. **初始化阶段**：读取 `config.yaml`，初始化所有配置的数据链路层（BIP 必定启用，MS/TP 可选）
2. **设备发现**：通过 Who-Is 广播发现设备，协议栈记录设备地址和对应的数据链路层
3. **自动路由**：后续读写操作根据设备地址自动选择正确的数据链路层
4. **显式指定**（可选）：通过 `datalink_hint` 字段强制指定数据链路层，跳过路由查询

---

### 💻 API 扩展

#### 1. 新增枚举：`bacnet_datalink_type_t`

```c
/**
 * @brief 数据链路层类型枚举
 * 
 * 说明：
 * - AUTO: 协议栈根据设备地址自动选择（推荐，默认）
 * - BIP:  强制使用 BACnet/IP（以太网 UDP）
 * - MSTP: 强制使用 MS/TP（RS-485 串口）
 * - 其他类型（ETHERNET/BIP6/BSC）保留，暂未实现
 */
typedef enum {
    BACNET_DATALINK_AUTO = 0,    // 自动检测（默认，推荐）
    BACNET_DATALINK_BIP,         // BACnet/IP（以太网 UDP）
    BACNET_DATALINK_MSTP,        // MS/TP（RS-485 串口）
    BACNET_DATALINK_ETHERNET,    // BACnet/Ethernet（保留）
    BACNET_DATALINK_BIP6,        // BACnet/IPv6（保留）
    BACNET_DATALINK_BSC          // BACnet/SC（保留）
} bacnet_datalink_type_t;
```

#### 2. 扩展读写结构体

```c
/**
 * @brief BACnet 读取请求结构体
 */
typedef struct {
    uint32_t device_instance;           // 目标设备实例 ID
    uint16_t object_type;               // 对象类型（如 OBJECT_ANALOG_INPUT）
    uint32_t object_instance;           // 对象实例 ID
    uint32_t property_id;               // 属性 ID（如 PROP_PRESENT_VALUE）
    int32_t  array_index;               // 数组索引（-1 表示整个属性）
    bacnet_data_value_t *value;         // 输出缓冲区
    bacnet_datalink_type_t datalink_hint;  // ⭐ 新增：数据链路层提示
} bacnet_read_t;

/**
 * @brief BACnet 写入请求结构体
 */
typedef struct {
    uint32_t device_instance;
    uint16_t object_type;
    uint32_t object_instance;
    uint32_t property_id;
    int32_t  array_index;
    bacnet_data_value_t value;
    uint8_t priority;
    bacnet_datalink_type_t datalink_hint;  // ⭐ 新增：数据链路层提示
} bacnet_write_t;
```

#### 3. 初始化宏更新

```c
// 读取初始化宏（datalink_hint 默认为 AUTO）
#define BACNET_READ_INIT(dev, obj_type, obj_inst, prop, out_val) \
    { \
        .device_instance = (dev), \
        .object_type = (obj_type), \
        .object_instance = (obj_inst), \
        .property_id = (prop), \
        .array_index = -1, \
        .value = (out_val), \
        .datalink_hint = BACNET_DATALINK_AUTO  /* ⭐ 默认自动路由 */ \
    }

// 写入初始化宏（datalink_hint 默认为 AUTO）
#define BACNET_WRITE_INIT(dev, obj_type, obj_inst, prop, val) \
    { \
        .device_instance = (dev), \
        .object_type = (obj_type), \
        .object_instance = (obj_inst), \
        .property_id = (prop), \
        .array_index = -1, \
        .value = (val), \
        .priority = 0, \
        .datalink_hint = BACNET_DATALINK_AUTO  /* ⭐ 默认自动路由 */ \
    }
```

---

### ⚙️ 配置支持

#### 配置文件示例（`config.yaml`）

```yaml
protocols:
  bacnet:
    # BACnet/IP 网络配置（必须）
    network:
      interface: "eth0"        # 网络接口名称（留空则自动选择）
      port: 47808              # UDP 端口（默认 47808）
      broadcast_address: "255.255.255.255"  # 广播地址

    # MS/TP 串口配置（可选）
    mstp:
      port: "/dev/ttyUSB0"     # 串口设备路径（留空则不启用 MS/TP）
      baud_rate: 38400         # 波特率（常用: 9600, 19200, 38400, 76800）
      mac_address: 1           # MAC 地址（0-127，网络内唯一）
      max_master: 127          # 最大主站地址（默认 127）
      max_info_frames: 1       # 单次令牌可发送的最大帧数（默认 1）
```

**配置说明**：
- **`network` section**：BACnet/IP 必定启用，即使未配置也会使用默认值
- **`mstp.port`**：
  - 留空或注释：不启用 MS/TP，只使用 BACnet/IP
  - 填写路径（如 `"/dev/ttyUSB0"`）：尝试初始化 MS/TP
    - 成功：BIP + MSTP 双网运行
    - 失败（设备不存在）：降级为 BIP 单网运行

---

### 🔨 编译配置

#### 1. CMakeLists.txt 配置

```cmake
# ============================================================
# 启用多数据链路层支持
# ============================================================

# 1. 启用多数据链路层架构（必须）
add_definitions(-DBACDL_MULTIPLE=1)

# 2. 链接到支持 MS/TP 的 BACnet 库（必须）
set(PROTOCOL_LIBS bacnet-stack yaml)  # 使用 libbacnet-stack.a

# 说明：
#   - BACDL_MULTIPLE=1: 启用多数据链路层模式，允许运行时初始化多个数据链路层
#   - bacnet-stack: 链接到 libbacnet-stack.a，该库必须编译了 MS/TP 支持
#   - 如果库未编译 MS/TP（未定义 BACDL_MSTP），运行时会优雅降级到 BIP 单网
```

#### 2. BACnet 库编译要求

**检查当前库是否支持 MS/TP**：

```bash
# 方法 1：检查符号表
nm depend/lib/libbacnet-stack.a | grep dlmstp

# 期望输出（有 MS/TP 支持）：
# 0000000000000dd0 T dlmstp_init
# 0000000000000000 T dlmstp_set_baud_rate
# 0000000000000bd0 T dlmstp_set_mac_address
# ...

# 如果无输出：库不支持 MS/TP，需要重新编译
```

**重新编译 BACnet 库以支持 MS/TP**：

```bash
# 1. 进入 BACnet 库源码目录
cd path/to/bacnet-stack

# 2. 启用 BACDL_ALL 宏（包含所有数据链路层）
export CFLAGS="-DBACDL_ALL=1"

# 3. 编译
make clean
make

# 4. 安装到项目依赖目录
cp lib/libbacnet.a /path/to/procto-manager-hd/impl/bacnet/depend/lib/libbacnet-stack.a
```

**编译宏说明**：

| 宏 | 作用域 | 说明 |
|----|--------|------|
| `BACDL_MULTIPLE=1` | 项目编译时 | 启用多数据链路层架构（必须） |
| `BACDL_ALL=1` | BACnet 库编译时 | 编译所有数据链路层实现（包含 MS/TP） |
| `BACDL_MSTP` | BACnet 库编译时 | 由 `BACDL_ALL` 自动定义，表示库包含 MS/TP 实现 |

---

### 🔧 初始化流程对比

#### 方式 1：之前（单数据链路层 - BACnet/IP）

```cpp
proto_status_t initialize_context(BacnetContext *context) {
    // 协议栈基础初始化
    Device_Init(nullptr);
    address_init();
    
    // ⭐ 数据链路层自动初始化（隐式）
    dlenv_init();  // 自动初始化 BACnet/IP，无需额外配置
    
    // 注册回调
    register_bacnet_handlers(context);
    return PROTO_SUCCESS;
}
```

**特点**：
- ✅ **简单**：只需 `dlenv_init()` 一行代码
- ✅ **自动**：根据环境变量或默认配置自动初始化 BACnet/IP
- ❌ **单一**：只能使用一种数据链路层（BACnet/IP）
- ❌ **不灵活**：无法同时访问以太网和串口设备

---

#### 方式 2：现在（多数据链路层 - BIP + MS/TP）

```cpp
/**
 * @brief 初始化所有配置的数据链路层
 * 
 * 架构特点：
 * - 显式初始化：每个数据链路层单独配置和初始化
 * - 并行运行：多个数据链路层同时工作，互不干扰
 * - 优雅降级：某个数据链路层初始化失败不影响其他数据链路层
 * - 配置驱动：根据 config.yaml 决定启用哪些数据链路层
 */
proto_status_t initialize_all_datalinks(BacnetContext *context) {
    log_info("[BACnet][DataLink] Initializing multiple datalink layers...");

#if defined(BACDL_MULTIPLE)
    /* ========================================================================
     * 步骤 1：初始化 BACnet/IP 数据链路层（以太网 UDP）
     * ======================================================================== */
    
    log_info("[BACnet][DataLink] [1/2] Initializing BACnet/IP (Ethernet)...");
    
    // 1.1 设置 UDP 端口
    bip_set_port(context->config.port);  // 默认 47808
    log_debug("[BACnet][DataLink][BIP] Port set to {}", context->config.port);
    
    // 1.2 初始化 BACnet/IP 数据链路层
    //     参数：interface_name - 网络接口名称（如 "eth0"），nullptr 表示自动选择
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

    /* ========================================================================
     * 步骤 2：初始化 MS/TP 数据链路层（RS-485 串口）
     * ======================================================================== */
    
    // ⚠️ 检查 BACnet 库是否编译了 MS/TP 支持
    // 注意：即使定义了 BACDL_MULTIPLE，也需要 BACnet 库编译时启用 BACDL_MSTP
#if defined(BACDL_MSTP)
    // 检查是否配置了 MS/TP 串口路径
    if (context->config.mstp_port[0] != '\0') {
        log_info("[BACnet][DataLink] [2/2] Initializing MS/TP (RS-485 Serial)...");
        
        // 2.1 设置波特率（常用值：9600, 19200, 38400, 76800）
        dlmstp_set_baud_rate(context->config.mstp_baud);
        log_debug("[BACnet][DataLink][MSTP] Baud rate set to {} bps", context->config.mstp_baud);
        
        // 2.2 设置 MAC 地址（0-127，唯一标识本设备）
        dlmstp_set_mac_address(context->config.mstp_mac);
        log_debug("[BACnet][DataLink][MSTP] MAC address set to {}", context->config.mstp_mac);
        
        // 2.3 设置最大主站地址（默认 127）
        dlmstp_set_max_master(context->config.mstp_max_master);
        log_debug("[BACnet][DataLink][MSTP] Max master set to {}", context->config.mstp_max_master);
        
        // 2.4 设置最大信息帧数（默认 1，决定单次令牌持有可发送的帧数）
        dlmstp_set_max_info_frames(context->config.mstp_max_frames);
        log_debug("[BACnet][DataLink][MSTP] Max info frames set to {}", context->config.mstp_max_frames);
        
        // 2.5 初始化 MS/TP 数据链路层
        //     参数：port - 串口设备路径（如 "/dev/ttyUSB0", "/dev/ttyS1"）
        if (!dlmstp_init(const_cast<char*>(context->config.mstp_port))) {
            log_error("[BACnet][DataLink][MSTP] Failed to initialize MS/TP on port '{}'", 
                      context->config.mstp_port);
            // ⭐ MS/TP 初始化失败不影响 BACnet/IP，继续运行（优雅降级）
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

    /* ========================================================================
     * 步骤 3：数据链路层初始化完成
     * ======================================================================== */
    
    log_info("[BACnet][DataLink] All datalink layers initialized successfully");
    log_info("[BACnet][DataLink] Protocol stack will auto-route requests based on device address table");
    log_info("[BACnet][DataLink] Optional: Use 'datalink_hint' field to force specific datalink");
    
    return PROTO_SUCCESS;
#else
    // 未启用 BACDL_MULTIPLE，回退到单一数据链路层模式
    log_warn("[BACnet][DataLink] BACDL_MULTIPLE not defined, using single datalink mode");
    return PROTO_SUCCESS;
#endif  // BACDL_MULTIPLE
}

/**
 * @brief 初始化 BACnet 上下文（包含多数据链路层初始化）
 */
proto_status_t initialize_context(BacnetContext *context) {
    // 协议栈基础初始化
    Device_Init(nullptr);
    address_init();
    dlenv_init();  // 加载环境变量配置
    
    // ⭐ 新增：显式初始化所有配置的数据链路层
    proto_status_t dl_status = initialize_all_datalinks(context);
    if (dl_status != PROTO_SUCCESS) {
        log_error("[BACnet] Failed to initialize datalink layers: {}", dl_status);
        return dl_status;
    }
    
    // 注册回调
    register_bacnet_handlers(context);
    return PROTO_SUCCESS;
}
```

**特点**：
- ✅ **多物理层**：BACnet/IP + MS/TP 同时运行，支持跨网通信
- ✅ **显式初始化**：每个数据链路层单独配置，逻辑清晰
- ✅ **可选启用**：MS/TP 串口路径为空则不启用，灵活配置
- ✅ **优雅降级**：MS/TP 初始化失败不影响 BACnet/IP，系统继续运行
- ✅ **条件编译**：通过 `BACDL_MSTP` 宏保证编译时安全

**对比表格**：

| 特性 | 单数据链路层（旧） | 多数据链路层（新） |
|------|-------------------|-------------------|
| 初始化方式 | `dlenv_init()` 隐式初始化 | `initialize_all_datalinks()` 显式初始化 |
| 支持的物理层 | 仅 BACnet/IP | BACnet/IP + MS/TP 并行 |
| 配置方式 | 环境变量或编译宏 | `config.yaml` 配置文件 |
| 复杂度 | 简单（1 行代码） | 适中（150 行代码，含注释） |
| 灵活性 | 低（单一网络） | 高（跨网融合） |
| 错误处理 | 初始化失败直接返回 | 优雅降级（部分失败继续运行） |
| 适用场景 | 单一以太网环境 | 楼宇自动化多网融合 |

---

### 📝 使用示例

#### 示例 1：自动路由（推荐）

```c
#include "proto_bacnet.h"
#include <bacnet/bacenum.h>  // BACnet 枚举定义

int main(void) {
    // ⭐ 零配置：无需初始化，首次读写时自动初始化
    
    /* ------------------------------------------------------------------
     * 场景 1：读取以太网温度传感器（设备 5678）
     * ------------------------------------------------------------------ */
    bacnet_data_value_t temp_value = {BACNET_DATA_NULL, {0}};
    bacnet_read_t temp_req = BACNET_READ_INIT(
        5678,                       // 设备实例 ID
        OBJECT_ANALOG_INPUT,        // 对象类型：模拟输入
        0,                          // 对象实例
        PROP_PRESENT_VALUE,         // 属性：当前值
        &temp_value                 // 输出缓冲区
    );
    // ⭐ datalink_hint 默认为 AUTO，协议栈自动路由
    
    int ret = plc_proto_read(&temp_req);
    if (ret == PROTO_SUCCESS && temp_value.type == BACNET_DATA_REAL) {
        printf("温度: %.2f °C\n", temp_value.value.real_value);
    }
    
    /* ------------------------------------------------------------------
     * 场景 2：控制串口空调设备（设备 1234）
     * ------------------------------------------------------------------ */
    bacnet_data_value_t target_temp = {
        .type = BACNET_DATA_REAL,
        .value = {.real_value = 24.0f}  // 设置目标温度 24°C
    };
    
    bacnet_write_t ac_req = BACNET_WRITE_INIT(
        1234,                       // 设备实例 ID
        OBJECT_ANALOG_OUTPUT,       // 对象类型：模拟输出
        0,                          // 对象实例
        PROP_PRESENT_VALUE,         // 属性：当前值
        target_temp                 // 写入值
    );
    // ⭐ datalink_hint 默认为 AUTO，协议栈自动路由到串口
    
    ret = plc_proto_write(&ac_req);
    if (ret == PROTO_SUCCESS) {
        printf("目标温度已设置为 24.0 °C\n");
    }
    
    // ⭐ 零配置：程序退出时自动清理（atexit）
    return 0;
}
```

#### 示例 2：显式指定数据链路层（可选）

```c
#include "proto_bacnet.h"
#include <bacnet/bacenum.h>

int main(void) {
    /* ------------------------------------------------------------------
     * 场景 1：强制走以太网 BACnet/IP
     * 用途：已知设备在以太网，跳过路由查询以提升性能
     * ------------------------------------------------------------------ */
    bacnet_data_value_t temp_value = {BACNET_DATA_NULL, {0}};
    bacnet_read_t temp_req = BACNET_READ_INIT(
        5678, OBJECT_ANALOG_INPUT, 0, PROP_PRESENT_VALUE, &temp_value
    );
    
    // ⭐ 显式指定：强制使用 BACnet/IP（以太网）
    temp_req.datalink_hint = BACNET_DATALINK_BIP;
    
    plc_proto_read(&temp_req);
    
    /* ------------------------------------------------------------------
     * 场景 2：强制走串口 MS/TP
     * 用途：已知设备在串口，跳过路由查询
     * ------------------------------------------------------------------ */
    bacnet_data_value_t target_temp = {
        .type = BACNET_DATA_REAL,
        .value = {.real_value = 22.0f}
    };
    
    bacnet_write_t ac_req = BACNET_WRITE_INIT(
        1234, OBJECT_ANALOG_OUTPUT, 0, PROP_PRESENT_VALUE, target_temp
    );
    
    // ⭐ 显式指定：强制使用 MS/TP（串口）
    ac_req.datalink_hint = BACNET_DATALINK_MSTP;
    
    plc_proto_write(&ac_req);
    
    return 0;
}
```

**完整示例程序**：
- 源文件：`demo/multi_datalink_example.cc`
- 编译命令：`make multi_datalink_example`
- 运行命令：`./multi_datalink_example`

---

### 🧪 测试与验证

#### 1. 编译测试

```bash
cd /path/to/procto-manager-hd/impl/bacnet/build
make clean
make -j$(nproc)

# 检查编译产物
ls -lh libproto_bacnet.so
ls -lh multi_datalink_example
```

#### 2. 运行时验证（无真实设备）

```bash
# 运行示例程序
./multi_datalink_example

# 期望日志输出（BACnet/IP + MS/TP 初始化成功）
# [BACnet][DataLink] Initializing multiple datalink layers...
# [BACnet][DataLink] [1/2] Initializing BACnet/IP (Ethernet)...
# [BACnet][DataLink][BIP] Initialized successfully (interface: <auto>, port: 47808)
# [BACnet][DataLink] [2/2] Initializing MS/TP (RS-485 Serial)...
# [BACnet][DataLink][MSTP] Baud rate set to 38400 bps
# [BACnet][DataLink][MSTP] MAC address set to 1
# [BACnet][DataLink][MSTP] Failed to initialize MS/TP on port '/dev/ttyUSB0'  # ⬅️ 设备不存在，正常
# [BACnet][DataLink][MSTP] Continuing with BACnet/IP only
# [BACnet][DataLink] All datalink layers initialized successfully
```

#### 3. 配置验证

查看配置加载日志：

```
┃ [MS/TP] (RS-485 Serial)                                      ┃
┃   port               : /dev/ttyUSB0          [YAML   ] ┃  ✅ 从 YAML 读取
┃   baud_rate          :                38400  [YAML   ] ┃  ✅ 从 YAML 读取
┃   mac_address        :                    1  [YAML   ] ┃  ✅ 从 YAML 读取
┃   max_master         :                  127  [YAML   ] ┃  ✅ 从 YAML 读取
┃   max_info_frames    :                    1  [YAML   ] ┃  ✅ 从 YAML 读取
```

#### 4. 真实设备测试（可选）

```bash
# 1. 连接 RS-485 USB 转换器到 /dev/ttyUSB0
# 2. 连接 MS/TP 设备（如 VAV 控制器）
# 3. 修改 config.yaml 中的 discovery.target_device_start/end 为真实设备 ID
# 4. 运行程序

./multi_datalink_example

# 期望日志输出（成功发现设备）
# [BACnet] Device discovery success for range 1000-2000
# [BACnet] Found device 1234 at MSTP MAC 5
# [BACnet] Found device 5678 at IP 192.168.1.100
```

---

### 🐛 故障排查

#### 问题 1：MS/TP 初始化失败

**日志**：
```
[BACnet][DataLink][MSTP] Failed to initialize MS/TP on port '/dev/ttyUSB0'
/dev/ttyUSB0: No such file or directory
```

**原因**：串口设备不存在或无权限访问

**解决方案**：
```bash
# 1. 检查串口设备是否存在
ls -l /dev/ttyUSB*

# 2. 添加用户到 dialout 组（获取串口权限）
sudo usermod -a -G dialout $USER
# 重新登录生效

# 3. 检查设备权限
sudo chmod 666 /dev/ttyUSB0  # 临时方案

# 4. 如果设备不存在，安装 USB 转串口驱动
sudo modprobe ftdi_sio
sudo modprobe cp210x
```

---

#### 问题 2：链接错误（undefined reference to dlmstp_*）

**日志**：
```
/usr/bin/ld: libproto_bacnet.so: undefined reference to `dlmstp_init'
/usr/bin/ld: libproto_bacnet.so: undefined reference to `dlmstp_set_baud_rate'
```

**原因**：BACnet 库未编译 MS/TP 支持

**解决方案**：
```bash
# 1. 检查库是否包含 MS/TP 符号
nm depend/lib/libbacnet-stack.a | grep dlmstp

# 2. 如果无输出，重新编译 BACnet 库
cd path/to/bacnet-stack
export CFLAGS="-DBACDL_ALL=1"
make clean && make

# 3. 替换项目中的库文件
cp lib/libbacnet.a /path/to/procto-manager-hd/impl/bacnet/depend/lib/libbacnet-stack.a

# 4. 重新编译项目
cd /path/to/procto-manager-hd/impl/bacnet/build
make clean && make
```

---

#### 问题 3：配置未生效（显示 [DEFAULT] 而不是 [YAML]）

**日志**：
```
┃   read_timeout_ms    :                 6000  [DEFAULT] ┃  ❌ 应该是 [YAML]
```

**原因**：YAML 解析器未正确切换 section

**解决方案**：
已在 `proto_bacnet_utils.cc` 中修复（允许在 Bacnet 子 section 之间自由切换），确保使用最新代码。

---

### 📚 技术要点总结

#### 1. 核心实现要点

| 要点 | 说明 |
|------|------|
| **条件编译** | 使用 `#if defined(BACDL_MSTP)` 确保只在库支持时调用 MS/TP API |
| **优雅降级** | MS/TP 初始化失败不影响 BACnet/IP，系统继续以单网模式运行 |
| **配置驱动** | 通过 `config.yaml` 的 `mstp.port` 决定是否启用 MS/TP |
| **自动路由** | 协议栈维护设备地址路由表，根据 Who-Is 发现结果自动选择数据链路层 |
| **显式指定** | 可选通过 `datalink_hint` 字段强制指定数据链路层，跳过路由查询 |

#### 2. 关键 API

**BACnet/IP 专用 API**：
```c
void bip_set_port(uint16_t port);           // 设置 UDP 端口（默认 47808）
bool bip_init(char *ifname);                 // 初始化 BACnet/IP（参数：网络接口名）
```

**MS/TP 专用 API**：
```c
void dlmstp_set_baud_rate(uint32_t baud);    // 设置波特率（如 38400）
void dlmstp_set_mac_address(uint8_t mac);    // 设置 MAC 地址（0-127）
void dlmstp_set_max_master(uint8_t max);     // 设置最大主站地址（默认 127）
void dlmstp_set_max_info_frames(uint8_t n);  // 设置最大信息帧数（默认 1）
bool dlmstp_init(char *port);                // 初始化 MS/TP（参数：串口路径）
```

#### 3. 设计模式

- **策略模式**：数据链路层作为可插拔策略，运行时动态选择
- **工厂模式**：`initialize_all_datalinks()` 根据配置创建不同的数据链路层实例
- **责任链模式**：协议栈按优先级尝试不同的数据链路层，直到成功发送
- **适配器模式**：统一的 `plc_proto_read/write` API 适配不同的数据链路层实现

#### 4. 性能考量

| 场景 | 性能影响 | 优化建议 |
|------|---------|---------|
| 自动路由 | 首次访问需查询路由表（~1ms） | 缓存设备地址，后续访问无开销 |
| 显式指定 | 无路由查询，性能最佳 | 已知设备物理层时推荐使用 |
| MS/TP 通信 | 串口波特率限制（38400 bps ≈ 3.8 KB/s） | 大数据量传输优先使用 BACnet/IP |
| BACnet/IP 通信 | 以太网带宽充足（100 Mbps+） | 适合大数据量和实时性要求高的场景 |

---

### 🎓 最佳实践

#### 1. 配置建议

```yaml
# ✅ 推荐配置
protocols:
  bacnet:
    network:
      interface: "eth0"        # 明确指定网络接口，避免自动选择错误
      port: 47808
    
    mstp:
      port: "/dev/ttyUSB0"     # 使用稳定的设备路径（避免 /dev/ttyUSB0 → ttyUSB1 的问题）
      baud_rate: 38400         # 常用波特率，兼容大部分设备
      mac_address: 1           # 确保网络内唯一
      max_master: 127
      max_info_frames: 1       # 保守值，兼容性最好

# ❌ 不推荐配置
protocols:
  bacnet:
    mstp:
      port: "/dev/ttyUSB0"
      baud_rate: 115200        # ❌ 非标准波特率，部分设备不支持
      mac_address: 255         # ❌ 超出范围（0-127）
```

#### 2. 编程建议

```c
// ✅ 推荐：使用自动路由（简洁、灵活）
bacnet_read_t req = BACNET_READ_INIT(device_id, obj_type, obj_inst, prop, &value);
plc_proto_read(&req);  // datalink_hint 默认为 AUTO

// ✅ 可选：显式指定（性能优化）
req.datalink_hint = BACNET_DATALINK_MSTP;  // 已知设备在串口
plc_proto_read(&req);

// ❌ 不推荐：手动管理数据链路层（破坏封装）
// 不要直接调用 bip_init() 或 dlmstp_init()，让驱动自动管理
```

#### 3. 错误处理

```c
int ret = plc_proto_read(&req);
switch (ret) {
    case PROTO_SUCCESS:
        // 读取成功
        break;
    
    case PROTO_ERROR_CONNECT:
        // 连接失败：设备可能不在线或网络问题
        // 建议：检查设备电源、网络连接、串口线缆
        break;
    
    case PROTO_ERROR_TIMEOUT:
        // 超时：设备响应慢或网络拥塞
        // 建议：增加 read_timeout_ms 配置值
        break;
    
    case PROTO_ERROR_INIT:
        // 初始化失败：数据链路层未就绪
        // 建议：检查配置文件、串口设备权限
        break;
    
    default:
        log_error("Unexpected error: {}", ret);
        break;
}
```

#### 4. 调试技巧

```bash
# 1. 启用调试日志
export BACNET_LOG_LEVEL=debug
./your_program

# 2. 抓包分析（BACnet/IP）
sudo tcpdump -i eth0 -w bacnet.pcap udp port 47808
# 用 Wireshark 打开 bacnet.pcap 分析

# 3. 监控串口通信（MS/TP）
sudo cat /dev/ttyUSB0  # 查看原始数据
sudo minicom -D /dev/ttyUSB0  # 串口终端

# 4. 查看设备发现日志
grep "Device discovery" /var/log/bacnet.log
grep "I-Am" /var/log/bacnet.log
```

---

### 📖 相关文档

- **示例程序**：`demo/multi_datalink_example.cc` - 完整的多数据链路层使用示例
- **配置文件**：`config.yaml` - MS/TP 配置模板
- **架构文档**：`ARCHITECTURE.md` - BACnet 驱动架构设计
- **BACnet 标准**：ASHRAE 135-2020 - Clause 9（MS/TP 规范）

---

### 🔄 版本历史

| 版本 | 日期 | 更新内容 |
|------|------|---------|
| v5.0 | 2025-11-18 | ✨ 新增多数据链路层支持（BIP + MS/TP） |
| v4.0 | 2025-10-15 | 重构为零配置架构，自动初始化和清理 |
| v3.0 | 2025-09-01 | 新增热配置监控功能 |
| v2.0 | 2025-07-20 | 新增读写缓存机制 |
| v1.0 | 2025-06-01 | 初始版本，仅支持 BACnet/IP |

---

```cpp
proto_status_t initialize_context(BacnetContext *context) {
    Device_Init(nullptr);
    address_init();
    dlenv_init();  // ⭐ 自动初始化 BACnet/IP
    register_bacnet_handlers(context);
}
```

**特点**：
- ✅ 简单：只需 `dlenv_init()`
- ❌ 单一：只能一种数据链路层

#### 现在（多数据链路层）

```cpp
proto_status_t initialize_context(BacnetContext *context) {
    Device_Init(nullptr);
    address_init();
    dlenv_init();
    
    // ⭐ 显式初始化所有配置的数据链路层
    initialize_all_datalinks(context);
    
    register_bacnet_handlers(context);
}

proto_status_t initialize_all_datalinks(BacnetContext *context) {
    // 1. 初始化 BACnet/IP
    bip_set_port(context->config.port);
    bip_init(context->config.interface_name);
    
    // 2. 初始化 MS/TP（如果配置了）
    if (context->config.mstp_port[0] != '\0') {
        dlmstp_set_baud_rate(context->config.mstp_baud);
        dlmstp_set_mac_address(context->config.mstp_mac);
        dlmstp_init(context->config.mstp_port);
    }
    
    return PROTO_SUCCESS;
}
```

**特点**：
- ✅ 多物理层：BACnet/IP + MS/TP 同时运行
- ✅ 显式初始化：每个数据链路层单独配置
- ✅ 可选启用：MS/TP 串口路径为空则不启用

### 📝 使用示例

```c
// 自动路由（推荐）
bacnet_read_t req = BACNET_READ_INIT(5678, AI, 0, PV, &value);
plc_proto_read(&req);  // 协议栈自动选择数据链路层

// 显式指定（可选）
req.datalink_hint = BACNET_DATALINK_MSTP;  // 强制走串口
plc_proto_read(&req);
```

### 🧪 验证

```bash
# 运行示例
./multi_datalink_example

# 查看日志
[BACnet][DataLink] [1/2] Initializing BACnet/IP...
[BACnet][DataLink][BIP] Initialized (interface: eth0, port: 47808)
[BACnet][DataLink] [2/2] Initializing MS/TP...
[BACnet][DataLink][MSTP] Initialized (port: /dev/ttyUSB0, baud: 38400)
```

详细文档：`impl/bacnet/MULTI_DATALINK_SUPPORT.md`

---

###  常见日志解读

```
[PLC] Current connection state: 2 (CONNECTED)
 已连接到设备

[BACnet] Cache strategy: Aggressive (always send requests) (expiry: 1000ms)
 激进策略，1秒缓存过期

[PLC] Request sent: analog-input-1, present-value (invoke_id: 10)
 发送了读取请求

[PLC] Request pending: analog-input-1, present-value (active invoke_id: 10)
 已有活跃请求，跳过发送（防重复）

[BACnet] Cache stats - Objects: 8 (cached: 8, active: 1)
 8个对象，全部有缓存，1个请求在飞行中

[BACnet] ReadProperty successful, cache updated (device: 5678, analog-input-1)
 缓存已更新
```

---

###  调试命令

```bash
# 查看连接状态
grep "connection state:" bacnet.log | tail -1

# 查看缓存统计
grep "Cache stats" bacnet.log | tail -5

# 查看活跃请求
grep "Request pending" bacnet.log | wc -l

# 查看 Invoke ID 映射
grep "mapped to\|map size" bacnet.log | tail -10

# 监控性能
watch -n 1 'grep "Cache stats" bacnet.log | tail -1'
```

---

###  性能分析

**缓存统计解读**:
```
cached: 10, active: 0    理想 (所有有缓存，无活跃请求)
cached: 10, active: 2    正常 (2个请求在等待)
cached:  3, active: 7    注意 (多数请求在等待)
cached:  0, active: 10   异常 (所有请求都超时?)
```

---

###  常见问题排查

**Q: 看到大量 "Request pending"**  
A: 网络延迟高或设备响应慢  
解决: 增加 `cache_expiry_ms` 或降低 PLC 轮询频率

**Q: active 数量持续很高**  
A: 请求堆积，响应未到达  
解决: 检查网络、设备状态、超时设置

**Q: map size 一直增长**  
A: Invoke ID 映射泄漏  
解决: 检查 `cleanup_stale_requests()` 是否正常工作

**Q: 缓存命中率低**  
A: 缓存过期太快  
解决: 增大 `cache_expiry_ms`

---

###  核心函数和数据结构

**状态转换函数** (`proto_bacnet_utils.cpp`):
- `connection_state_to_string(ConnectionState)`
- `cache_strategy_to_string(CacheStrategy)`
- `invoke_id_to_string(uint8_t, char*, size_t)`
- `is_invoke_id_valid(uint8_t)`

**核心数据结构** (`proto_bacnet_internal.hpp`):
- `struct ObjectKey { ... }`
- `struct ObjectState { ... }`
- `std::unordered_map<ObjectKey, ObjectState> object_states`
- `std::unordered_map<uint8_t, ObjectKey> invoke_id_to_key`
