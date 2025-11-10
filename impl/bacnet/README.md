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

- **🔄 异步高效** - 非阻塞读写，事件驱动架构
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
| 🚀 **异步非阻塞** | 读写操作立即返回，通过事件获取结果 | 高并发，响应快 |
| 🎯 **接口简洁** | 只需 `plc_proto_read()` 和 `plc_proto_write()` | 易学易用 |
| 🔒 **线程安全** | 原子变量 + 互斥锁 + 条件变量 | 并发安全 |
| 💎 **现代 C++** | 智能指针、RAII、lambda、chrono | 代码优雅，内存安全 |
| 📦 **模块化** | 按功能拆分文件，职责单一 | 易维护，易扩展 |
| 🔄 **自动管理** | 自动连接、自动发现、自动重试 | 零配置使用 |
| 🔥 **热配置** | 配置文件修改后自动生效，无需重启 ⭐ NEW | 运行时动态调整 |
| 📊 **事件驱动** | 完整的事件队列和轮询机制 | 灵活的事件处理 |
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
│  2. 调用内部 C++ 实现                                             │
│  3. 阻塞等待操作完成                                              │
│  4. 返回操作结果                                                  │
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
║  │  │ 事件队列（互斥锁 + 条件变量）                        │ │    ║
║  │  │  - std::queue<bacnet_event_t> events                │ │    ║
║  │  │  - std::condition_variable event_cv                 │ │    ║
║  │  │    → 通知等待的线程                                  │ │    ║
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
3️⃣ 执行读取操作
   ├── 检查队列是否有数据
   ├── 解析目标设备地址
   ├── 设置活动操作状态
   ├── 发送 ReadProperty 请求
   └── 获取 invoke_id
    ↓
4️⃣ 异步等待响应
   ├── 工作线程接收数据包
   ├── 调用协议栈处理器
   ├── 触发回调函数
   │   handle_read_property_ack()
   │   ├── 解码响应数据
   │   ├── store_application_value()
   │   ├── 推入读队列
   │   └── 推送完成事件
   └── 主线程轮询事件
    ↓
5️⃣ 返回结果
   └── 用户获取数据
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
│  │ 1. plc_proto_read/write() ← 提交异步请求            │    │
│  │ 2. bacnet_poll_event() ← 轮询事件结果               │    │
│  │ 3. 处理返回数据                                      │    │
│  └─────────────────────────────────────────────────────┘    │
│          │                                                   │
│          │ 事件通知 (条件变量)                               │
│          ↓                                                   │
└─────────┼───────────────────────────────────────────────────┘
           │
┌──────────┼───────────────────────────────────────────────────┐
│          │         🧵 工作线程 (异步处理)                     │
│  ┌───────┴─────────────┐                                     │
│  │  worker_loop_function()                                  │
│  │  ┌─────────────────────────────────────────────────┐     │
│  │  │ 循环执行:                                        │     │
│  │  │ 1. 处理写队列请求                              │     │
│  │  │ 2. 更新定时器                                   │     │
│  │  │ 3. 检查操作超时                                 │     │
│  │  │ 4. 接收并处理数据包 ← 关键！                   │     │
│  │  │ 5. 自动重连检查                                 │     │
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
│          │ 事件推送                                          │
│          ↓                                                   │
└──────────┼───────────────────────────────────────────────────┘
           │
           └── 推送到事件队列，通知用户线程
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

**功能**：提交异步读取请求，立即返回

**参数**：
- `req` - `bacnet_read_t*` 读取请求结构体

**返回值**：
- `PROTO_SUCCESS` (0) - 请求提交成功
- 其他值 - 错误码

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

**功能**：提交异步写入请求，立即返回

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

#### `bacnet_poll_event()` - 事件轮询 📨

```c
int bacnet_poll_event(bacnet_event_t *event, uint32_t timeout_ms);
```

**功能**：轮询等待异步操作结果

**参数**：
- `event` - 事件结构体指针
- `timeout_ms` - 等待超时时间 (0=非阻塞)

**事件类型**：
```c
typedef enum {
    BACNET_EVENT_NONE = 0,           // 无事件
    BACNET_EVENT_READ_COMPLETE,      // 读取完成
    BACNET_EVENT_WRITE_COMPLETE,     // 写入完成
    BACNET_EVENT_DEVICE_DISCOVERED,  // 设备发现
    BACNET_EVENT_ERROR               // 错误事件
} bacnet_event_type_t;
```

#### `bacnet_reload_config()` - 热配置重载 🔥

```c
int bacnet_reload_config(void);
```

**功能**：重新加载配置文件，无需重启程序

---

## 🔧 配置说明

