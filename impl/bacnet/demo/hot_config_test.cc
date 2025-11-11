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
    log_info("");
    log_info("╔════════════════════════════════════════════════════════════════╗");
    log_info("║         🔥 BACnet 热配置自动监控测试                          ║");
    log_info("╚════════════════════════════════════════════════════════════════╝");
    log_info("");
    
    log_info("[Test] Starting hot config monitoring test");
    
    log_info("📋 测试步骤：");
    log_info("1. 程序启动后会自动监控 ../config.yaml");
    log_info("2. 在另一个终端编辑配置文件：vim ../config.yaml");
    log_info("3. 修改任意配置项（如 log_level），保存退出");
    log_info("4. 观察本程序输出，配置会在 1 秒内自动重载");
    log_info("5. 按 Ctrl+C 退出测试");
    log_info("");
    
    // 准备一个简单的读取请求
    bacnet_data_value_t read_value;
    
    // ✨ 使用新 API：简化的初始化宏
    bacnet_read_t read_req = BACNET_READ_INIT(
        5678,              // 设备实例
        0,                 // ANALOG_INPUT
        1,                 // 对象实例
        85,                // PRESENT_VALUE
        &read_value        // value缓冲区
    );
    
    log_info("🚀 初始化 BACnet 驱动...");
    log_info("[Test] Calling first plc_proto_read to trigger initialization");
    
    // 第一次调用会触发初始化和自动启动监控
    int result = plc_proto_read(&read_req);
    
    if (result == PROTO_SUCCESS) {
        log_info("✅ BACnet 驱动初始化成功");
        log_info("✅ 热配置监控已自动启动");
    } else {
        log_info("❌ 初始化失败: {}", result);
        log_info("   请检查设备连接和配置文件");
    }
    
    log_info("");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("🔍 监控中... (每 10 秒执行一次读取操作)");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("");
    
    // 持续运行，定期执行读取操作
    int loop_count = 0;
    while (true) {
        loop_count++;
        
        log_info("[{}] 执行读取操作 (device={}, object=AI-{})...", 
               loop_count, read_req.device_instance, read_req.object_instance);
        
        result = plc_proto_read(&read_req);
        
        if (result == PROTO_SUCCESS) {
            if (read_value.type == BACNET_DATA_REAL) {
                log_info("     ✅ 读取成功: {:.2f}", read_value.value.real_value);
            } else if (read_value.type == BACNET_DATA_UNSIGNED) {
                log_info("     ✅ 读取成功: {}", read_value.value.unsigned_value);
            } else {
                log_info("     ✅ 读取成功: type={}", static_cast<int>(read_value.type));
            }
        } else {
            log_info("     ⚠️  读取失败: {}", result);
        }
        
        log_info("     💤 等待 10 秒...");
        log_info("");
        
        // 在这 10 秒内，如果配置文件被修改，监控线程会自动检测并重载
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    return 0;
}
