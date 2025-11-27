# 多数据链路层支持 (Multi-Datalink Support)

## 概述

该功能允许 BACnet 协议栈同时支持多种物理层传输方式，典型场景：

- **BACnet/IP (以太网)**：访问局域网内的温度传感器、风机等设备
- **MS/TP (串口 RS-485)**：访问现场总线的空调控制器、阀门等设备

PLC 可以在同一个程序中同时访问这两种网络的设备，无需重启或切换协议栈。

## 架构设计

### 方案 A+ (已实现)

**核心理念**：启动时初始化所有数据链路层 + 自动路由 + 可选显式指定

```
┌─────────────────────────────────────────────────────────────┐
│                       BACnet 协议栈                          │
├─────────────────────────────────────────────────────────────┤
│  应用层 API:                                                │
│    plc_proto_read(req)  // req.datalink_hint = AUTO (默认) │
│    plc_proto_write(req) // req.datalink_hint = BIP/MSTP    │
├─────────────────────────────────────────────────────────────┤
│  设备地址路由表:                                             │
│    Device 5678 → BACnet/IP (以太网)                          │
│    Device 1234 → MS/TP (串口)                                │
├─────────────────────────────────────────────────────────────┤
│  数据链路层 (并行运行):                                      │
│    ┌──────────────┐    ┌──────────────┐                     │
│    │  BACnet/IP   │    │    MS/TP     │                     │
│    │  (以太网)    │    │   (串口)     │                     │
│    └──────────────┘    └──────────────┘                     │
└─────────────────────────────────────────────────────────────┘
         │                      │
         ▼                      ▼
    以太网设备              串口设备
  (温度传感器等)         (空调控制器等)
```

### 关键特性

1. **自动路由**（推荐）
   - 协议栈在 Who-Is 发现阶段记录设备地址和数据链路层类型
   - 后续读写请求自动路由到正确的物理层
   - 应用层无需关心设备连接方式

2. **显式指定**（可选）
   - 在 `bacnet_read_t/bacnet_write_t` 中设置 `datalink_hint` 字段
   - 用于调试、性能优化或已知设备类型的场景

3. **编译时配置**
   - CMakeLists.txt 中定义 `BACDL_MULTIPLE=1` 启用多数据链路层

## 代码改动

### 1. API 扩展

#### 新增枚举：`bacnet_datalink_type_t`

```c
typedef enum {
    BACNET_DATALINK_AUTO = 0,    /* 自动检测（默认） */
    BACNET_DATALINK_BIP,         /* BACnet/IP（以太网） */
    BACNET_DATALINK_MSTP,        /* MS/TP（串口） */
    BACNET_DATALINK_ETHERNET,    /* Ethernet（ISO 8802-3） */
    BACNET_DATALINK_BIP6         /* BACnet/IPv6 */
} bacnet_datalink_type_t;
```

#### 扩展结构体：`bacnet_read_t` 和 `bacnet_write_t`

```c
typedef struct {
    uint32_t            device_instance;
    uint16_t            object_type;
    uint32_t            object_instance;
    uint32_t            property_id;
    bacnet_data_value_t *value;
    int32_t             array_index;
    uint32_t            timeout_ms;
    uint8_t             invoke_id;
    
    /* 新增字段 */
    bacnet_datalink_type_t datalink_hint;  /* 数据链路层提示（默认: AUTO） */
} bacnet_read_t;
```

### 2. 配置支持

#### `bacnet_config_t` 新增 MS/TP 字段

```c
typedef struct {
    /* ... 现有 19 个字段 ... */
    
    /* MS/TP 配置 */
    char     mstp_port[32];      /* 串口路径（如 "/dev/ttyUSB0"） */
    uint32_t mstp_baud;          /* 波特率（9600/19200/38400/76800） */
    uint8_t  mstp_mac;           /* MAC 地址（0-127） */
    uint8_t  mstp_max_master;    /* 最大主站地址（默认 127） */
    uint8_t  mstp_max_frames;    /* 最大信息帧数（默认 1） */
} bacnet_config_t;
```

