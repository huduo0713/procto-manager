/**
 * @file multi_datalink_example.cc
 * @brief 多数据链路层使用示例（零配置版本）
 * 
 * 功能说明：
 * - 演示如何在一个 PLC 中同时使用 BACnet/IP 和 MS/TP
 * - 演示自动路由和显式指定数据链路层两种方式
 * - 适用场景：同时访问以太网设备和串口设备
 * - ⭐ 零配置：无需手动初始化和清理
 * 
 * 编译要求：
 * - 必须在 CMakeLists.txt 中启用 BACDL_MULTIPLE=1
 * - config.yaml 中配置好 network 和 mstp 两个 section
 * 
 * 零配置架构（v4.0）：
 * - ✅ 无需调用 plc_proto_init()    → 首次读写时自动初始化
 * - ✅ 无需调用 plc_proto_deinit()  → 程序退出时自动清理（atexit）
 * - ✅ 无需检查连接状态              → 读写时自动连接
 */

#include "proto_bacnet.h"
#include "common/utils/one_logger.hpp"
#include <bacnet/bacenum.h>  // BACnet 枚举定义
#include <cstdio>
#include <cstring>
#include <unistd.h>

/* ========================================================================== */
/* 示例场景说明                                                               */
/* ========================================================================== */

/*
 * 假设我们有以下设备：
 * 
 * 1. 温度传感器（设备 ID: 5678）
 *    - 物理连接：以太网（BACnet/IP）
 *    - 对象：AnalogInput:0 (Property: PresentValue)
 *    - 功能：读取当前温度
 * 
 * 2. 空调控制器（设备 ID: 1234）
 *    - 物理连接：串口 RS-485（MS/TP）
 *    - 对象：AnalogOutput:0 (Property: PresentValue)
 *    - 功能：设置目标温度
 * 
 * 我们的 PLC 需要：
 * - 从以太网读取温度传感器数据
 * - 通过串口控制空调设备
 * 
 * BACnet 协议栈会自动处理数据链路层路由！
 */

/* ========================================================================== */
/* 方式 1：自动路由（推荐）                                                    */
/* ========================================================================== */

/**
 * @brief 使用自动路由读取设备
 * 
 * 原理：
 * - BACnet 协议栈会在首次 Who-Is 发现时记录设备地址
 * - 后续读写操作会根据设备地址表自动选择正确的数据链路层
 * - 无需应用层关心设备使用哪种物理层
 * 
 * 优点：
 * - 代码简洁，无需指定数据链路层类型
 * - 设备移动到不同网络无需修改代码
 * - 符合 BACnet 协议栈设计理念
 * 
 * ⭐ 重要：无需手动初始化！
 * - 首次调用 plc_proto_read/write 时自动初始化
 * - 程序退出时自动清理资源（atexit）
 */
