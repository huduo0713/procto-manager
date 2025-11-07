# ObjectKey 四元组工作示例

## 🎯 场景：并发读取同一对象的多个属性

### 示例代码

```c
// 同时读取 analog-input-1 的三个属性
bacnet_read_t req1 = {
    .device_instance = 5678,
    .object_type = OBJECT_ANALOG_INPUT,
    .object_instance = 1,
    .property_id = PROP_PRESENT_VALUE    // 当前值
};

bacnet_read_t req2 = {
    .device_instance = 5678,
    .object_type = OBJECT_ANALOG_INPUT,
    .object_instance = 1,
    .property_id = PROP_DESCRIPTION      // 描述
};

bacnet_read_t req3 = {
    .device_instance = 5678,
    .object_type = OBJECT_ANALOG_INPUT,
    .object_instance = 1,
    .property_id = PROP_UNITS            // 单位
};

// 连续调用
plc_proto_read(&req1);  // T=0ms
plc_proto_read(&req2);  // T=1ms
plc_proto_read(&req3);  // T=2ms
```

---

## 📋 内部处理流程

### T=0ms: 读取 present-value

```cpp
// 1. 构造 ObjectKey
ObjectKey key1 = {5678, analog-input, 1, present-value};

// 2. 查找缓存
object_states.find(key1)  → 找不到（首次）

// 3. 发送请求
invoke_id = 10
active_invoke_id = 10

// 4. 建立映射
object_states[key1] = {
    active_invoke_id: 10,
    has_valid_cache: false
};
invoke_id_to_key[10] = key1;
```

**状态快照：**
```
object_states:
  key1 {5678, analog-input, 1, present-value} → {active: 10}

invoke_id_to_key:
  10 → key1
```

---

### T=1ms: 读取 description（不同属性！）

```cpp
// 1. 构造 ObjectKey
ObjectKey key2 = {5678, analog-input, 1, description};  // property_id 不同！

// 2. 查找缓存
object_states.find(key2)  → 找不到（首次）

// 3. key2 != key1，可以并发！
invoke_id = 11
active_invoke_id = 11

// 4. 建立映射
object_states[key2] = {
    active_invoke_id: 11,
    has_valid_cache: false
};
invoke_id_to_key[11] = key2;
```

**状态快照：**
```
object_states:
  key1 {5678, analog-input, 1, present-value} → {active: 10}
  key2 {5678, analog-input, 1, description}   → {active: 11}  ← 新增

invoke_id_to_key:
  10 → key1
  11 → key2  ← 新增
```

---

### T=2ms: 读取 units（又一个不同属性！）

```cpp
// 1. 构造 ObjectKey
ObjectKey key3 = {5678, analog-input, 1, units};  // property_id 再次不同！

// 2. 查找缓存
object_states.find(key3)  → 找不到（首次）

// 3. key3 != key1 && key3 != key2，可以并发！
invoke_id = 12
active_invoke_id = 12

// 4. 建立映射
object_states[key3] = {
    active_invoke_id: 12,
    has_valid_cache: false
};
invoke_id_to_key[12] = key3;
```

**状态快照：**
```
object_states:
  key1 {5678, analog-input, 1, present-value} → {active: 10}
  key2 {5678, analog-input, 1, description}   → {active: 11}
  key3 {5678, analog-input, 1, units}         → {active: 12}  ← 新增

invoke_id_to_key:
  10 → key1
  11 → key2
  12 → key3  ← 新增

✅ 三个请求同时在网络中传输！
```

---

### T=3ms: 重复读取 present-value（防重复机制启动）

```cpp
// 1. 构造 ObjectKey
ObjectKey key1 = {5678, analog-input, 1, present-value};  // 和第一个请求相同！

// 2. 查找缓存
object_states.find(key1)  → 找到了！

// 3. 检查 active_invoke_id
it->second.active_invoke_id = 10  (非 0xFF，有活跃请求)

// 4. 跳过发送！
log_debug("Request pending: analog-input-1, present-value (active invoke_id: 10)");
return PROTO_NO_DATA;  // 返回旧缓存（如果有）
```

**状态快照：**
```
object_states:
  key1 {5678, analog-input, 1, present-value} → {active: 10}  ← 仍然活跃
  key2 {5678, analog-input, 1, description}   → {active: 11}
  key3 {5678, analog-input, 1, units}         → {active: 12}

invoke_id_to_key:
  10 → key1
  11 → key2
  12 → key3

❌ 没有发送新请求！（防止重复）
```

---

### T=15ms: 响应 invoke_id=10 到达

```cpp
// 回调函数处理
handle_read_property_ack(..., invoke_id=10) {
    // 1. 反向查找对象键
    auto key_it = invoke_id_to_key.find(10);
    ObjectKey key = key_it->second;  // key1 {5678, analog-input, 1, present-value}
    
    // 2. 更新对象状态
    auto& state = object_states[key];
    state.cached_value = 新值;
    state.has_valid_cache = true;
    state.timestamp = now;
    state.active_invoke_id = 0xFF;  // 清除活跃标记
    
    // 3. 删除反向映射
    invoke_id_to_key.erase(10);
}
```

**状态快照：**
```
object_states:
  key1 {5678, analog-input, 1, present-value} → {active: INVALID, cache: 23.5} ✓
  key2 {5678, analog-input, 1, description}   → {active: 11}
  key3 {5678, analog-input, 1, units}         → {active: 12}

invoke_id_to_key:
  11 → key2
  12 → key3

✅ key1 的缓存已更新，active_invoke_id 已清除
```

