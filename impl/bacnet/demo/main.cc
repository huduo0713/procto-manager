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
    
    log_info("=== BACnet PLC 异步读写测试 ===");
    log_info("日志文件: bacnet.log");
    
    log_info("开始测试BACnet PLC异步读写功能");

    // --- 读取操作测试 ---
    log_info("");
    log_info("--- 测试读取操作 ---");

    // 分配值缓冲区
    bacnet_data_value_t read_value;

    // ✨ 使用新 API：简化的初始化宏
    bacnet_read_t read_req = BACNET_READ_INIT(
        5678,                      // 设备实例
        2,                         // OBJECT_ANALOG_VALUE
        1,                         // 对象实例
        85,                        // PROP_PRESENT_VALUE
        &read_value                // value缓冲区
    );

    log_info("正在读取设备 {} 的模拟值对象 1 的当前值...", read_req.device_instance);

    // 调用读取（新缓存机制：首次可能返回 PROTO_NO_DATA，需要等待后重试）
    int result = plc_proto_read(&read_req);
    
    log_info("plc_proto_read 返回结果: {}", result);

    if (result == PROTO_SUCCESS) {
        if (read_req.value->type == BACNET_DATA_REAL) {
            log_info("✅ 读取成功: 浮点值 = {:.2f}", read_req.value->value.real_value);
        } else if (read_req.value->type == BACNET_DATA_UNSIGNED) {
            log_info("✅ 读取成功: 无符号整数 = {}", read_req.value->value.unsigned_value);
        } else {
            log_info("✅ 读取成功: 其他类型 (type={})", static_cast<int>(read_req.value->type));
        }
    } else if (result == PROTO_NO_DATA) {
        log_info("📡 读取请求已发送，等待设备响应...");
        log_info("⏳ 等待 3 秒让设备响应...");
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // 再次读取（新缓存机制会返回缓存的结果）
        result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            if (read_req.value->type == BACNET_DATA_REAL) {
                log_info("✅ 延迟读取成功: 浮点值 = {:.2f}", read_req.value->value.real_value);
            } else if (read_req.value->type == BACNET_DATA_UNSIGNED) {
                log_info("✅ 延迟读取成功: 无符号整数 = {}", read_req.value->value.unsigned_value);
            } else {
                log_info("✅ 延迟读取成功: 其他类型 (type={})", static_cast<int>(read_req.value->type));
            }
        } else {
            log_info("⏰ 读取超时，设备未响应");
        }
    } else {
        log_info("❌ 读取请求发送失败: 错误码 {}", result);
    }

    // --- 写入操作测试 ---
    log_info("");
    log_info("--- 测试写入操作 ---");

    // ✨ 使用新 API：简化的初始化宏
    bacnet_write_t write_req = BACNET_WRITE_INIT(
        5678,                                          // 设备实例
        2,                                             // OBJECT_ANALOG_VALUE
        1,                                             // 对象实例
        85,                                            // PROP_PRESENT_VALUE
        ((bacnet_data_value_t){                        // 写入值
            .type = BACNET_DATA_REAL,
            .value = {.real_value = 25.5f}
        })
    );

    log_info("正在写入设备 {} 的模拟值对象 1 的当前值: {:.1f}",
           write_req.device_instance, write_req.value.value.real_value);

    // 调用写入
    result = plc_proto_write(&write_req);

    if (result == PROTO_SUCCESS) {
        log_info("✅ 写入请求已提交");
        log_info("⏳ 等待 2 秒让写入完成...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        log_info("✅ 写入操作完成");
    } else {
        log_info("❌ 写入请求失败: 错误码 {}", result);
    }

    // --- 验证写入结果（再次读取） ---
    log_info("");
    log_info("--- 验证写入结果 ---");

    log_info("重新读取以验证写入是否成功...");
    result = plc_proto_read(&read_req);

    if (result == PROTO_SUCCESS) {
        if (read_req.value->type == BACNET_DATA_REAL) {
            log_info("✅ 验证读取成功: 浮点值 = {:.2f}", read_req.value->value.real_value);
            if (fabs(read_req.value->value.real_value - 25.5f) < 0.01f) {
                log_info("🎉 写入验证成功！值已正确更新");
            } else {
                log_info("⚠️  写入可能未生效，期望值: 25.5, 实际值: {:.2f}",
                       read_req.value->value.real_value);
            }
        } else {
            log_info("✅ 验证读取成功: 其他类型 (type={})", static_cast<int>(read_req.value->type));
        }
    } else if (result == PROTO_NO_DATA) {
        log_info("📡 验证读取请求已发送，等待响应...");
        log_info("⏳ 等待 2 秒...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 再次读取
        result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS && read_req.value->type == BACNET_DATA_REAL) {
            log_info("✅ 延迟验证成功: 浮点值 = {:.2f}", read_req.value->value.real_value);
            if (fabs(read_req.value->value.real_value - 25.5f) < 0.01f) {
                log_info("🎉 写入验证成功！值已正确更新");
            } else {
                log_info("⚠️  写入可能未生效，期望值: 25.5, 实际值: {:.2f}",
                       read_req.value->value.real_value);
            }
        } else {
            log_info("❌ 验证读取失败");
        }
    } else {
        log_info("❌ 验证读取请求失败: 错误码 {}", result);
    }

    log_info("");
    log_info("=== 测试完成 ===");
    log_info("注意: 如果测试失败，请检查:");
    log_info("1. BACnet设备是否在网络上且实例ID为5678");
    log_info("2. 设备是否有模拟值对象1");
    log_info("3. 网络配置是否正确");
    log_info("4. 配置文件config.yaml是否正确");

    return 0;
}
