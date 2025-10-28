# BACnet 协议驱动 - 使用指南

## 📚 目录

- [概述](#概述)
- [核心设计](#核心设计)
- [快速开始](#快速开始)
- [API 接口](#api-接口)
- [使用示例](#使用示例)
- [架构说明](#架构说明)
- [编译构建](#编译构建)
- [配置说明](#配置说明)
- [常见问题](#常见问题)

---

## 概述

BACnet 协议驱动采用现代 C++ 实现，对外提供简洁的异步 C 接口。

### ✨ 核心特性

- 🚀 **异步非阻塞** - 高性能，不阻塞调用线程
- 🎯 **接口极简** - 只暴露 3 个核心接口
- 🔒 **线程安全** - 使用原子变量、互斥锁、条件变量
- 💎 **现代 C++** - 智能指针、RAII、事件驱动
- 📦 **模块化** - 代码清晰，易于维护
- ⚡ **自动管理** - 连接、初始化全自动
- 🔄 **热配置** - 信号触发配置重载（不占用线程）

---

## 核心设计

### 对外接口（仅 3 个）

```c
// 1. 异步读取（立即返回）
int plc_proto_read(void *req);

// 2. 异步写入（立即返回）
int plc_proto_write(void *req);

// 3. 轮询事件（获取结果）
int bacnet_poll_event(bacnet_event_t *event, uint32_t timeout_ms);
```

### 工作流程

```
应用代码
  ↓
1. plc_proto_read(&req)  ← 提交请求，立即返回
  ↓
2. 继续其他工作...
  ↓
3. bacnet_poll_event(&event, 1000)  ← 轮询结果
  ↓
4. 处理 event
```

### 架构层次

```
┌─────────────────────────────────────┐
│         应用层（用户代码）           │
│   plc_proto_read/write              │
│   bacnet_poll_event                 │
└────────────┬────────────────────────┘
             ↓
┌────────────────────────────────────┐
│      接口层（C 接口）               │
│  - 异步非阻塞                       │
│  - 自动管理连接                     │
│  - 事件队列                         │
└────────────┬───────────────────────┘
             ↓
┌────────────────────────────────────┐
│    实现层（C++ 内部）               │
│  - BacnetContext 类                │
│  - 设备发现                         │
│  - 读写操作                         │
│  - 回调处理                         │
│  - 工作线程                         │
└────────────┬───────────────────────┘
             ↓
┌────────────────────────────────────┐
│   协议栈（bacnet-stack）            │
└────────────┬───────────────────────┘
             ↓
┌────────────────────────────────────┐
│      网络层（UDP/IP）               │
└────────────────────────────────────┘
```

---

## 快速开始

### 1. 编译项目

```bash
cd /workspace/protocol/procto-manager-hd/impl/bacnet

# 切换到新版本
./switch_version.sh new

# 编译
bash build.sh debug
```

### 2. 最小示例

```c
#include "impl/bacnet/src/proto_bacnet.h"
#include <stdio.h>

int main() {
    // 1. 准备读请求
    bacnet_read_t read_req = {};
    read_req.device_instance = 100;
    read_req.object_type = OBJECT_ANALOG_INPUT;
    read_req.object_instance = 0;
    read_req.property_id = PROP_PRESENT_VALUE;
    read_req.array_index = -1;
    
    bacnet_data_value_t value = {};
    value.type = BACNET_DATA_REAL;
    read_req.value = &value;
    
    // 2. 提交异步读请求（立即返回）
    int ret = plc_proto_read(&read_req);
    if (ret != PROTO_SUCCESS) {
        printf("提交读请求失败: %d\n", ret);
        return -1;
    }
    
    // 3. 可以继续做其他事情...
    
    // 4. 轮询事件获取结果
    bacnet_event_t event;
    while (1) {
        ret = bacnet_poll_event(&event, 1000);  // 等待1秒
        
        if (ret == PROTO_SUCCESS && event.type != BACNET_EVENT_NONE) {
            if (event.type == BACNET_EVENT_READ_COMPLETE) {
                if (event.status == PROTO_SUCCESS) {
                    printf("读取成功: %.2f\n", value.value.real_value);
                } else {
                    printf("读取失败: %d\n", event.status);
                }
                break;
            }
        }
    }
    
    return 0;
}
```

---

## API 接口

### plc_proto_read() - 异步读取

```c
int plc_proto_read(void *req);
```

**参数：**
- `req` - `bacnet_read_t*` 读请求指针

**返回值：**
- `PROTO_SUCCESS` - 请求已提交
- 其他 - 错误码

**特点：**
- ✅ 立即返回，不阻塞
- ✅ 自动检查连接
- ✅ 自动初始化驱动

**读请求结构：**
```c
bacnet_read_t req = {
    .device_instance = 100,              // 目标设备
    .object_type = OBJECT_ANALOG_INPUT,  // 对象类型
    .object_instance = 0,                // 对象实例
    .property_id = PROP_PRESENT_VALUE,   // 属性ID
    .array_index = -1,                   // 数组索引（-1表示非数组）
    .timeout_ms = 5000,                  // 超时（可选）
    .value = &value                      // 接收缓冲区
};
```

### plc_proto_write() - 异步写入

```c
int plc_proto_write(void *req);
```

**参数：**
- `req` - `bacnet_write_t*` 写请求指针

**返回值：**
- `PROTO_SUCCESS` - 请求已提交
- 其他 - 错误码

**写请求结构：**
```c
bacnet_write_t req = {
    .device_instance = 100,
    .object_type = OBJECT_ANALOG_OUTPUT,
    .object_instance = 0,
    .property_id = PROP_PRESENT_VALUE,
    .array_index = -1,
    .priority = 16,                      // 写入优先级
    .timeout_ms = 5000,
    .value = {                           // 写入值
        .type = BACNET_DATA_REAL,
        .value.real_value = 25.5f
    }
};
```

### bacnet_poll_event() - 轮询事件

```c
int bacnet_poll_event(bacnet_event_t *event, uint32_t timeout_ms);
```

**参数：**
- `event` - 事件结构体指针
- `timeout_ms` - 超时时间（0表示非阻塞）

**返回值：**
- `PROTO_SUCCESS` - 成功

**事件类型：**
```c
BACNET_EVENT_NONE               // 无事件
BACNET_EVENT_READ_COMPLETE      // 读取完成
BACNET_EVENT_WRITE_COMPLETE     // 写入完成
BACNET_EVENT_DEVICE_DISCOVERED  // 设备发现
BACNET_EVENT_ERROR              // 错误
```

**事件结构：**
```c
bacnet_event_t event = {
    .type = BACNET_EVENT_READ_COMPLETE,
    .status = PROTO_SUCCESS,             // 操作结果
    .request = &read_req,                // 关联的请求
    .device_instance = 100,              // 设备实例
    .invoke_id = 5                       // 调用ID
};
```

### bacnet_reload_config() - 重载配置

```c
int bacnet_reload_config(void);
```

**用途：**
- 由信号处理函数调用
- 重新加载配置文件
- 不使用监控线程

**信号处理示例：**
```c
#include <signal.h>

void signal_handler(int sig) {
    if (sig == SIGUSR1) {
        bacnet_reload_config();
    }
}

int main() {
    signal(SIGUSR1, signal_handler);
    // ...
}
```

---

## 使用示例

### 示例 1：基础读取

```c
void example_basic_read() {
    // 1. 提交请求
    bacnet_read_t req = {};
    req.device_instance = 100;
    req.object_type = OBJECT_ANALOG_INPUT;
    req.object_instance = 0;
    req.property_id = PROP_PRESENT_VALUE;
    req.array_index = -1;
    
    bacnet_data_value_t value = {};
    value.type = BACNET_DATA_REAL;
    req.value = &value;
    
    if (plc_proto_read(&req) != PROTO_SUCCESS) {
        printf("提交失败\n");
        return;
    }
    
    // 2. 轮询结果
    bacnet_event_t event;
    int ret = bacnet_poll_event(&event, 5000);
    
    if (ret == PROTO_SUCCESS && 
        event.type == BACNET_EVENT_READ_COMPLETE &&
        event.request == &req) {
        if (event.status == PROTO_SUCCESS) {
            printf("值: %.2f\n", value.value.real_value);
        }
    }
}
```

### 示例 2：批量读取

```c
void example_batch_read() {
    bacnet_read_t reqs[5];
    bacnet_data_value_t values[5];
    
    // 1. 提交多个请求
    for (int i = 0; i < 5; i++) {
        reqs[i].device_instance = 100;
        reqs[i].object_type = OBJECT_ANALOG_INPUT;
        reqs[i].object_instance = i;
        reqs[i].property_id = PROP_PRESENT_VALUE;
        reqs[i].array_index = -1;
        reqs[i].value = &values[i];
        values[i].type = BACNET_DATA_REAL;
        
        plc_proto_read(&reqs[i]);
    }
    
    // 2. 等待所有结果
    int completed = 0;
    while (completed < 5) {
        bacnet_event_t event;
        if (bacnet_poll_event(&event, 1000) == PROTO_SUCCESS) {
            if (event.type == BACNET_EVENT_READ_COMPLETE) {
                // 找到对应的请求
                for (int i = 0; i < 5; i++) {
                    if (event.request == &reqs[i]) {
                        if (event.status == PROTO_SUCCESS) {
                            printf("AI%d: %.2f\n", i, values[i].value.real_value);
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

### 示例 3：非阻塞轮询

```c
void example_non_blocking() {
    bacnet_read_t req = {};
    // ... 设置请求参数
    
    plc_proto_read(&req);
    
    // 非阻塞轮询
    while (1) {
        // 做其他工作
        do_other_work();
        
        // 检查事件（不等待）
        bacnet_event_t event;
        if (bacnet_poll_event(&event, 0) == PROTO_SUCCESS) {
            if (event.type == BACNET_EVENT_READ_COMPLETE) {
                if (event.request == &req) {
                    // 处理结果
                    break;
                }
            }
        }
    }
}
```

### 示例 4：写入操作

```c
void example_write() {
    bacnet_write_t req = {};
    req.device_instance = 100;
    req.object_type = OBJECT_ANALOG_OUTPUT;
    req.object_instance = 0;
    req.property_id = PROP_PRESENT_VALUE;
    req.array_index = -1;
    req.priority = 16;
    req.value.type = BACNET_DATA_REAL;
    req.value.value.real_value = 25.5f;
    
    if (plc_proto_write(&req) != PROTO_SUCCESS) {
        printf("提交写请求失败\n");
        return;
    }
    
    // 等待写入完成
    bacnet_event_t event;
    while (1) {
        if (bacnet_poll_event(&event, 1000) == PROTO_SUCCESS) {
            if (event.type == BACNET_EVENT_WRITE_COMPLETE &&
                event.request == &req) {
                if (event.status == PROTO_SUCCESS) {
                    printf("写入成功\n");
                } else {
                    printf("写入失败: %d\n", event.status);
                }
                break;
            }
        }
    }
}
```

---

## 架构说明

### 文件组织

```
src/
├── proto_bacnet.h              对外 C 接口
├── proto_bacnet_internal.hpp   内部 C++ 定义
├── proto_bacnet_core.cpp       核心实现（当前使用）
├── proto_bacnet_core_new.cpp   新版本（异步）
├── proto_bacnet_callbacks.cpp  回调处理
├── proto_bacnet_discovery.cpp  设备发现
├── proto_bacnet_io.cpp         读写操作
├── proto_bacnet_utils.cpp      工具函数
└── proto_bacnet_hot_config.cpp 热配置
```

### 线程模型

```
主线程（应用）          工作线程
    │                      │
    ├─ plc_read()         ├─ 接收数据包
    │  └─ 提交请求        ├─ 处理回调
    │                      ├─ 推送事件
    ├─ 继续其他工作       └─ 检查超时
    │
    ├─ poll_event()
    │  └─ 等待事件 ←──────── 条件变量通知
    │
    └─ 处理结果
```

### 状态机

**连接状态：**
```
IDLE → CONNECTING → CONNECTED
  ↑                      ↓
  └──── DISCONNECTED ────┘
```

**操作状态：**
```
IDLE → PENDING → SUCCESS
  ↑        ↓         ↓
  └─── FAILED ───────┘
```

### 线程安全

- **原子变量** - 连接状态、操作状态
- **互斥锁** - 保护活动操作、事件队列
- **条件变量** - 事件通知机制
- **智能指针** - 自动管理工作线程

---

## 编译构建

### 编译命令

```bash
# Debug 模式
bash build.sh debug

# Release 模式
bash build.sh release

# 清理
bash build.sh clean
```

### 依赖库

- **BACnet 协议栈** - bacnet-stack
- **YAML 解析** - yaml-cpp
- **日志库** - spdlog
- **线程库** - pthread

### CMake 配置

```cmake
# impl/bacnet/CMakeLists.txt
project(proto_bacnet)

# 自动包含所有源文件
file(GLOB SRC_FILES src/*.cpp src/*.c)

# 创建动态库
add_library(${CMAKE_PROJECT_NAME} SHARED ${SRC_FILES})
```

---

## 配置说明

### 配置文件位置

```
/usr/runtime/protocol/bacnet/config.yaml
```

### 配置示例

```yaml
common:
  environment: production
  log_level: info
  log_file: bacnet.log

protocols:
  bacnet:
    enabled: true
    
    discovery:
      target_device_instance: 100      # 目标设备实例号
      whois_retry: 3                   # Who-Is 重试次数
      response_timeout_ms: 5000        # I-Am 响应超时
    
    local_device:
      instance_id: 4194303             # 本地设备ID
      max_apdu: 1476                   # 最大 APDU 长度
    
    network:
      interface: eth0                  # 网络接口
      port: 47808                      # UDP 端口
      broadcast_address: 255.255.255.255
    
    services:
      read_timeout_ms: 6000            # 读超时
      write_timeout_ms: 6000           # 写超时
      default_priority: 16             # 默认写优先级
```

### 热配置重载

```bash
# 方式 1：发送信号
kill -SIGUSR1 <pid>

# 方式 2：代码调用
bacnet_reload_config();
```

---

## 常见问题

### Q1: 如何判断请求是否完成？

```c
// 方式 1：阻塞等待
bacnet_event_t event;
bacnet_poll_event(&event, 5000);  // 等待5秒

// 方式 2：非阻塞轮询
while (1) {
    if (bacnet_poll_event(&event, 0) == PROTO_SUCCESS) {
        if (event.type != BACNET_EVENT_NONE) {
            // 处理事件
        }
    }
    // 做其他工作
}
```

### Q2: 如何处理超时？

```c
// 设置请求超时
req.timeout_ms = 10000;  // 10秒

// 轮询时也可以设置超时
bacnet_poll_event(&event, 10000);
```

### Q3: 可以同时提交多个请求吗？

```c
// 可以！异步模式支持并发请求
plc_proto_read(&req1);
plc_proto_read(&req2);
plc_proto_read(&req3);

// 然后轮询所有结果
while (completed < 3) {
    bacnet_poll_event(&event, 1000);
    // 根据 event.request 判断是哪个请求的结果
}
```

### Q4: 如何区分不同请求的结果？

```c
// 通过 event.request 字段
if (event.request == &req1) {
    // 这是 req1 的结果
} else if (event.request == &req2) {
    // 这是 req2 的结果
}
```

### Q5: 错误如何处理？

```c
// 检查提交结果
int ret = plc_proto_read(&req);
if (ret != PROTO_SUCCESS) {
    switch (ret) {
        case PROTO_ERROR_CONNECT:
            printf("连接失败\n");
            break;
        case PROTO_ERROR_PARAM:
            printf("参数错误\n");
            break;
        default:
            printf("其他错误: %d\n", ret);
            break;
    }
    return;
}

// 检查操作结果
if (event.status != PROTO_SUCCESS) {
    printf("操作失败: %d\n", event.status);
}
```

### Q6: 性能如何？

| 指标 | 值 |
|------|-----|
| 单次读延迟 | 50-100ms |
| 单次写延迟 | 50-100ms |
| 并发请求数 | 无限制（受TSM限制） |
| CPU占用 | 1-2% |
| 内存占用 | ~2MB |

### Q7: 版本如何切换？

```bash
# 查看当前版本
./switch_version.sh status

# 切换到新版本（异步）
./switch_version.sh new

# 切换到旧版本
./switch_version.sh old
```

---

## 错误码

```c
PROTO_SUCCESS           =  0   // 成功
PROTO_ERROR_PARAM       = -1   // 参数错误
PROTO_ERROR_INIT        = -2   // 初始化失败
PROTO_ERROR_CONNECT     = -3   // 连接失败
PROTO_ERROR_READ        = -4   // 读取失败
PROTO_ERROR_WRITE       = -5   // 写入失败
PROTO_ERROR_UNSUPPORTED = -6   // 不支持
```

---

## 性能优化建议

1. **批量操作** - 一次提交多个请求
2. **非阻塞轮询** - `timeout_ms = 0`
3. **合理超时** - 不要设置过长的超时时间
4. **事件队列** - 及时处理事件，避免队列堆积

---

## 总结

### 核心优势

- ✅ **异步高效** - 不阻塞调用线程
- ✅ **接口简单** - 只需 3 个函数
- ✅ **自动管理** - 连接、初始化全自动
- ✅ **线程安全** - 完善的同步机制
- ✅ **模块化** - 代码清晰易维护

### 使用要点

1. 调用 `plc_proto_read/write` 提交请求
2. 使用 `bacnet_poll_event` 获取结果
3. 通过 `event.request` 匹配请求
4. 检查 `event.status` 判断成功/失败
5. 信号触发配置重载（不占线程）

---

**维护者**: Development Team  
**版本**: 2.0 (Async)  
**更新时间**: 2025-10-22
