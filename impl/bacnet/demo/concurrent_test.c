#include "impl/bacnet/src/proto_bacnet.h"
#include <bacnet/bacenum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// 测试并发读取多个对象
void test_concurrent_reads() {
    printf("🔄 测试并发读取多个对象的值\n");
    printf("================================\n");

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

    printf("📡 第1轮：批量提交 %d 个读请求...\n", NUM_OBJECTS);

    // 第1轮：提交所有请求
    for (int i = 0; i < NUM_OBJECTS; i++) {
        bacnet_read_t read_req = {
            .device_instance = 5678,
            .object_type = objects[i].object_type,
            .object_instance = objects[i].object_instance,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &values[i]
        };

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            printf("✅ %s - 从缓存获取到数据\n", objects[i].name);
        } else if (result == PROTO_NO_DATA) {
            printf("📤 %s - 请求已发送，等待响应\n", objects[i].name);
        } else {
            printf("❌ %s - 失败: %d\n", objects[i].name, result);
        }
    }

    printf("\n⏳ 等待 4 秒，让慢速设备响应到达 (analog-input-1 需要~3秒)...\n");
    sleep(4);

    printf("\n📡 第2轮：检查结果...\n");

    // 第2轮：检查结果
    int success_count = 0;
    for (int i = 0; i < NUM_OBJECTS; i++) {
        bacnet_read_t read_req = {
            .device_instance = 5678,
            .object_type = objects[i].object_type,
            .object_instance = objects[i].object_instance,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &values[i]
        };

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            printf("✅ %s = ", objects[i].name);
            switch (values[i].type) {
                case BACNET_DATA_REAL:
                    printf("%.2f\n", values[i].value.real_value);
                    break;
                case BACNET_DATA_BOOLEAN:
                    printf("%s\n", values[i].value.boolean_value ? "TRUE" : "FALSE");
                    break;
                case BACNET_DATA_UNSIGNED:
                    printf("%u\n", values[i].value.unsigned_value);
                    break;
                case BACNET_DATA_SIGNED:
                    printf("%d\n", values[i].value.signed_value);
                    break;
                case BACNET_DATA_ENUM:
                    printf("%u\n", values[i].value.enum_value);
                    break;
                case BACNET_DATA_CHARACTER_STRING:
                    printf("'%.*s'\n",
                           (int)values[i].value.character_string.length,
                           values[i].value.character_string.data);
                    break;
                default:
                    printf("(类型:%d)\n", values[i].type);
                    break;
            }
            success_count++;
        } else if (result == PROTO_NO_DATA) {
            printf("⏳ %s - 仍在等待响应\n", objects[i].name);
        } else {
            printf("❌ %s - 失败: %d\n", objects[i].name, result);
        }
    }

    printf("\n🎉 成功读取 %d/%d 个对象！\n", success_count, NUM_OBJECTS);
}

// 测试重复读取同一个对象
void test_repeated_reads() {
    printf("\n🔄 测试重复读取同一个对象 (模拟PLC轮询)\n");
    printf("==========================================\n");

    const int NUM_READS = 5;
    bacnet_data_value_t value;

    printf("📡 连续读取 analog-input-1 共 %d 次...\n\n", NUM_READS);
    printf("💡 注意: analog-input-1 首次响应可能需要3秒\n\n");

    for (int i = 0; i < NUM_READS; i++) {
        printf("[第 %d 次读取]\n", i + 1);

        bacnet_read_t read_req = {
            .device_instance = 5678,
            .object_type = OBJECT_ANALOG_INPUT,
            .object_instance = 1,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &value
        };

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            printf("  ✅ 成功: %.2f (来自缓存)\n", value.value.real_value);
        } else if (result == PROTO_NO_DATA) {
            printf("  📤 请求已发送，等待响应...\n");
            // 等待3.5秒让慢速设备响应到达
            printf("  ⏳ 等待3.5秒让慢速设备响应...\n");
            usleep(3500000);  // 3.5秒,确保analog-input-1能响应
            // 再试一次
            result = plc_proto_read(&read_req);
            if (result == PROTO_SUCCESS) {
                printf("  ✅ 延迟获取: %.2f\n", value.value.real_value);
            } else {
                printf("  ⏳ 仍在等待 (设备可能响应非常慢)\n");
            }
        } else {
            printf("  ❌ 失败: %d\n", result);
        }

        printf("\n");
        sleep(1);  // 间隔1秒
    }

    printf("🎉 重复读取测试完成！\n");
}