### 📄 配置文件位置

```
/usr/runtime/protocol/bacnet/config.yaml
```

### ⚙️ 配置参数详解

```yaml
common:
  environment: production          # 运行环境
  log_level: info                  # 日志级别 (debug/info/warn/error)
  log_file: bacnet.log             # 日志文件路径

bacnet:
  enabled: true                    # 是否启用 BACnet 功能
  
  discovery:
    target_device_start: 5678      # 🎯 目标设备实例范围起始
    target_device_end: 5678        # 🎯 目标设备实例范围结束
    whois_retry: 3                 # 🔄 Who-Is 重试次数
    response_timeout_ms: 5000      # ⏱️ I-Am 响应超时时间
  
  local_device:
    instance_id: 4194303           # 🆔 本地设备实例ID
    max_apdu: 1476                 # 📏 最大 APDU 长度
  
  network:
    interface: eth0                # 🌐 网络接口名称
    port: 47808                    # 🔌 UDP 端口
    broadcast_address: 255.255.255.255  # 📡 广播地址
  
  services:
    read_timeout_ms: 6000          # ⏱️ 读取操作超时 (API中timeout_ms=0时使用此值)
    write_timeout_ms: 6000         # ⏱️ 写入操作超时 (API中timeout_ms=0时使用此值)
    default_priority: 8            # ⭐ 默认写入优先级 (API中priority=0时使用此值)
    cache_expiry_ms: 1000          # 💾 缓存过期时间 (毫秒)
    cache_strategy: 0              # 📊 缓存策略 (0=激进,每次都发送; 1=保守,使用未过期缓存)
```

**💡 默认值说明**：
- 当 API 中 `timeout_ms = 0` 时，自动使用配置文件中的 `read_timeout_ms` 或 `write_timeout_ms`
- 当 API 中 `priority = 0` 时，自动使用配置文件中的 `default_priority`
- 当 API 中 `array_index` 不填写时，默认为 `-1` (读取整个数组)

**🎯 推荐做法**：
1. ✅ 在配置文件中设置合理的全局默认值
2. ✅ API调用时使用便捷宏 `BACNET_READ_INIT()` / `BACNET_WRITE_INIT()`
3. ✅ 只在特殊场景才手动覆盖默认值

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
#   - log_level: debug → info
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
[BACnet][HotConfig] Checking #42: mtime=1762740028, last_mtime=1762740028
[BACnet][HotConfig] Config file changed (1762740028 -> 1762741561), invoking callback
[BACnet] Config reload triggered
[BACnet] Config reload flag set, will reload on next read/write request

# 下次读写操作触发重载
[BACnet] Pending config reload detected, releasing driver...
[BACnet][Driver] Releasing driver resources...
[BACnet][Driver] Stopping hot config monitoring...
[BACnet][HotConfig] Stopping monitor thread...
[BACnet][HotConfig] Monitor thread stopped

# 重新初始化
[BACnet][Driver] Initializing driver...
[BACnet][Config] Configuration loaded from '../config.yaml'
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃          BACnet Configuration Loaded                         ┃
┃   ... (显示所有新配置) ...
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
[BACnet][Driver] Hot config monitoring started for: ../config.yaml  ← 新监控线程
[BACnet] Connected to device range 5678-5678 successfully
```

**技术细节**：

| 特性 | 说明 |
|------|------|
| 🧵 **监控线程** | 独立线程，使用 `std::thread`，RAII 自动管理 |
| ⏱️ **检查间隔** | 1000ms（可配置，见高级配置） |
| 🔒 **线程安全** | 使用 `std::atomic` + `std::mutex` 保证安全 |
| 🛡️ **死锁避免** | 延迟重载机制，监控线程不会阻塞自己 |
| 🔄 **重载方式** | 完全释放旧驱动 → 重新初始化 → 重新连接设备 |
| 📊 **性能开销** | < 0.1% CPU（每秒一次 stat() 系统调用） |
| 💾 **内存开销** | ~8KB（一个线程栈） |

**配置可修改项**：
```yaml
# config.yaml
protocols:
  bacnet:
    hot_config:
      enabled: true              # 是否启用热配置监控（默认: true）
      polling_interval_ms: 1000  # 轮询间隔（毫秒，默认: 1000ms）
                                 # 建议范围: 500-5000ms
