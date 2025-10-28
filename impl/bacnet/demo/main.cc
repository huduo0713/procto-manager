#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <chrono>
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h"
#include "proto_bacnet.h" // 包含 BACnet 特定配置结构体
#include "common/utils/one_logger.hpp"

int main() {
    // 初始化日志系统
    one_logger->info("=== BACnet PLC 异步读写测试开始 ===");
    
    printf("=== BACnet PLC 异步读写测试 ===\n");
    printf("日志文件: bacnet.log\n");
    
    log_info("开始测试BACnet PLC异步读写功能");

    // --- 读取操作测试 ---
    printf("\n--- 测试读取操作 ---\n");

    // 准备读取请求
    bacnet_read_t read_req = {
        .device_instance = 5678,      // 目标设备实例ID
        .object_type = 2,             // OBJECT_ANALOG_VALUE = 2
        .object_instance = 1,         // 对象实例 1
        .property_id = 85,            // PROP_PRESENT_VALUE = 85
        .array_index = -1,            // 不使用数组索引
        .timeout_ms = 6000,           // 6秒超时
        .value = nullptr,             // 稍后分配
        .check_only = false           // 默认发送请求
    };

    // 分配值缓冲区
    bacnet_data_value_t read_value;
    read_req.value = &read_value;

    printf("正在读取设备 %u 的模拟值对象 1 的当前值...\n", read_req.device_instance);
    log_info("准备调用 plc_proto_read，设备实例: {}", read_req.device_instance);

    // 调用异步读取
    int result = plc_proto_read(&read_req);
    
    log_info("plc_proto_read 返回结果: {}", result);
    printf("plc_proto_read 返回: %d\n", result);

    if (result == PROTO_SUCCESS) {
        printf("✅ 读取成功: ");
        if (read_req.value->type == BACNET_DATA_REAL) {
            printf("浮点值 = %.2f\n", read_req.value->value.real_value);
        } else if (read_req.value->type == BACNET_DATA_UNSIGNED) {
            printf("无符号整数 = %u\n", read_req.value->value.unsigned_value);
        } else {
            printf("其他类型 (type=%d)\n", read_req.value->type);
        }
    } else if (result == PROTO_NO_DATA) {
        printf("📡 读取请求已发送，等待设备响应...\n");

        // 轮询等待结果（最多等待10次，每次1秒）
        // 注意：不要在等待期间再次调用 plc_proto_read，这会导致发送重复请求
        bool got_result = false;
        for (int attempt = 0; attempt < 10; ++attempt) {
            printf("  等待中... (%d/10)\n", attempt + 1);
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // 检查读取队列是否有数据（使用相同的请求结构体）
            bacnet_read_t check_req = read_req;  // 复制请求
            check_req.check_only = true;  // 仅检查队列，不发送新请求
            int check_result = plc_proto_read(&check_req);
            if (check_result == PROTO_SUCCESS) {
                // 复制结果到原始请求
                *read_req.value = *check_req.value;
                printf("✅ 读取成功: ");
                if (read_req.value->type == BACNET_DATA_REAL) {
                    printf("浮点值 = %.2f\n", read_req.value->value.real_value);
                } else if (read_req.value->type == BACNET_DATA_UNSIGNED) {
                    printf("无符号整数 = %u\n", read_req.value->value.unsigned_value);
                } else {
                    printf("其他类型 (type=%d)\n", read_req.value->type);
                }
                got_result = true;
                break;
            } else if (check_result == PROTO_NO_DATA) { // PROTO_NO_DATA
                continue; // 继续等待
            } else {
                printf("❌ 读取失败: 错误码 %d\n", check_result);
                break;
            }
        }

        if (!got_result) {
            printf("⏰ 读取超时，设备未响应\n");
        }
    } else {
        printf("❌ 读取请求发送失败: 错误码 %d\n", result);
    }

    // --- 写入操作测试 ---
    printf("\n--- 测试写入操作 ---\n");

    // 准备写入请求
    bacnet_write_t write_req = {
        .device_instance = 5678,      // 目标设备实例ID
        .object_type = 2,             // OBJECT_ANALOG_VALUE = 2
        .object_instance = 1,         // 对象实例 1
        .property_id = 85,            // PROP_PRESENT_VALUE = 85
        .array_index = -1,            // 不使用数组索引
        .priority = 8,                // 默认优先级
        .timeout_ms = 6000,           // 6秒超时
        .value = {
            .type = BACNET_DATA_REAL,
            .value = {.real_value = 25.5f}  // 写入25.5
        }
    };

    printf("正在写入设备 %u 的模拟值对象 1 的当前值: %.1f\n",
           write_req.device_instance, write_req.value.value.real_value);

    // 调用异步写入
    result = plc_proto_write(&write_req);

    if (result == PROTO_SUCCESS) {
        printf("✅ 写入请求已提交到队列\n");

        // 写入是异步的，我们可以选择等待一段时间让工作线程处理
        printf("  等待工作线程处理写入请求...\n");
        std::this_thread::sleep_for(std::chrono::seconds(2));

        printf("✅ 写入操作完成\n");
    } else {
        printf("❌ 写入请求失败: 错误码 %d\n", result);
    }

    // --- 验证写入结果（再次读取） ---
    printf("\n--- 验证写入结果 ---\n");

    printf("重新读取以验证写入是否成功...\n");
    result = plc_proto_read(&read_req);

    if (result == PROTO_SUCCESS) {
        printf("✅ 验证读取成功: ");
        if (read_req.value->type == BACNET_DATA_REAL) {
            printf("浮点值 = %.2f\n", read_req.value->value.real_value);
            if (fabs(read_req.value->value.real_value - 25.5f) < 0.01f) {
                printf("🎉 写入验证成功！值已正确更新\n");
            } else {
                printf("⚠️  写入可能未生效，期望值: 25.5, 实际值: %.2f\n",
                       read_req.value->value.real_value);
            }
        } else {
            printf("其他类型 (type=%d)\n", read_req.value->type);
        }
    } else if (result == PROTO_NO_DATA) {
        printf("📡 验证读取请求已发送，等待响应...\n");
        
        // 等待验证结果（等待更短时间，因为写入刚刚完成）
        bool got_result = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // 检查读取队列是否有数据
            bacnet_read_t check_req = read_req;  // 复制请求
            check_req.check_only = true;  // 仅检查队列，不发送新请求
            int check_result = plc_proto_read(&check_req);
            if (check_result == PROTO_SUCCESS) {
                // 复制结果到原始请求
                *read_req.value = *check_req.value;
                printf("✅ 验证读取成功: ");
                if (read_req.value->type == BACNET_DATA_REAL) {
                    printf("浮点值 = %.2f\n", read_req.value->value.real_value);
                    if (fabs(read_req.value->value.real_value - 25.5f) < 0.01f) {
                        printf("🎉 写入验证成功！值已正确更新\n");
                    } else {
                        printf("⚠️  写入可能未生效，期望值: 25.5, 实际值: %.2f\n",
                               read_req.value->value.real_value);
                    }
                } else {
                    printf("其他类型 (type=%d)\n", read_req.value->type);
                }
                got_result = true;
                break;
            } else if (check_result == PROTO_NO_DATA) {
                continue; // 继续等待
            } else {
                printf("❌ 验证读取失败: 错误码 %d\n", check_result);
                break;
            }
        }

        if (!got_result) {
            printf("❌ 验证读取失败\n");
        }
    } else {
        printf("❌ 验证读取请求失败: 错误码 %d\n", result);
    }

    printf("\n=== 测试完成 ===\n");
    printf("注意: 如果测试失败，请检查:\n");
    printf("1. BACnet设备是否在网络上且实例ID为5678\n");
    printf("2. 设备是否有模拟值对象1\n");
    printf("3. 网络配置是否正确\n");
    printf("4. 配置文件config.yaml是否正确\n");

    return 0;
}
