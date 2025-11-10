/**
 * @file test_hot_config.cc
 * @brief 热配置自动监控测试程序
 * 
 * 功能演示：
 * 1. 启动程序后自动监控 config.yaml
 * 2. 编辑配置文件并保存
 * 3. 1秒内自动检测到变化并重载配置
 * 4. 无需重启程序，无需发送信号
 */

#include <stdio.h>
#include <thread>
#include <chrono>
#include "common/api/proto_common.h"
#include "proto_bacnet.h"
#include "common/utils/one_logger.hpp"

int main() {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║         🔥 BACnet 热配置自动监控测试                          ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    log_info("[Test] Starting hot config monitoring test");
    
    printf("📋 测试步骤：\n");
    printf("1. 程序启动后会自动监控 ../config.yaml\n");
    printf("2. 在另一个终端编辑配置文件：vim ../config.yaml\n");
    printf("3. 修改任意配置项（如 log_level），保存退出\n");
    printf("4. 观察本程序输出，配置会在 1 秒内自动重载\n");
    printf("5. 按 Ctrl+C 退出测试\n");
    printf("\n");
    
    // 准备一个简单的读取请求
    bacnet_data_value_t read_value;
    bacnet_read_t read_req = {
        .device_instance = 5678,
        .object_type = 0,              // ANALOG_INPUT
        .object_instance = 1,
        .property_id = 85,             // PRESENT_VALUE
        .value = &read_value,
        .array_index = -1,
        .timeout_ms = 5000,
        .check_only = false
    };
    
    printf("🚀 初始化 BACnet 驱动...\n");
    log_info("[Test] Calling first plc_proto_read to trigger initialization");
    
    // 第一次调用会触发初始化和自动启动监控
    int result = plc_proto_read(&read_req);
    
    if (result == PROTO_SUCCESS) {
        printf("✅ BACnet 驱动初始化成功\n");
        printf("✅ 热配置监控已自动启动\n");
    } else {
        printf("❌ 初始化失败: %d\n", result);
        printf("   请检查设备连接和配置文件\n");
    }
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("🔍 监控中... (每 10 秒执行一次读取操作)\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // 持续运行，定期执行读取操作
    int loop_count = 0;
    while (true) {
        loop_count++;
        
        printf("[%02d] 执行读取操作 (device=%u, object=AI-%u)...\n", 
               loop_count, read_req.device_instance, read_req.object_instance);
        
        result = plc_proto_read(&read_req);
        
        if (result == PROTO_SUCCESS) {
            if (read_value.type == BACNET_DATA_REAL) {
                printf("     ✅ 读取成功: %.2f\n", read_value.value.real_value);
            } else if (read_value.type == BACNET_DATA_UNSIGNED) {
                printf("     ✅ 读取成功: %u\n", read_value.value.unsigned_value);
            } else {
                printf("     ✅ 读取成功: type=%d\n", read_value.type);
            }
        } else {
            printf("     ⚠️  读取失败: %d\n", result);
        }
        
        printf("     💤 等待 10 秒...\n");
        printf("\n");
        
        // 在这 10 秒内，如果配置文件被修改，监控线程会自动检测并重载
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    return 0;
}