void example_auto_routing() {
    log_info("========================================");
    log_info("示例 1：自动路由（推荐方式）");
    log_info("========================================");

    // ⭐ 无需初始化！驱动会自动初始化
    // 以前：proto_ctx_t *ctx = plc_proto_init();  ❌ 不需要
    // 现在：直接使用 plc_proto_read/write        ✅ 推荐

    /* ---------------------------------------------------------------------- */
    /* 读取以太网温度传感器（设备 5678）                                       */
    /* ---------------------------------------------------------------------- */

    bacnet_data_value_t temp_value = {BACNET_DATA_NULL, {0}};
    bacnet_read_t temp_req = BACNET_READ_INIT(
        5678,                       // 设备实例 ID
        OBJECT_ANALOG_INPUT,        // 对象类型：模拟输入
        0,                          // 对象实例
        PROP_PRESENT_VALUE,         // 属性：当前值
        &temp_value                 // 输出缓冲区
    );
    // 注意：datalink_hint 默认为 AUTO，无需设置

    log_info("读取温度传感器（设备 5678，以太网 BACnet/IP）...");
    int ret = plc_proto_read(&temp_req);
    if (ret != PROTO_SUCCESS) {
        log_error("读取失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
    } else {
        log_info("请求已提交，等待结果...");
        sleep(3);  // 等待异步响应
        
        if (temp_value.type == BACNET_DATA_REAL) {
            log_info("✓ 当前温度: {:.2f} °C", temp_value.value.real_value);
        }
    }

    /* ---------------------------------------------------------------------- */
    /* 控制串口空调设备（设备 1234）                                           */
    /* ---------------------------------------------------------------------- */

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
    // 注意：datalink_hint 默认为 AUTO，协议栈会自动路由到串口

    log_info("设置空调目标温度（设备 1234，串口 MS/TP）...");
    ret = plc_proto_write(&ac_req);
    if (ret != PROTO_SUCCESS) {
        log_error("写入失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
    } else {
        log_info("✓ 目标温度已设置为 24.0 °C");
    }

    // ⭐ 无需清理！程序退出时自动清理（atexit）
    // 以前：plc_proto_deinit(ctx);  ❌ 不需要
    // 现在：自动清理              ✅ RAII + atexit

    log_info("========================================\n");
}

/* ========================================================================== */
/* 方式 2：显式指定数据链路层（可选）                                          */
/* ========================================================================== */

/**
 * @brief 显式指定数据链路层类型
 * 
 * 使用场景：
 * - 已知设备连接方式，希望跳过路由查询以提升性能
 * - 调试特定网络问题
 * - 设备尚未发现，但已知其物理层类型
 * 
 * 注意：
 * - 大多数情况下不需要显式指定
 * - 如果指定错误，请求会失败
 */
void example_explicit_datalink() {
    log_info("========================================");
    log_info("示例 2：显式指定数据链路层（可选）");
    log_info("========================================");

    // ⭐ 无需初始化！直接使用

    /* ---------------------------------------------------------------------- */
    /* 显式指定走以太网 BACnet/IP                                             */
    /* ---------------------------------------------------------------------- */

    bacnet_data_value_t temp_value = {BACNET_DATA_NULL, {0}};
    bacnet_read_t temp_req = BACNET_READ_INIT(
        5678,
        OBJECT_ANALOG_INPUT,
        0,
        PROP_PRESENT_VALUE,
        &temp_value
    );
    
    // 显式指定：强制使用 BACnet/IP（以太网）
    temp_req.datalink_hint = BACNET_DATALINK_BIP;

    log_info("读取温度传感器（强制走 BACnet/IP）...");
    int ret = plc_proto_read(&temp_req);
    if (ret != PROTO_SUCCESS) {
        log_error("读取失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
    } else {
        log_info("请求已提交（显式指定 BACnet/IP）");
    }

    sleep(2);

    /* ---------------------------------------------------------------------- */
    /* 显式指定走串口 MS/TP                                                   */
    /* ---------------------------------------------------------------------- */

    bacnet_data_value_t target_temp = {
        .type = BACNET_DATA_REAL,
        .value = {.real_value = 22.0f}
    };

    bacnet_write_t ac_req = BACNET_WRITE_INIT(
        1234,
        OBJECT_ANALOG_OUTPUT,
        0,
        PROP_PRESENT_VALUE,
        target_temp
    );
    
    // 显式指定：强制使用 MS/TP（串口）
    ac_req.datalink_hint = BACNET_DATALINK_MSTP;

    log_info("设置空调目标温度（强制走 MS/TP）...");
    ret = plc_proto_write(&ac_req);
    if (ret != PROTO_SUCCESS) {
        log_error("写入失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
    } else {
        log_info("✓ 目标温度已设置（显式指定 MS/TP）");
    }

    // ⭐ 无需清理！程序退出时自动清理

    log_info("========================================\n");
}

/* ========================================================================== */
/* 主函数                                                                     */
/* ========================================================================== */

int main(int argc, char *argv[]) {
    log_info("╔══════════════════════════════════════════════════════════════╗");
    log_info("║         BACnet 多数据链路层使用示例                          ║");
    log_info("╚══════════════════════════════════════════════════════════════╝\n");

    log_info("配置要求：");
    log_info("  1. config.yaml 中配置 network section（BACnet/IP）");
    log_info("  2. config.yaml 中配置 mstp section（串口路径、波特率等）");
    log_info("  3. CMakeLists.txt 中启用 BACDL_MULTIPLE=1");
    log_info("");
    
    log_info("⭐ 零配置架构特性：");
    log_info("  - 无需调用 plc_proto_init()    → 首次读写时自动初始化");
    log_info("  - 无需调用 plc_proto_deinit()  → 程序退出时自动清理（atexit）");
    log_info("  - 无需检查连接状态              → 读写时自动连接");
    log_info("");

    // 方式 1：自动路由（推荐）
    example_auto_routing();

    // 方式 2：显式指定（可选）
    example_explicit_datalink();

    log_info("╔══════════════════════════════════════════════════════════════╗");
    log_info("║  总结：                                                      ║");
    log_info("║  - 推荐使用自动路由（datalink_hint = AUTO）                  ║");
    log_info("║  - BACnet 协议栈会自动维护设备路由表                          ║");
    log_info("║  - 只在特殊场景下才显式指定数据链路层                         ║");
    log_info("║  - 完全零配置：无需初始化/清理，直接使用！                    ║");
    log_info("╚══════════════════════════════════════════════════════════════╝");

    return 0;  // ⭐ 自动清理：atexit 会调用 bacnet_cleanup()
}
