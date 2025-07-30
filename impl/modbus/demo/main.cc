#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h"
#include "proto_modbus.h"
#include "common/utils/one_logger.hpp"

int main() {

    // 准备协议配置数据
    modbus_config_t config = {
        "192.168.11.78",
        1502,
        1
    };

    proto_ctx_t ctx = {
        PROTO_TYPE_MODBUS,
        NULL,
        &config,
        NULL
    };

    // 协议初始化
    if (proto_driver_init(&ctx) != PROTO_SUCCESS) {
        log_info("Init failed\n");
        return -1;
    }

    //协议连接
    if (proto_connect(&ctx) != PROTO_SUCCESS) {
        log_info("Connect failed\n");
        return -1;
    }

    uint16_t data[2] = {0};
    proto_request_t req = {
        "HoldingReg",
        99,
        2,
        data
    };

    // 读取地址99， 长度2的寄存器地址并打印
    if (proto_read(&ctx, &req) == PROTO_SUCCESS) {
        log_info("Read success: {} {}\n", data[0], data[1]);
    } else {
        log_info("Read failed\n");
    }

    data[0] = 123;
    data[1] = 456;
    // 连续写入2个值，地址99开始，并打印结果
    if (proto_write(&ctx, &req) == PROTO_SUCCESS) {
        log_info("Write success\n");
    } else {
        log_info("Write failed\n");
    }

    // 断开和销毁对象
    proto_disconnect(&ctx);
    proto_driver_release(&ctx);
    return 0;
}
