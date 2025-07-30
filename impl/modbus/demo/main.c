#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/api/proto_common.h"
#include "proto_modbus.h"

int main() {

    // 准备协议配置数据
    modbus_config_t config = {
        .ip = "192.168.11.78",
        .port = 1502,
        .slave_id = 1
    };

    proto_ctx_t ctx = {
        .type = PROTO_TYPE_MODBUS,
        .config = &config,
        .client = NULL,
        .userdata = NULL
    };

    // 协议初始化
    if (proto_modbus_driver_init(&ctx) != PROTO_SUCCESS) {
        printf("Init failed\n");
        return -1;
    }

    //协议连接
    if (proto_modbus_connect(&ctx) != PROTO_SUCCESS) {
        printf("Connect failed\n");
        return -1;
    }

    uint16_t data[2] = {0};
    proto_request_t req = {
        .resource_name = "HoldingReg",
        .address = 99,
        .quantity = 2,
        .value = data
    };

    // 读取地址99， 长度2的寄存器地址并打印
    if (proto_modbus_read(&ctx, &req) == PROTO_SUCCESS) {
        printf("Read success: %u %u\n", data[0], data[1]);
    } else {
        printf("Read failed\n");
    }

    data[0] = 123;
    data[1] = 456;
    // 连续写入2个值，地址99开始，并打印结果
    if (proto_modbus_write(&ctx, &req) == PROTO_SUCCESS) {
        printf("Write success\n");
    } else {
        printf("Write failed\n");
    }

    // 断开和销毁对象
    proto_modbus_disconnect(&ctx);
    proto_modbus_driver_release(&ctx);
    return 0;
}
