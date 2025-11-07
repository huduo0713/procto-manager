# BACnet Object-Based Caching Design

## 概述

本设计实现了基于对象键（ObjectKey）的统一缓存机制，解决了PLC高频轮询场景中invoke_id不匹配导致无法获取数据的问题。

## 核心设计原则

1. **以对象为中心**：不关心invoke_id是否匹配，只看对象(device, object_type, object_instance, property_id)是否匹配
2. **灵活的缓存策略**：支持激进和保守两种策略，通过配置文件控制
3. **O(1)查找性能**：使用哈希表实现快速缓存查找和更新

## 数据结构

### ObjectKey（对象键）
```cpp
struct ObjectKey {
    uint32_t device_instance;     // 设备实例号
    uint16_t object_type;         // 对象类型（如ANALOG_INPUT）
    uint32_t object_instance;     // 对象实例号
    uint32_t property_id;         // 属性ID（如PRESENT_VALUE）
};
```

### ObjectState（对象状态）
```cpp
struct ObjectState {
    bacnet_data_value_t cached_value;    // 缓存的值
    proto_status_t status;               // 最后一次操作状态
    uint8_t active_invoke_id;            // 当前活跃请求ID（0表示无活跃请求）
    std::chrono::steady_clock::time_point timestamp;  // 缓存更新时间
    bool has_valid_cache;                // 是否有有效缓存
};
```

### 哈希表

1. **主缓存表**：`unordered_map<ObjectKey, ObjectState> object_states`
   - Key：对象四元组
   - Value：对象状态（包含缓存值、活跃请求等）

2. **反向映射表**：`unordered_map<uint8_t, ObjectKey> invoke_id_to_key`
   - Key：invoke_id
   - Value：对象键
   - 用途：回调函数通过invoke_id找到对应的ObjectKey

3. **写队列**：`unordered_map<uint8_t, WriteBufferItem> write_queue`
   - 写操作不需要缓存，仍用invoke_id作为key

## 缓存策略

### 1. 激进策略（Aggressive，默认）
- **行为**：每次读操作都发送BACnet请求
- **优点**：数据总是最新的
- **适用场景**：网络压力不大，要求数据实时性高
- **配置**：`cache_strategy: 0`

### 2. 保守策略（Conservative）
- **行为**：如果有未过期的缓存，直接返回缓存值，不发送请求
- **优点**：减少网络流量
- **适用场景**：网络压力大，可容忍一定数据延迟
- **配置**：`cache_strategy: 1`

### 缓存过期时间
- 配置参数：`cache_expiry_ms`（默认1000ms）
- 作用：决定缓存有效期

## 工作流程

### 读操作流程

```
PLC调用 plc_proto_read()
    ↓
构造 ObjectKey(device, obj_type, obj_inst, prop_id)
    ↓
查找 object_states[key]
    ↓
┌──────────────┬────────────────────┐
│  保守策略    │    激进策略        │
├──────────────┼────────────────────┤
│ 有缓存且未过期│  不检查缓存       │
│   → 返回缓存 │   → 发送请求      │
│              │                    │
│ 无缓存或过期 │                    │
│   → 发送请求 │                    │
└──────────────┴────────────────────┘
    ↓
发送请求前检查：
  - 有活跃请求(active_invoke_id != 0)？
    → 返回 PROTO_PENDING（附带旧缓存）
  - 无活跃请求
    → 发送BACnet请求
    ↓
Send_Read_Property_Request()
    ↓
获得 invoke_id
    ↓
建立映射：invoke_id_to_key[invoke_id] = key
更新状态：object_states[key].active_invoke_id = invoke_id
    ↓
返回 PROTO_SUCCESS 或 PROTO_PENDING
```

### 响应处理流程

```
BACnet响应到达
    ↓
handle_read_property_ack(invoke_id, data)
    ↓
查找 invoke_id_to_key[invoke_id] → 得到 ObjectKey
    ↓
删除反向映射：invoke_id_to_key.erase(invoke_id)
    ↓
解码数据值
    ↓
更新缓存：
  object_states[key].cached_value = data
  object_states[key].has_valid_cache = true
  object_states[key].timestamp = now
  object_states[key].active_invoke_id = 0  ← 清除活跃标记
    ↓
释放TSM资源：tsm_free_invoke_id(invoke_id)
```

## PLC高频轮询场景

### 问题场景（旧设计）
```
T=0ms:   PLC调用read() → 发送请求，invoke_id=1
T=100ms: PLC调用read() → 发送请求，invoke_id=2
T=200ms: PLC调用read() → 发送请求，invoke_id=3
T=250ms: 响应1到达 → 存入hashmap[1]
T=300ms: PLC调用read() → 发送请求，invoke_id=4
         PLC检查hashmap[4] → 未找到！
         结果：永远拿不到数据
```