```

**常见场景**：

1. **修改日志级别**（立即生效）：
   ```yaml
   log_level: debug  → log_level: info
   ```
   无需重启，下次请求后日志级别改变 ✅

2. **修改目标设备**（自动重新发现）：
   ```yaml
   target_device_start: 5678 → target_device_start: 5679
   ```
   自动断开旧连接，重新发现新设备 ✅

3. **修改超时时间**（立即生效）：
   ```yaml
   read_timeout_ms: 6000 → read_timeout_ms: 10000
   ```
   下次读取使用新超时时间 ✅

4. **修改缓存策略**（立即生效）：
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
    
    // 3️⃣ 提交异步读取请求
    int result = plc_proto_read(&read_req);
    if (result != PROTO_SUCCESS) {
        printf("❌ 提交读取请求失败: %d\n", result);
        return -1;
    }
    
    printf("📡 读取请求已发送，等待响应...\n");
    
    // 4️⃣ 轮询等待结果
    bacnet_event_t event;
    result = bacnet_poll_event(&event, 6000);  // 等待6秒
    
    if (result == PROTO_SUCCESS && 
        event.type == BACNET_EVENT_READ_COMPLETE &&
        event.status == PROTO_SUCCESS) {
        
        // 5️⃣ 处理读取结果
        if (read_value.type == BACNET_DATA_REAL) {
            printf("✅ 读取成功: 浮点值 = %.2f\n", read_value.value.real_value);
        } else {
            printf("✅ 读取成功: 类型=%d\n", read_value.type);
        }
    } else {
        printf("❌ 读取失败: 事件类型=%d, 状态=%d\n", event.type, event.status);
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
    
    int result = plc_proto_write(&write_req);
    if (result != PROTO_SUCCESS) {
        printf("❌ 写入请求失败\n");
        return;
    }
    
    // 等待写入完成
    bacnet_event_t event;
    if (bacnet_poll_event(&event, 6000) == PROTO_SUCCESS &&
        event.type == BACNET_EVENT_WRITE_COMPLETE) {
        if (event.status == PROTO_SUCCESS) {
            printf("✅ 写入成功\n");
        } else {
            printf("❌ 写入失败\n");
        }
    }
}
```

### 🔄 批量操作示例