---

### T=16ms: 再次读取 present-value（可以发送新请求了）

```cpp
// 1. 构造 ObjectKey
ObjectKey key1 = {5678, analog-input, 1, present-value};

// 2. 查找缓存
object_states.find(key1)  → 找到了！

// 3. 检查 active_invoke_id
it->second.active_invoke_id = 0xFF  (无活跃请求)

// 4. 可以发送新请求！
invoke_id = 13
active_invoke_id = 13

// 5. 同时返回缓存值
*read_req->value = state.cached_value;  // 23.5
```

**状态快照：**
```
object_states:
  key1 {5678, analog-input, 1, present-value} → {active: 13, cache: 23.5}
  key2 {5678, analog-input, 1, description}   → {active: 11}
  key3 {5678, analog-input, 1, units}         → {active: 12}

invoke_id_to_key:
  11 → key2
  12 → key3
  13 → key1  ← 新的请求

✅ 返回旧缓存值 23.5，同时发送新请求获取最新值
```

---

## 🎯 不同实例的并发示例

```c
// 同时读取多个 analog-input 的 present-value
plc_proto_read({5678, analog-input, 1, present-value});  // key1
plc_proto_read({5678, analog-input, 2, present-value});  // key2 (实例不同)
plc_proto_read({5678, analog-input, 3, present-value});  // key3 (实例不同)
```

**内部状态：**
```
object_states:
  {5678, analog-input, 1, present-value} → {active: 10}
  {5678, analog-input, 2, present-value} → {active: 11}  ← 可以并发
  {5678, analog-input, 3, present-value} → {active: 12}  ← 可以并发

✅ 三个不同实例，三个不同的 ObjectKey，可以同时发送请求！
```

---

## 🎯 不同设备的并发示例

```c
// 同时读取多个设备的同一对象
plc_proto_read({5678, analog-input, 1, present-value});  // key1
plc_proto_read({5679, analog-input, 1, present-value});  // key2 (设备不同)
plc_proto_read({5680, analog-input, 1, present-value});  // key3 (设备不同)
```

**内部状态：**
```
object_states:
  {5678, analog-input, 1, present-value} → {active: 10}
  {5679, analog-input, 1, present-value} → {active: 11}  ← 可以并发
  {5680, analog-input, 1, present-value} → {active: 12}  ← 可以并发

✅ 三个不同设备，三个不同的 ObjectKey，可以同时发送请求！
```

---

## 🚫 防重复示例

```c
// 快速重复调用同一个四元组
plc_proto_read({5678, analog-input, 1, present-value});  // T=0ms, invoke_id=10
plc_proto_read({5678, analog-input, 1, present-value});  // T=1ms, 跳过！
plc_proto_read({5678, analog-input, 1, present-value});  // T=2ms, 跳过！
plc_proto_read({5678, analog-input, 1, present-value});  // T=3ms, 跳过！
```

**日志输出：**
```
[T=0ms] Request sent: analog-input-1, present-value (invoke_id: 10)
[T=1ms] Request pending: analog-input-1, present-value (active invoke_id: 10)
[T=2ms] Request pending: analog-input-1, present-value (active invoke_id: 10)
[T=3ms] Request pending: analog-input-1, present-value (active invoke_id: 10)
```

**内部状态：**
```
object_states:
  {5678, analog-input, 1, present-value} → {active: 10}

invoke_id_to_key:
  10 → {5678, analog-input, 1, present-value}

❌ 只发送了一个请求，避免了网络拥塞！
```

---

## 📊 四元组唯一性保证

### 不同的情况都会生成不同的 ObjectKey

| 场景 | device | type | instance | property | ObjectKey |
|------|--------|------|----------|----------|-----------|
| analog-input-1 的值 | 5678 | analog-input | 1 | present-value | key1 |
| analog-input-1 的描述 | 5678 | analog-input | 1 | **description** | key2 ≠ key1 |
| analog-input-**2** 的值 | 5678 | analog-input | **2** | present-value | key3 ≠ key1 |
| **binary**-input-1 的值 | 5678 | **binary-input** | 1 | present-value | key4 ≠ key1 |
| 设备 **5679** 的 analog-input-1 | **5679** | analog-input | 1 | present-value | key5 ≠ key1 |

**结论**：只要四元组中的任何一个元素不同，就是不同的请求，可以并发执行！

---

## 🎯 总结

### ObjectKey 四元组的作用

1. **精确标识**：唯一确定一个 BACnet 属性读取请求
2. **并发控制**：不同四元组的请求可以并发
3. **防重复**：相同四元组的重复调用会被跳过
4. **缓存管理**：每个四元组有独立的缓存和活跃状态

### active_invoke_id 的作用

- **范围**：针对**特定四元组**
- **目的**：防止对**同一四元组**发送重复请求
- **不影响**：其他四元组的并发请求

### 关键优势

✅ 同一对象的不同属性可以并发读取  
✅ 不同实例的同一属性可以并发读取  
✅ 不同设备的同一对象可以并发读取  
✅ 相同四元组的重复调用会被优化（防重复）  
✅ 每个四元组有独立的缓存和超时管理

---

**Created**: 2025-11-06  
**Version**: 1.0  
**Author**: BACnet Cache System - ObjectKey Examples
