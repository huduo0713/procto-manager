#include "impl/bacnet/src/proto_bacnet.h"
#include "common/utils/one_logger.hpp"
#include <bacnet/bacenum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// 测试并发读取多个对象
void test_concurrent_reads() {
    log_info("🔄 测试并发读取多个对象的值");
    log_info("================================");

    // 定义要读取的对象列表
    struct {
        uint16_t object_type;
        uint32_t object_instance;
        const char* name;
    } objects[] = {
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1"},
        {OBJECT_ANALOG_OUTPUT, 1, "analog-output-1"},
        {OBJECT_ANALOG_VALUE, 1, "analog-value-1"},
        {OBJECT_BINARY_INPUT, 1, "binary-input-1"},
        {OBJECT_BINARY_OUTPUT, 1, "binary-output-1"},
        {OBJECT_BINARY_VALUE, 1, "binary-value-1"},
        {OBJECT_INTEGER_VALUE, 1, "integer-value-1"},
        {OBJECT_CHARACTERSTRING_VALUE, 1, "characterstring-value-1"}
    };

    const int NUM_OBJECTS = sizeof(objects) / sizeof(objects[0]);
    bacnet_data_value_t values[NUM_OBJECTS];

    log_info("📡 第1轮：批量提交 {} 个读请求...", NUM_OBJECTS);

    // 第1轮：提交所有请求（使用简化的初始化方式）
    for (int i = 0; i < NUM_OBJECTS; i++) {
        // ✨ 新API：只需填写四元组 + value缓冲区，其他使用默认值
        bacnet_read_t read_req = BACNET_READ_INIT(
            5678,                      // 设备实例
            objects[i].object_type,    // 对象类型
            objects[i].object_instance,// 对象实例
            PROP_PRESENT_VALUE,        // 属性ID
            &values[i]                 // value缓冲区
        );

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            log_info("✅ {} - 从缓存获取到数据", objects[i].name);
        } else if (result == PROTO_NO_DATA) {
            log_info("📤 {} - 请求已发送，等待响应", objects[i].name);
        } else {
            log_info("❌ {} - {}", objects[i].name, proto_status_to_string((proto_status_t)result));
        }
    }

    log_info("");
    log_info("⏳ 等待 4 秒，让慢速设备响应到达 (analog-input-1 需要~3秒)...");
    sleep(4);

    log_info("");
    log_info("📡 第2轮：检查结果...");

    // 第2轮：检查结果
    int success_count = 0;
    for (int i = 0; i < NUM_OBJECTS; i++) {
        // ✨ 新API：只需填写四元组 + value缓冲区
        bacnet_read_t read_req = BACNET_READ_INIT(
            5678,
            objects[i].object_type,
            objects[i].object_instance,
            PROP_PRESENT_VALUE,
            &values[i]
        );

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            switch (values[i].type) {
                case BACNET_DATA_REAL:
                    log_info("✅ {} = {:.2f}", objects[i].name, values[i].value.real_value);
                    break;
                case BACNET_DATA_BOOLEAN:
                    log_info("✅ {} = {}", objects[i].name, values[i].value.boolean_value ? "TRUE" : "FALSE");
                    break;
                case BACNET_DATA_UNSIGNED:
                    log_info("✅ {} = {}", objects[i].name, values[i].value.unsigned_value);
                    break;
                case BACNET_DATA_SIGNED:
                    log_info("✅ {} = {}", objects[i].name, values[i].value.signed_value);
                    break;
                case BACNET_DATA_ENUM:
                    log_info("✅ {} = {}", objects[i].name, values[i].value.enum_value);
                    break;
                case BACNET_DATA_CHARACTER_STRING:
                    log_info("✅ {} = '{}'", objects[i].name,
                           std::string((char*)values[i].value.character_string.data, values[i].value.character_string.length));
                    break;
                default:
                    log_info("✅ {} = (类型:{})", objects[i].name, static_cast<int>(values[i].type));
                    break;
            }
            success_count++;
        } else if (result == PROTO_NO_DATA) {
            log_info("⏳ {} - 仍在等待响应", objects[i].name);
        } else {
            log_info("❌ {} - {}", objects[i].name, proto_status_to_string((proto_status_t)result));
        }
    }

    log_info("");
    log_info("🎉 成功读取 {}/{} 个对象！", success_count, NUM_OBJECTS);
}

// 测试重复读取同一个对象
void test_repeated_reads() {
    log_info("");
    log_info("🔄 测试重复读取同一个对象 (模拟PLC轮询)");
    log_info("==========================================");

    const int NUM_READS = 5;
    bacnet_data_value_t value;

    log_info("📡 连续读取 analog-input-1 共 {} 次...", NUM_READS);
    log_info("");
    log_info("💡 注意: analog-input-1 首次响应可能需要3秒");
    log_info("");

    for (int i = 0; i < NUM_READS; i++) {
        log_info("[第 {} 次读取]", i + 1);

        // ✨ 新API：只需填写四元组 + value缓冲区
        bacnet_read_t read_req = BACNET_READ_INIT(
            5678,
            OBJECT_ANALOG_INPUT,
            1,
            PROP_PRESENT_VALUE,
            &value
        );

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            log_info("  ✅ 成功: {:.2f} (来自缓存)", value.value.real_value);
        } else if (result == PROTO_NO_DATA) {
            log_info("  📤 请求已发送，等待响应...");
            // 等待3.5秒让慢速设备响应到达
            log_info("  ⏳ 等待3.5秒让慢速设备响应...");
            usleep(3500000);  // 3.5秒,确保analog-input-1能响应
            // 再试一次
            result = plc_proto_read(&read_req);
            if (result == PROTO_SUCCESS) {
                log_info("  ✅ 延迟获取: {:.2f}", value.value.real_value);
            } else {
                log_info("  ⏳ 仍在等待 (设备可能响应非常慢)");
            }
        } else {
            log_info("  ❌ {}", proto_status_to_string((proto_status_t)result));
        }

        log_info("");
        sleep(1);  // 间隔1秒
    }

    log_info("🎉 重复读取测试完成！");
}

