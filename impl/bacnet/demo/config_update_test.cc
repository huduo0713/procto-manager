/**
 * @file config_update_test.cc
 * @brief BACnet 配置更新 API 测试程序
 * 
 * 功能演示：
 * - 使用 config_update() 动态修改配置文件
 * - 如果启用热配置，修改会自动生效
 */

#include "proto_bacnet.h"
#include "common/utils/one_logger.hpp"
#include <cstring>
#include <thread>
#include <chrono>

int main() {
    log_info("");
    log_info("╔════════════════════════════════════════════════════════════════╗");
    log_info("║         🔧 BACnet 配置更新 API 测试                           ║");
    log_info("╚════════════════════════════════════════════════════════════════╝");
    log_info("");

    // ========================================================================
    // 测试场景 1: 更新服务超时配置
    // ========================================================================
    
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("📝 测试场景 1: 更新服务超时配置");
    log_info("═══════════════════════════════════════════════════════════════");
    
    bacnet_config_t cfg1 = {0};
    cfg1.bacnet.services.read_timeout_ms = 8000;
    cfg1.bacnet.services.write_timeout_ms = 8000;
    
    log_info("🔧 修改配置:");
    log_info("  - read_timeout_ms  : 8000");
    log_info("  - write_timeout_ms : 8000");
    
    int ret = config_update(&cfg1);
    if (ret != PROTO_SUCCESS) {
        log_error("❌ 配置更新失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
        return -1;
    }
    
    log_info("✅ 配置更新成功！");
    log_info("");
    
    // 等待热配置生效
    log_info("⏳ 等待 2 秒（热配置监控会自动生效）...");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // ========================================================================
    // 测试场景 2: 更新设备发现配置
    // ========================================================================
    
    log_info("");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("📝 测试场景 2: 更新设备发现配置");
    log_info("═══════════════════════════════════════════════════════════════");
    
    bacnet_config_t cfg2 = {0};
    cfg2.bacnet.discovery.target_device_start = 6000;
    cfg2.bacnet.discovery.target_device_end = 6100;
    cfg2.bacnet.discovery.whois_retry = 5;
    
    log_info("🔧 修改配置:");
    log_info("  - target_device_start : 6000");
    log_info("  - target_device_end   : 6100");
    log_info("  - whois_retry         : 5");
    
    ret = config_update(&cfg2);
    if (ret != PROTO_SUCCESS) {
        log_error("❌ 配置更新失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
        return -1;
    }
    
    log_info("✅ 配置更新成功！");
    log_info("");
    
    // ========================================================================
    // 测试场景 3: 更新缓存策略
    // ========================================================================
    
    log_info("");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("📝 测试场景 3: 更新缓存策略");
    log_info("═══════════════════════════════════════════════════════════════");
    
    bacnet_config_t cfg3 = BACNET_CONFIG_INIT;
    cfg3.bacnet.services.cache_strategy = 1;  // 0=激进, 1=保守
    cfg3.bacnet.services.cache_expiry_ms = 2000;
    
    log_info("🔧 修改配置:");
    log_info("  - cache_strategy   : 1 (保守策略)");
    log_info("  - cache_expiry_ms  : 2000");
    
    ret = config_update(&cfg3);
    if (ret != PROTO_SUCCESS) {
        log_error("❌ 配置更新失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
        return -1;
    }
    
    log_info("✅ 配置更新成功！");
    log_info("");
    
    // ========================================================================
    // 测试场景 4: 更新热配置监控参数
    // ========================================================================
    
    log_info("");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("📝 测试场景 4: 更新热配置监控参数");
    log_info("═══════════════════════════════════════════════════════════════");
    
    bacnet_config_t cfg4 = BACNET_CONFIG_INIT;
    cfg4.bacnet.hot_config.enabled = 1;  // -1=未设置, 0=false, 1=true
    cfg4.bacnet.hot_config.polling_interval_ms = 500;
    
    log_info("🔧 修改配置:");
    log_info("  - hot_config.enabled            : true");
    log_info("  - hot_config.polling_interval_ms: 500");
    
    ret = config_update(&cfg4);
    if (ret != PROTO_SUCCESS) {
        log_error("❌ 配置更新失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
        return -1;
    }
    
    log_info("✅ 配置更新成功！");
    log_info("");
    
    // ========================================================================
    // 测试场景 5: 批量更新多个配置项
    // ========================================================================
    
    log_info("");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("📝 测试场景 5: 批量更新多个配置项");
    log_info("═══════════════════════════════════════════════════════════════");
    
    bacnet_config_t cfg5 = BACNET_CONFIG_INIT;
    std::strcpy(cfg5.common.log_level, "info");
    cfg5.bacnet.services.read_timeout_ms = 5000;
    cfg5.bacnet.services.write_timeout_ms = 5000;
    cfg5.bacnet.connection.max_reconnect_attempts = 10;
    cfg5.bacnet.connection.reconnect_interval_ms = 2000;
    
    log_info("🔧 修改配置:");
    log_info("  - common.log_level              : info");
    log_info("  - services.read_timeout_ms      : 5000");
    log_info("  - services.write_timeout_ms     : 5000");
    log_info("  - connection.max_reconnect      : 10");
    log_info("  - connection.reconnect_interval : 2000");
    
    ret = config_update(&cfg5);
    if (ret != PROTO_SUCCESS) {
        log_error("❌ 配置更新失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
        return -1;
    }
    
    log_info("✅ 配置更新成功！");
    log_info("");
    
    // ========================================================================
    // 总结
    // ========================================================================
    
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("🎉 所有测试完成！");
    log_info("═══════════════════════════════════════════════════════════════");
    log_info("");
    log_info("📝 使用说明:");
    log_info("1. config_update() 直接修改 config.yaml 文件");
    log_info("2. 只需填写需要修改的字段，其他字段保持不变");
    log_info("3. 如果启用了热配置监控，修改会自动生效");
    log_info("4. 查看 ../config.yaml 确认修改结果");
    log_info("");
    log_info("⚠️  注意:");
    log_info("- 本测试程序仅修改配置文件，不启动 BACnet 驱动");
    log_info("- 如需验证配置生效，请运行 hot_config_test");
    log_info("");

    return 0;
}