// 测试并发写入
void test_concurrent_writes() {
    printf("\n✏️ 测试并发写入多个对象\n");
    printf("======================\n");

    struct {
        uint16_t object_type;
        uint32_t object_instance;
        const char* name;
        bacnet_data_value_t value;
    } writes[] = {
        {OBJECT_ANALOG_OUTPUT, 1, "analog-output-1",
         {.type = BACNET_DATA_REAL, .value.real_value = 25.5f}},
        {OBJECT_ANALOG_VALUE, 1, "analog-value-1",
         {.type = BACNET_DATA_REAL, .value.real_value = 75.0f}},
        {OBJECT_BINARY_OUTPUT, 1, "binary-output-1",
         {.type = BACNET_DATA_ENUM, .value.enum_value = 1}},
        {OBJECT_BINARY_VALUE, 1, "binary-value-1",
         {.type = BACNET_DATA_ENUM, .value.enum_value = 0}},
        {OBJECT_INTEGER_VALUE, 1, "integer-value-1",
         {.type = BACNET_DATA_SIGNED, .value.signed_value = 42}}
    };

    const int NUM_WRITES = sizeof(writes) / sizeof(writes[0]);

    printf("📝 批量提交 %d 个写请求...\n", NUM_WRITES);

    for (int i = 0; i < NUM_WRITES; i++) {
        bacnet_write_t write_req = {
            .device_instance = 5678,
            .object_type = writes[i].object_type,
            .object_instance = writes[i].object_instance,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .priority = 8,
            .timeout_ms = 5000,
            .value = writes[i].value
        };

        int result = plc_proto_write(&write_req);
        if (result == PROTO_SUCCESS) {
            printf("✅ 已提交写入 %s = ", writes[i].name);
            switch (writes[i].value.type) {
                case BACNET_DATA_REAL:
                    printf("%.2f\n", writes[i].value.value.real_value);
                    break;
                case BACNET_DATA_ENUM:
                    printf("%u\n", writes[i].value.value.enum_value);
                    break;
                case BACNET_DATA_SIGNED:
                    printf("%d\n", writes[i].value.value.signed_value);
                    break;
                default:
                    printf("(类型:%d)\n", writes[i].value.type);
                    break;
            }
        } else {
            printf("❌ 提交写入 %s 失败: %d\n", writes[i].name, result);
        }
    }

    printf("\n⏳ 等待 2 秒让写入完成...\n");
    sleep(2);

    printf("🎉 写入测试完成！\n");
}

// 测试PLC高频轮询场景
void test_plc_polling() {
    printf("\n🔁 测试PLC高频轮询场景 (100ms间隔)\n");
    printf("====================================\n");
    printf("💡 这模拟了PLC每100ms读取一次的真实场景\n\n");

    bacnet_data_value_t value;
    const int POLL_COUNT = 20;  // 轮询20次
    int success_count = 0;

    for (int i = 0; i < POLL_COUNT; i++) {
        printf("[轮询 #%d] ", i + 1);

        bacnet_read_t read_req = {
            .device_instance = 5678,
            .object_type = OBJECT_ANALOG_INPUT,
            .object_instance = 1,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &value
        };

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            printf("✅ %.2f (缓存命中)\n", value.value.real_value);
            success_count++;
        } else if (result == PROTO_NO_DATA) {
            printf("📤 等待响应中...\n");
        } else {
            printf("❌ 错误: %d\n", result);
        }

        usleep(100000);  // 100ms间隔
    }

    printf("\n📊 统计: 成功获取 %d/%d 次 (%.1f%%)\n", 
           success_count, POLL_COUNT, 
           (float)success_count / POLL_COUNT * 100);
    printf("🎉 PLC轮询测试完成！\n");
}

int main() {
    printf("🚀 BACnet 缓存机制测试程序\n");
    printf("目标设备: 5678 (Living Room Thermostat)\n");
    printf("==========================================\n\n");

    // 测试1: 并发读取
    test_concurrent_reads();

    // 测试2: 重复读取
    test_repeated_reads();

    // 测试3: 并发写入
    test_concurrent_writes();

    // 测试4: PLC高频轮询
    test_plc_polling();

    printf("\n� 所有测试完成！\n");
    printf("💡 新缓存机制特点：\n");
    printf("   ✅ 基于对象的缓存，不依赖invoke_id\n");
    printf("   ✅ 支持PLC高频轮询场景\n");
    printf("   ✅ 激进策略：每次都发送请求，尽可能获取最新数据\n");
    printf("   ✅ 简化的API：不需要事件循环，直接同步调用\n");

    return 0;
}
