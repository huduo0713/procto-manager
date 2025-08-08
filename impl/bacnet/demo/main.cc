#include <stdio.h>
#include <stdlib.h>
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h"
#include "proto_bacnet.h" // 包含 BACnet 特定配置结构体
#include "common/utils/one_logger.hpp"

int main() {
    // --- 准备 BACnet 协议配置 ---
    bacnet_config_t bacnet_conf = {
        5678 // 目标设备的实例ID
    };

    // --- 使用通用上下文结构体 ---
    proto_ctx_t ctx = {
        PROTO_TYPE_BACNET,
        NULL,
        &bacnet_conf,
        NULL
    };

    // --- 调用通用的驱动接口 ---

    // 协议初始化
    if (proto_driver_init(&ctx) != PROTO_SUCCESS) {
        printf("Init failed\n");
        return -1;
    }

    // 协议连接
    if (proto_connect(&ctx) != PROTO_SUCCESS) {
        printf("Connect failed\n");
        proto_driver_release(&ctx);
        return -1;
    }

    // --- 读取操作 ---
    float temp_value = 0.0f;
    proto_request_t read_req = {
        "analog-output:1:present-value",
        0, // BACnet 中忽略
        0, // BACnet 中忽略
        &temp_value // 传入一个浮点数指针用于接收返回值
    };

    printf("\n--- 正在读取 'analog-output:1:present-value' ---\n");
    if (proto_read(&ctx, &read_req) == PROTO_SUCCESS) {
        printf("读取成功: %.2f\n", temp_value);
    } else {
        printf("读取失败\n");
    }

    // --- 写入操作 ---
    float setpoint_value = 23.5f;
    proto_request_t write_req = {
        "analog-output:1:present-value", // 假设设备上有 AV,1
        0,
        0,
        &setpoint_value // 传入一个包含要写入值的浮点数指针
    };

    printf("\n--- 正在写入 'analog-output:1:present-value' ---\n");
    if (proto_write(&ctx, &write_req) == PROTO_SUCCESS) {
        printf("写入成功\n");
    } else {
        printf("写入失败\n");
    }

    // 断开和销毁对象
    proto_disconnect(&ctx);
    proto_driver_release(&ctx);

    return 0;
}