### 新设计解决方案
```
T=0ms:   PLC调用read() 
         → 检查object_states[key]，无缓存
         → 发送请求，invoke_id=1
         → 设置active_invoke_id=1
         → 返回 PROTO_PENDING

T=100ms: PLC调用read()
         → 检查object_states[key]，有活跃请求(id=1)
         → 返回 PROTO_PENDING（不发新请求）

T=200ms: PLC调用read()
         → 检查object_states[key]，仍有活跃请求(id=1)
         → 返回 PROTO_PENDING

T=250ms: 响应1到达
         → invoke_id_to_key[1] → 找到key
         → 更新object_states[key]:
            - cached_value = 新数据
            - active_invoke_id = 0
            - has_valid_cache = true

T=300ms: PLC调用read()
         → 检查object_states[key]
         → 激进策略：发送新请求，返回旧缓存
         → 保守策略：缓存未过期，直接返回缓存
```

## 配置示例

```yaml
# config.yaml
protocols:
  bacnet:
    services:
      read_timeout_ms: 6000
      write_timeout_ms: 6000
      cache_expiry_ms: 1000        # 缓存1秒过期
      cache_strategy: 0            # 0=激进, 1=保守
```

## 内存管理

### 缓存清理
- **定期清理**：每5秒清理超时请求
- **超时阈值**：2倍请求超时时间（默认12秒）
- **清理内容**：
  - 清除超时的invoke_id_to_key映射
  - 重置object_states中的active_invoke_id
  - 释放TSM资源

### 缓存淘汰
当前实现：
- 缓存不会主动淘汰（除非手动清理）
- 过期缓存仍保留在内存中
- 如果内存压力大，后续可添加LRU淘汰策略

## 性能特性

1. **查找性能**：O(1) - 哈希表查找
2. **插入/更新性能**：O(1) - 哈希表操作
3. **删除性能**：O(1) - 哈希表删除
4. **并发安全**：通过互斥锁保证线程安全

## 与旧设计对比

| 特性 | 旧设计（invoke_id队列） | 新设计（ObjectKey缓存） |
|------|------------------------|------------------------|
| 查找key | invoke_id | ObjectKey |
| PLC轮询 | ❌ invoke_id不匹配 | ✅ 对象匹配 |
| 缓存 | ❌ 无缓存 | ✅ 有缓存 |
| 策略 | 单一 | 灵活（激进/保守） |
| 性能 | O(1) | O(1) |
| 内存占用 | 低（仅活跃请求） | 中（包含缓存） |

## 待优化项

1. **缓存淘汰策略**：添加LRU或基于内存占用的淘汰
2. **缓存统计**：添加命中率、miss率监控
3. **动态策略**：根据网络状况自动切换策略
4. **批量读取**：支持一次读取多个属性，共享缓存

## 测试场景

### 场景1：PLC 100ms轮询
- 配置：激进策略，1000ms过期
- 预期：
  - 第一次：发送请求，返回PENDING
  - 100ms后：请求仍活跃，返回PENDING
  - 250ms后响应到达：缓存更新
  - 300ms后：发送新请求，返回旧缓存
  - 数据实时性：~200-300ms延迟

### 场景2：PLC 2000ms慢速轮询
- 配置：保守策略，1000ms过期
- 预期：
  - 第一次：发送请求
  - 响应到达：缓存更新
  - 2000ms后：缓存已过期，发送新请求
  - 数据实时性：~1-2s延迟

### 场景3：并发读取
- 8个不同对象并发读取
- 预期：
  - 每个对象独立缓存
  - 无invoke_id冲突
  - O(1)查找性能

## 日志示例

```
[BACnet] plc_proto_read: device=5678, ANALOG_INPUT-1, present-value
[BACnet] Cache strategy: Aggressive (expiry: 1000ms)
[BACnet] Request sent: ANALOG_INPUT-1, present-value (invoke_id: 42)
[BACnet] Invoke ID 42 mapped to ANALOG_INPUT-1, present-value
[BACnet] ReadProperty successful, cache updated (device: 5678, ANALOG_INPUT-1, present-value, invoke_id: 42)
[BACnet] Cache hit (conservative): ANALOG_INPUT-1, present-value (age: 250ms)
[BACnet] Cache stats - Objects: 8 (cached: 7, active: 1), InvokeID mappings: 1, Write queue: 0/0
```

## 总结

此设计彻底解决了PLC高频轮询场景中invoke_id不匹配的问题，通过以对象为中心的缓存机制，提供了灵活的数据获取策略，并保持了O(1)的高性能。