#### `config.yaml` 新增 mstp section

```yaml
protocols:
  bacnet:
    network:
      interface: "eth0"
      port: 47808
    
    mstp:
      port: "/dev/ttyUSB0"   # 留空则不启用 MS/TP
      baud_rate: 38400
      mac_address: 1
      max_master: 127
      max_info_frames: 1
```

### 3. 核心实现

#### `initialize_all_datalinks()` 函数

- 位置：`impl/bacnet/src/proto_bacnet_core.cc`
- 功能：在启动时初始化所有配置的数据链路层
- 调用时机：`initialize_context()` 中，在 `dlenv_init()` 之后

```cpp
proto_status_t initialize_all_datalinks(BacnetContext *context) {
    #if defined(BACDL_MULTIPLE)
        // 1. 初始化 BACnet/IP
        bip_set_port(context->config.port);
        bip_init(context->config.interface_name);
        
        // 2. 初始化 MS/TP（如果配置了串口）
        if (context->config.mstp_port[0] != '\0') {
            dlmstp_set_baud_rate(context->config.mstp_baud);
            dlmstp_set_mac_address(context->config.mstp_mac);
            dlmstp_init(context->config.mstp_port);
        }
    #endif
    
    return PROTO_SUCCESS;
}
```

### 4. 编译配置

#### CMakeLists.txt

```cmake
# 启用多数据链路层支持
add_definitions(-DBACDL_MULTIPLE=1)
```

## 使用指南

### 基本用法（自动路由）

```c
#include "proto_bacnet.h"

int main() {
    // 1. 初始化 PLC 协议驱动
    proto_ctx_t *ctx = plc_proto_init();
    
    // 2. 读取以太网设备（自动路由）
    bacnet_data_value_t value = {BACNET_DATA_NULL, {0}};
    bacnet_read_t req = BACNET_READ_INIT(
        5678,                   // 以太网设备 ID
        OBJECT_ANALOG_INPUT,
        0,
        PROP_PRESENT_VALUE,
        &value
    );
    // datalink_hint 默认为 AUTO，无需设置
    
    plc_proto_read(&req);
    
    // 3. 控制串口设备（自动路由）
    bacnet_data_value_t target = {
        .type = BACNET_DATA_REAL,
        .value = {.real_value = 24.0f}
    };
    bacnet_write_t write_req = BACNET_WRITE_INIT(
        1234,                   // 串口设备 ID
        OBJECT_ANALOG_OUTPUT,
        0,
        PROP_PRESENT_VALUE,
        target
    );
    // 协议栈会自动路由到串口
    
    plc_proto_write(&write_req);
    
    // 4. 清理
    plc_proto_deinit(ctx);
}
```

### 高级用法（显式指定）

```c
// 强制走以太网 BACnet/IP
bacnet_read_t req = BACNET_READ_INIT(...);
req.datalink_hint = BACNET_DATALINK_BIP;
plc_proto_read(&req);

// 强制走串口 MS/TP
bacnet_write_t write_req = BACNET_WRITE_INIT(...);
write_req.datalink_hint = BACNET_DATALINK_MSTP;
plc_proto_write(&write_req);
```

## 配置步骤

### 1. 编辑 `config.yaml`

```yaml
protocols:
  bacnet:
    # BACnet/IP 配置（以太网）
    network:
      interface: "eth0"        # 网络接口名称
      port: 47808              # BACnet/IP 端口
    
    # MS/TP 配置（串口）
    mstp:
      port: "/dev/ttyUSB0"     # 串口设备路径
      baud_rate: 38400         # 波特率
      mac_address: 1           # MAC 地址（0-127，唯一）
      max_master: 127          # 最大主站
      max_info_frames: 1       # 最大帧数
```

### 2. 构建项目

```bash
cd impl/bacnet
mkdir -p build
cd build
cmake ..
make
```

### 3. 运行示例

```bash
# 基础演示
./bacnet_demo

# 多数据链路层示例
./multi_datalink_example
```

## 测试验证