// 测试并发写入
void test_concurrent_writes() {
    log_info("");
    log_info("✏️ 测试并发写入多个对象");
    log_info("======================");

    struct {
        uint16_t object_type;
        uint32_t object_instance;
        const char* name;
        bacnet_data_value_t value;
    } writes[] = {
        {OBJECT_ANALOG_OUTPUT, 1, "analog-output-1",
         {BACNET_DATA_REAL, {.real_value = 25.5f}}},
        {OBJECT_ANALOG_VALUE, 1, "analog-value-1",
         {BACNET_DATA_REAL, {.real_value = 75.0f}}},
        {OBJECT_BINARY_OUTPUT, 1, "binary-output-1",
         {BACNET_DATA_ENUM, {.enum_value = 1}}},
        {OBJECT_BINARY_VALUE, 1, "binary-value-1",
         {BACNET_DATA_ENUM, {.enum_value = 0}}},
        {OBJECT_INTEGER_VALUE, 1, "integer-value-1",
         {BACNET_DATA_SIGNED, {.signed_value = 42}}}
    };

    const int NUM_WRITES = sizeof(writes) / sizeof(writes[0]);

    log_info("📝 批量提交 {} 个写请求...", NUM_WRITES);

    for (int i = 0; i < NUM_WRITES; i++) {
        // ✨ 新API：只需填写四元组 + value
        bacnet_write_t write_req = BACNET_WRITE_INIT(
            5678,
            writes[i].object_type,
            writes[i].object_instance,
            PROP_PRESENT_VALUE,
            writes[i].value
        );

        int result = plc_proto_write(&write_req);
        if (result == PROTO_SUCCESS) {
            switch (writes[i].value.type) {
                case BACNET_DATA_REAL:
                    log_info("✅ 已提交写入 {} = {:.2f}", writes[i].name, writes[i].value.value.real_value);
                    break;
                case BACNET_DATA_ENUM:
                    log_info("✅ 已提交写入 {} = {}", writes[i].name, writes[i].value.value.enum_value);
                    break;
                case BACNET_DATA_SIGNED:
                    log_info("✅ 已提交写入 {} = {}", writes[i].name, writes[i].value.value.signed_value);
                    break;
                default:
                    log_info("✅ 已提交写入 {} = (类型:{})", writes[i].name, static_cast<int>(writes[i].value.type));
                    break;
            }
        } else {
            log_info("❌ 提交写入 {} 失败: {}", writes[i].name, 
                   proto_status_to_string((proto_status_t)result));
        }
    }

    log_info("");
    log_info("⏳ 等待 2 秒让写入完成...");
    sleep(2);

    log_info("🎉 写入测试完成！");
}

// 测试PLC高频轮询场景
void test_plc_polling() {
    log_info("");
    log_info("🔁 测试PLC高频轮询场景 (100ms间隔)");
    log_info("====================================");
    log_info("💡 这模拟了PLC每100ms读取一次的真实场景");
    log_info("");

    bacnet_data_value_t value;
    const int POLL_COUNT = 20;  // 轮询20次
    int success_count = 0;

    for (int i = 0; i < POLL_COUNT; i++) {

        // ✨ 新API：只需填写四元组 + value缓冲区
        bacnet_read_t read_req = BACNET_READ_INIT(
            5678,
            OBJECT_ANALOG_INPUT,
            1,
            PROP_PRESENT_VALUE,
            &value
        );

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            log_info("[轮询 #{}] ✅ {:.2f} (缓存命中)", i + 1, value.value.real_value);
            success_count++;
        } else if (result == PROTO_NO_DATA) {
            log_info("[轮询 #{}] 📤 等待响应中...", i + 1);
        } else {
            log_info("[轮询 #{}] ❌ {}", i + 1, proto_status_to_string((proto_status_t)result));
        }

        usleep(100000);  // 100ms间隔
    }

    log_info("");
    log_info("📊 统计: 成功获取 {}/{} 次 ({:.1f}%)", 
           success_count, POLL_COUNT, 
           (float)success_count / POLL_COUNT * 100);
    log_info("🎉 PLC轮询测试完成！");
}

int main() {
    log_info("🚀 BACnet 缓存机制测试程序");
    log_info("目标设备: 5678 (Living Room Thermostat)");
    log_info("==========================================");
    log_info("");

    // 测试1: 并发读取
    test_concurrent_reads();

    // 测试2: 重复读取
    test_repeated_reads();

    // 测试3: 并发写入
    test_concurrent_writes();

    // 测试4: PLC高频轮询
    test_plc_polling();

    log_info("");
    log_info("🎉 所有测试完成！");
    log_info("💡 新缓存机制特点：");
    log_info("   ✅ 基于对象的缓存，不依赖invoke_id");
    log_info("   ✅ 支持PLC高频轮询场景");
    log_info("   ✅ 激进策略：每次都发送请求，尽可能获取最新数据");
    log_info("   ✅ 简化的API：不需要事件循环，直接同步调用");
    log_info("   ✅ 自动资源清理：程序退出时自动清理，无需手动调用");

    return 0;
}
