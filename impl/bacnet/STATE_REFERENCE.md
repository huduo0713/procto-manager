# BACnet 状态和概念参考手册

## 📖 目录

1. [连接状态 (ConnectionState)](#连接状态-connectionstate)
2. [缓存策略 (CacheStrategy)](#缓存策略-cachestrategy)
3. [活跃请求 (active_invoke_id)](#活跃请求-active_invoke_id)
4. [Invoke ID 机制](#invoke-id-机制)
5. [对象状态 (ObjectState)](#对象状态-objectstate)
6. [日志解读示例](#日志解读示例)

---

## 连接状态 (ConnectionState)

BACnet 驱动的连接状态，表示当前与 BACnet 设备的连接情况。

### 状态定义

```cpp
enum class ConnectionState {
    DISCONNECTED = 0,  // 未连接
    CONNECTING = 1,    // 正在连接（设备发现中）
    CONNECTED = 2,     // 已连接
    ERROR = 3          // 错误状态
};
```

### 状态转换

```
初始化
  ↓
DISCONNECTED (0)
  ↓
[调用 proto_connect()] → CONNECTING (1)
  ↓
[设备发现成功] → CONNECTED (2)
  ↓
[通信正常]
  ↓
[出现错误] → ERROR (3)
  ↓
[重连] → CONNECTING (1)
```

### 日志示例

**旧格式（难读）：**
```
[PLC] Current connection state: 2
```

**新格式（易读）：**
```
[PLC] Current connection state: 2 (CONNECTED)
```

### 使用函数

```cpp
// 获取状态字符串
const char* state_str = connection_state_to_string(ConnectionState::CONNECTED);
// 返回: "CONNECTED"
```

---

## 缓存策略 (CacheStrategy)

决定何时发送 BACnet 请求的策略。

### 策略定义

```cpp
enum class CacheStrategy {
    Aggressive = 0,      // 激进策略
    Conservative = 1     // 保守策略
};
```

### 策略对比

| 策略 | 请求发送时机 | 返回值来源 | 适用场景 |
|------|------------|-----------|---------|
| **Aggressive (激进)** | 每次调用都发送 | 旧缓存（如有） | PLC 高频轮询，需要最新数据 |
| **Conservative (保守)** | 仅缓存过期时发送 | 有效缓存 | 数据变化慢，减少网络负载 |

### 激进策略流程

```
PLC 调用 plc_proto_read()
  ↓
检查缓存
  ├─ 有缓存 → 立即返回缓存值（可能过期）
  └─ 无缓存 → 返回默认值
  ↓
发送新请求（无论缓存是否存在）
  ↓
后台更新缓存
```

### 保守策略流程

```
PLC 调用 plc_proto_read()
  ↓
检查缓存
  ├─ 有效缓存（未过期）→ 返回缓存值，不发送请求
  └─ 无缓存或已过期 → 发送请求，等待响应
```

### 配置方式

**config.yaml：**
```yaml
services:
  cache_expiry_ms: 1000          # 缓存过期时间（毫秒）
  cache_strategy: 0              # 0=激进, 1=保守
```

### 日志示例

**初始化时：**
```
[BACnet] Cache strategy: Aggressive (always send requests) (expiry: 1000ms)
```

### 使用函数

```cpp
const char* strategy_str = cache_strategy_to_string(CacheStrategy::Aggressive);
// 返回: "Aggressive (always send requests)"
```

---

## 活跃请求 (active_invoke_id)

用于防止对**同一四元组**发送重复请求。

### 重要概念：基于四元组的控制

`active_invoke_id` 是**针对每个四元组**独立控制的，不是针对整个对象！

**四元组定义**：
```cpp
struct ObjectKey {
    uint32_t device_instance;   // ① 设备实例号
    uint16_t object_type;       // ② 对象类型
    uint32_t object_instance;   // ③ 对象实例号
    uint32_t property_id;       // ④ 属性 ID
};
```

**这意味着**：
- ✅ 同一对象的**不同属性**可以并发读取（property_id 不同）
- ✅ **不同实例**的同一属性可以并发读取（object_instance 不同）
- ✅ **不同设备**的同一对象可以并发读取（device_instance 不同）
- ❌ **相同四元组**的重复调用会被跳过（防止网络拥塞）

### 概念解释

每个四元组状态中都有一个 `active_invoke_id` 字段：
- **0xFF (INVALID)**：当前没有活跃请求
- **0-254**：有请求正在进行中，值为该请求的 invoke_id

### 工作原理

```cpp
struct ObjectState {
    uint8_t active_invoke_id;  // 当前活跃的 invoke_id
    // ...其他字段
};
```

#### 场景 1: 正常流程

```
T=0ms:   plc_proto_read(device=5678, analog-input-1, present-value)
         ├─ ObjectKey = {5678, analog-input, 1, present-value}
         ├─ active_invoke_id = 0xFF (无活跃请求)
         ├─ 发送请求 invoke_id=10
         └─ active_invoke_id = 10  ✓ 标记为活跃

T=50ms:  plc_proto_read(device=5678, analog-input-1, present-value)  [相同四元组！]
         ├─ ObjectKey = {5678, analog-input, 1, present-value}
         ├─ active_invoke_id = 10 (有活跃请求)
         └─ 跳过发送！返回缓存值  ✓ 防止重复

T=60ms:  响应 invoke_id=10 到达
         ├─ 更新缓存
         └─ active_invoke_id = 0xFF  ✓ 清除标记

T=100ms: plc_proto_read(device=5678, analog-input-1, present-value)
         ├─ active_invoke_id = 0xFF (无活跃请求)
         └─ 可以发送新请求了  ✓
```

#### 场景 2: 不同属性可以并发

```
T=0ms:   plc_proto_read(device=5678, analog-input-1, present-value)
         ├─ ObjectKey = {5678, analog-input, 1, present-value}
         └─ 发送请求 invoke_id=10  ✓

T=10ms:  plc_proto_read(device=5678, analog-input-1, description)  [属性不同！]
         ├─ ObjectKey = {5678, analog-input, 1, description}  ← 不同的 ObjectKey
         └─ 发送请求 invoke_id=11  ✓ 可以并发！

两个请求同时在网络中传输，因为它们的四元组不同！
```

#### 场景 3: 不同实例可以并发

```
T=0ms:   plc_proto_read(device=5678, analog-input-1, present-value)
         ├─ ObjectKey = {5678, analog-input, 1, present-value}
         └─ 发送请求 invoke_id=10  ✓

T=10ms:  plc_proto_read(device=5678, analog-input-2, present-value)  [实例不同！]
         ├─ ObjectKey = {5678, analog-input, 2, present-value}  ← 不同的 ObjectKey
         └─ 发送请求 invoke_id=11  ✓ 可以并发！

两个不同实例的对象可以同时读取！
```

#### 场景 4: 防止重复请求

```
假设没有 active_invoke_id 机制：

T=0ms:   发送请求 invoke_id=10  {5678, analog-input, 1, present-value}
T=10ms:  发送请求 invoke_id=11  {5678, analog-input, 1, present-value}  [重复！]
T=20ms:  发送请求 invoke_id=12  {5678, analog-input, 1, present-value}  [重复！]
T=30ms:  发送请求 invoke_id=13  {5678, analog-input, 1, present-value}  [重复！]

结果：相同四元组被重复请求，浪费带宽，设备负载过高！
```

### 日志示例

**有活跃请求时：**
```
[PLC] Request pending: analog-input-1, present-value (active invoke_id: 10)
```

**无活跃请求时：**
```
[PLC] Request sent: analog-input-1, present-value (invoke_id: 15)
```

### 使用函数

```cpp
char buf[16];
const char* id_str = invoke_id_to_string(invoke_id, buf, sizeof(buf));
// invoke_id=10  → "10"
// invoke_id=0xFF → "INVALID"
```

---

## Invoke ID 机制

BACnet 协议用于匹配请求和响应的事务 ID。

### 基本概念

- **范围**：0-255（uint8_t）
- **用途**：一个请求一个 ID，响应时带上同样的 ID
- **循环使用**：用完 255 后从 0 开始重新分配

### 旧设计的问题

```
PLC 高频轮询（每 100ms）：

T=0ms:    plc_proto_read() → invoke_id=1, 存入哈希表 map[1]
T=100ms:  plc_proto_read() → invoke_id=2, 存入哈希表 map[2]  ← ID 变了！
T=200ms:  plc_proto_read() → invoke_id=3, 存入哈希表 map[3]
T=10ms:   响应 invoke_id=1 到达
          └─ 去 map[2] 查找（当前 PLC 等待的是 2）
          └─ 找不到！数据丢失！❌
```

### 新设计的解决方案

```
使用对象键 (ObjectKey) 作为主键：

object_states[ObjectKey] = {
    cached_value: 23.5,
    active_invoke_id: 1,
    ...
}

invoke_id_to_key[1] = ObjectKey  // 反向映射

响应到达：
1. 查 invoke_id_to_key[1] → 找到 ObjectKey
2. 更新 object_states[ObjectKey]
3. PLC 查询 object_states[ObjectKey] → 获取数据 ✓
```

### 映射关系

```
正向映射（主缓存）：
ObjectKey {device=5678, type=analog-input, inst=1, prop=present-value}
  ↓
ObjectState {value=23.5, active_invoke_id=10, ...}

反向映射（回调查找）：
invoke_id = 10
  ↓
ObjectKey {device=5678, type=analog-input, inst=1, prop=present-value}
```

---

## 对象状态 (ObjectState)

每个被读取的 BACnet 对象的完整状态。

### 数据结构

```cpp
struct ObjectState {
    bacnet_data_value_t cached_value;          // 缓存的值
    proto_status_t status;                     // 最后操作状态
    uint8_t active_invoke_id;                  // 活跃请求ID (0xFF=无)
    std::chrono::steady_clock::time_point timestamp;  // 缓存时间戳
    bool has_valid_cache;                      // 是否有有效缓存
    bacnet_read_t *original_request;           // 原始请求指针
};
```

### 字段说明

| 字段 | 类型 | 说明 | 示例值 |
|------|-----|------|-------|
| `cached_value` | bacnet_data_value_t | 缓存的数据值 | `{type=REAL, value=23.5}` |
| `status` | proto_status_t | 最后操作状态 | `PROTO_SUCCESS` |
| `active_invoke_id` | uint8_t | 活跃请求 | `10` 或 `0xFF` (无) |
| `timestamp` | time_point | 缓存更新时间 | 用于计算过期 |
| `has_valid_cache` | bool | 是否有缓存 | `true` / `false` |
| `original_request` | ptr | 原始请求 | 指向 bacnet_read_t |

### 生命周期

```
1. 首次读取
   ├─ has_valid_cache = false
   ├─ active_invoke_id = 0xFF
   └─ 发送请求 → active_invoke_id = 10

2. 响应到达
   ├─ cached_value = 新值
   ├─ timestamp = now
   ├─ has_valid_cache = true
   └─ active_invoke_id = 0xFF (清除)

3. 缓存使用
   ├─ 检查 has_valid_cache
   ├─ 计算 age = now - timestamp
   └─ 判断是否过期

4. 缓存过期
   ├─ age > cache_expiry_ms
   └─ 发送新请求
```

---

## 日志解读示例

### 示例 1: 初始化

```
[BACnet] Cache strategy: Aggressive (always send requests) (expiry: 1000ms)
```

**含义**：
- 缓存策略：激进（每次都发送请求）
- 缓存过期时间：1000 毫秒

---

### 示例 2: 连接状态

```
[PLC] Current connection state: 2 (CONNECTED)
```

**含义**：
- 连接状态码：2
- 状态名称：CONNECTED（已连接）

---

### 示例 3: 发送请求

```
[PLC] Request sent: analog-input-1, present-value (invoke_id: 10)
```

**含义**：
- 对象：analog-input-1
- 属性：present-value
- 分配的事务 ID：10

---

### 示例 4: 活跃请求

```
[PLC] Request pending: analog-input-1, present-value (active invoke_id: 10)
```

**含义**：
- 该对象已有请求在进行中
- 活跃的事务 ID：10
- 跳过发送新请求（防止重复）

---

### 示例 5: 缓存统计

```
[BACnet] Cache stats - Objects: 8 (cached: 8, active: 1), InvokeID mappings: 1, Write queue: 0/0
```

**含义**：
- 总对象数：8
- 有缓存的对象：8
- 活跃请求数：1（有 1 个请求正在等待响应）
- Invoke ID 映射数：1（有 1 个反向映射）
- 写队列：0 个已完成 / 0 个总数

---

## 快速参考表

### 状态码速查

| 代码 | 状态名 | 说明 |
|-----|-------|------|
| 0 | DISCONNECTED | 未连接 |
| 1 | CONNECTING | 连接中 |
| 2 | CONNECTED | 已连接 |
| 3 | ERROR | 错误 |

### Invoke ID 状态

| 值 | 说明 |
|----|------|
| 0-254 | 有效的事务 ID |
| 0xFF (255) | 无效/无活跃请求 |

### 缓存策略

| 代码 | 策略 | 行为 |
|-----|------|------|
| 0 | Aggressive | 总是发送请求 |
| 1 | Conservative | 仅缓存过期时发送 |

---

## 调试技巧

### 1. 检查连接状态

如果看到状态一直是 `CONNECTING (1)`，说明设备发现失败。

**解决方法**：
- 检查网络连接
- 确认设备实例号正确
- 查看 Who-Is 响应

---

### 2. 检查活跃请求

如果看到大量 "Request pending"，可能：
- 网络延迟高
- 设备响应慢
- 超时时间设置过长

**解决方法**：
- 增加超时检测频率
- 降低 PLC 轮询频率

---

### 3. 检查缓存命中率

通过统计日志查看：
```
cached: 8, active: 0  → 所有对象都有缓存，无活跃请求 ✓ 理想
cached: 2, active: 6  → 多数请求在等待响应 ⚠️ 注意
```

---

### 4. 检查 Invoke ID 映射

```
InvokeID mappings: 0  → 无请求在飞行中 ✓
InvokeID mappings: 10 → 有 10 个请求等待响应 ⚠️
```

如果映射数一直增长不减少，可能有请求泄漏。

---

## 总结

| 概念 | 作用 | 关键点 |
|------|-----|--------|
| ConnectionState | 追踪连接状态 | 0=未连接, 2=已连接 |
| CacheStrategy | 控制请求频率 | 0=激进, 1=保守 |
| active_invoke_id | 防止重复请求 | 0xFF=无, 其他=有 |
| Invoke ID | 事务匹配 | 0-255 循环使用 |
| ObjectState | 对象缓存 | 包含值、状态、时间戳 |

---

**Created**: 2025-11-06  
**Version**: 1.0  
**Author**: BACnet Cache System