### 查看初始化日志

运行任何程序后，查看日志确认两个数据链路层都已启动：

```
[BACnet][DataLink] Initializing multiple datalink layers...
[BACnet][DataLink] [1/2] Initializing BACnet/IP (Ethernet)...
[BACnet][DataLink][BIP] Port set to 47808
[BACnet][DataLink][BIP] Initialized successfully (interface: eth0, port: 47808)
[BACnet][DataLink] [2/2] Initializing MS/TP (RS-485 Serial)...
[BACnet][DataLink][MSTP] Baud rate set to 38400 bps
[BACnet][DataLink][MSTP] MAC address set to 1
[BACnet][DataLink][MSTP] Initialized successfully (port: /dev/ttyUSB0, baud: 38400, MAC: 1)
[BACnet][DataLink] All datalink layers initialized successfully
```

### 验证自动路由

1. 在以太网和串口上分别连接设备
2. 运行 `multi_datalink_example`
3. 观察协议栈自动选择正确的数据链路层

## 故障排查

### Q: MS/TP 初始化失败

**可能原因**：
- 串口设备路径不存在（如 `/dev/ttyUSB0`）
- 权限不足（需要 `sudo` 或加入 `dialout` 组）
- 波特率不匹配

**解决方法**：
```bash
# 检查串口设备
ls -l /dev/ttyUSB*

# 添加用户到 dialout 组
sudo usermod -a -G dialout $USER

# 重新登录使权限生效
```

### Q: 设备路由到错误的数据链路层

**原因**：协议栈自动学习的设备地址表可能不准确

**解决方法**：使用显式指定
```c
req.datalink_hint = BACNET_DATALINK_MSTP;  // 强制走串口
```

### Q: 编译错误 "BACDL_MULTIPLE undefined"

**原因**：CMakeLists.txt 中未启用多数据链路层

**解决方法**：
```cmake
add_definitions(-DBACDL_MULTIPLE=1)
```

## 性能考虑

### 自动路由 vs 显式指定

| 方式 | 首次请求延迟 | 后续请求延迟 | 推荐场景 |
|------|-------------|-------------|---------|
| 自动路由 | 略高（需查询路由表）| 低（直接命中缓存）| 大多数场景 |
| 显式指定 | 低（跳过查询）| 低 | 已知设备类型 |

### 内存占用

- BACnet/IP: ~100 KB
- MS/TP: ~80 KB
- 总计: ~180 KB（可接受）

## 未来扩展

### 支持更多数据链路层

当前架构可轻松扩展支持：

1. **BACnet/IPv6** (`BACNET_DATALINK_BIP6`)
2. **Ethernet** (`BACNET_DATALINK_ETHERNET`)
3. **LonTalk**
4. **ZigBee**

只需：
1. 在 `bacnet_datalink_type_t` 中添加枚举
2. 在 `initialize_all_datalinks()` 中添加初始化代码
3. 在 config.yaml 中添加对应 section

## 相关文件

### 头文件
- `impl/bacnet/src/proto_bacnet.h` - 公共 API 定义
- `impl/bacnet/src/proto_bacnet_internal.hpp` - 内部实现头文件

### 源文件
- `impl/bacnet/src/proto_bacnet_core.cc` - 初始化逻辑
- `impl/bacnet/src/proto_bacnet_utils.cc` - 配置加载

### 配置文件
- `impl/bacnet/config.yaml` - 运行时配置
- `impl/bacnet/CMakeLists.txt` - 编译配置

### 示例程序
- `impl/bacnet/demo/multi_datalink_example.cc` - 多数据链路层使用示例

## 参考资料

- [BACnet 协议标准 ASHRAE 135](https://www.ashrae.org/technical-resources/bookstore/bacnet)
- [BACnet Stack 项目文档](https://bacnet.sourceforge.net/)
- [MS/TP 数据链路层说明](https://en.wikipedia.org/wiki/BACnet#MS/TP)

---

**版本**: 1.0.0  
**最后更新**: 2025-11-18  
**作者**: GitHub Copilot