```c
void batch_read_example() {
    const int NUM_POINTS = 5;
    bacnet_read_t requests[NUM_POINTS];
    bacnet_data_value_t values[NUM_POINTS];
    
    // 1️⃣ 批量提交请求
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
        
        plc_proto_read(&requests[i]);
        printf("📡 已提交读取请求 %d\n", i);
    }
    
    // 2️⃣ 收集所有结果
    int completed = 0;
    while (completed < NUM_POINTS) {
        bacnet_event_t event;
        if (bacnet_poll_event(&event, 1000) == PROTO_SUCCESS) {
            if (event.type == BACNET_EVENT_READ_COMPLETE) {
                // 找到对应的请求
                for (int i = 0; i < NUM_POINTS; i++) {
                    if (event.request == &requests[i]) {
                        if (event.status == PROTO_SUCCESS) {
                            printf("✅ AI%d = %.2f\n", i, values[i].value.real_value);
                        } else {
                            printf("❌ AI%d 读取失败\n", i);
                        }
                        completed++;
                        break;
                    }
                }
            }
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
   - 轮询 `bacnet_poll_event()`
   - 处理返回结果
   - **特点**：同步阻塞式接口

2. **🧵 工作线程 (Worker Thread)**
   - 循环接收 BACnet 数据包
   - 调用协议栈处理器
   - 检查操作超时
   - 更新 TSM 定时器
   - **特点**：异步运行，10ms轮询间隔

3. **🔥 热配置线程 (可选)**
   - 监控配置文件变化
   - 触发配置重载
   - **特点**：独立线程，1秒检查间隔

### 🔄 线程协作流程

```
用户线程                    工作线程
    │                           │
    ├─ plc_proto_read()         │
    │  └─ 提交请求到协议栈      │
    │                           ├─ 发送 ReadProperty
    │                           │
    ├─ bacnet_poll_event()      │
    │  └─ 等待事件 (阻塞)       │
    │                           ├─ 接收数据包
    │                           ├─ 处理回调
    │                           ├─ 推送事件 ──→ 通知用户线程
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
   std::mutex operation_mutex;    // 保护活动操作
   std::mutex event_mutex;        // 保护事件队列
   std::mutex read_queue_mutex;   // 保护读队列
   ```

3. **📢 条件变量 (事件通知)**
   ```cpp
   std::condition_variable event_cv;
   
   // 生产者 (工作线程)
   {
       std::lock_guard<std::mutex> lock(event_mutex);
       events.push(event);
       event_cv.notify_all();
   }
   
   // 消费者 (用户线程)
   {
       std::unique_lock<std::mutex> lock(event_mutex);
       event_cv.wait_for(lock, timeout, []{ return !events.empty(); });
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
| 📊 事件队列 | 动态增长 | std::queue实现 |

### 📈 性能优化建议

1. **🔄 批量操作** - 一次提交多个请求
2. **⚡ 非阻塞轮询** - `bacnet_poll_event(event, 0)`
3. **⏰ 合理超时** - 避免过长的超时时间
4. **📨 及时处理** - 快速消费事件队列
5. **🔍 队列检查** - 使用 `check_only` 模式避免重复请求

---

## ❓ 常见问题

### Q1: 如何判断异步操作是否完成？

```c
// 方式1: 阻塞等待 (推荐用于简单场景)
bacnet_event_t event;
int ret = bacnet_poll_event(&event, 5000);  // 等待5秒
if (ret == PROTO_SUCCESS && event.type == BACNET_EVENT_READ_COMPLETE) {
    // 处理结果
}

// 方式2: 非阻塞轮询 (推荐用于复杂场景)
while (running) {
    bacnet_event_t event;
    if (bacnet_poll_event(&event, 0) == PROTO_SUCCESS) {  // 非阻塞
        if (event.type != BACNET_EVENT_NONE) {
            // 处理事件
        }
    }
    // 执行其他任务
}
```

### Q2: 如何处理操作超时？

```c
// 设置合理的超时时间
bacnet_read_t req = {
    .timeout_ms = 3000,  // 3秒超时
    // ... 其他参数
};

// 轮询时也设置超时
bacnet_event_t event;
int ret = bacnet_poll_event(&event, 3500);  // 稍微长一点
```

### Q3: 可以同时提交多个请求吗？

```c
// 可以！异步设计支持并发操作
plc_proto_read(&req1);
plc_proto_read(&req2);
plc_proto_read(&req3);

// 然后分别处理结果
while (completed < 3) {
    bacnet_event_t event;
    bacnet_poll_event(&event, 1000);
    if (event.type == BACNET_EVENT_READ_COMPLETE) {
        // 通过 event.request 判断是哪个请求
        if (event.request == &req1) {
            // 处理 req1 的结果
        }
        completed++;
    }
}
```

### Q4: 错误码含义是什么？

```c
PROTO_SUCCESS          =  0   // ✅ 成功
PROTO_ERROR_PARAM      = -1   // ❌ 参数错误
PROTO_ERROR_INIT       = -2   // ❌ 初始化失败
PROTO_ERROR_CONNECT    = -3   // ❌ 连接失败
PROTO_ERROR_READ       = -4   // ❌ 读取失败
PROTO_ERROR_WRITE      = -5   // ❌ 写入失败
PROTO_ERROR_UNSUPPORTED = -6   // ❌ 不支持的操作
```

### Q5: 如何调试 BACnet 通信？

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

### Q6: 配置文件修改后如何生效？

```bash
# 方式1: 重启程序
# 方式2: 发送信号 (推荐)
kill -SIGUSR1 $(pidof your_program)

# 方式3: 代码调用
bacnet_reload_config();
```

### Q7: 如何处理网络故障？

```yaml
# 启用自动重连 (计划功能)
bacnet:
  discovery:
    auto_reconnect: true
    max_reconnect_attempts: 5
    reconnect_interval_ms: 5000
```

### Q8: 内存使用量大吗？

```
📊 内存分析：
- BacnetContext: ~1.2MB (包含队列和缓冲区)
- BACnet协议栈: ~800KB
- 线程栈: ~256KB × 2
- 总计: ~2.1MB

优化建议：
- 减小队列大小 (kReadQueueSize, kWriteQueueSize)
- 减少缓冲区大小
- 使用内存池管理动态分配
```

### Q9: 如何扩展新功能？

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

### 7. **std::queue 事件队列**
```cpp
// 线程安全的事件队列
std::queue<bacnet_event_t> events;

void push_event(BacnetContext *context, const bacnet_event_t &evt) {
    std::lock_guard<std::mutex> lock(context->event_mutex);
    context->events.push(evt);
    context->event_cv.notify_all();
}
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
│   ├── proto_bacnet_core.cpp          # 当前版本
│   ├── proto_bacnet_core_new.cpp      # 新版本
│   ├── proto_bacnet_core_old.cpp      # 旧版本备份
│   ├── proto_bacnet_callbacks.cpp
│   ├── proto_bacnet_discovery.cpp
│   ├── proto_bacnet_io.cpp
│   ├── proto_bacnet_utils.cpp
│   └── proto_bacnet_hot_config.cpp
├── build.sh
├── CMakeLists.txt
├── config.yaml
├── switch_version.sh
├── REFACTOR_SUMMARY.md
├── README_REFACTOR.md                 # 本文件
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
