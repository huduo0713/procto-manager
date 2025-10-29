#include "impl/bacnet/src/proto_bacnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// BACnet 对象类型定义
#define OBJECT_ANALOG_INPUT      0
#define OBJECT_ANALOG_OUTPUT     1
#define OBJECT_ANALOG_VALUE      2
#define OBJECT_BINARY_INPUT      3
#define OBJECT_BINARY_OUTPUT     4
#define OBJECT_BINARY_VALUE      5
#define OBJECT_CALENDAR          6
#define OBJECT_COMMAND           7
#define OBJECT_DEVICE            8
#define OBJECT_EVENT_ENROLLMENT  9
#define OBJECT_FILE             10
#define OBJECT_GROUP            11
#define OBJECT_LOOP             12
#define OBJECT_MULTI_STATE_INPUT 13
#define OBJECT_MULTI_STATE_OUTPUT 14
#define OBJECT_NOTIFICATION_CLASS 15
#define OBJECT_PROGRAM          16
#define OBJECT_SCHEDULE         17
#define OBJECT_AVERAGING        18
#define OBJECT_MULTI_STATE_VALUE 19
#define OBJECT_TREND_LOG        20
#define OBJECT_LIFE_SAFETY_POINT 21
#define OBJECT_LIFE_SAFETY_ZONE 22
#define OBJECT_ACCUMULATOR      23
#define OBJECT_PULSE_CONVERTER  24
#define OBJECT_EVENT_LOG        25
#define OBJECT_GLOBAL_GROUP     26
#define OBJECT_TREND_LOG_MULTIPLE 27
#define OBJECT_LOAD_CONTROL     28
#define OBJECT_STRUCTURED_VIEW  29
#define OBJECT_ACCESS_DOOR      30
#define OBJECT_TIMER            31
#define OBJECT_ACCESS_CREDENTIAL 32
#define OBJECT_ACCESS_POINT     33
#define OBJECT_ACCESS_RIGHTS    34
#define OBJECT_ACCESS_USER      35
#define OBJECT_ACCESS_ZONE      36
#define OBJECT_CREDENTIAL_DATA_INPUT 37
#define OBJECT_NETWORK_SECURITY 38
#define OBJECT_BITSTRING_VALUE  39
#define OBJECT_CHARACTERSTRING_VALUE 40
#define OBJECT_DATEPATTERN_VALUE 41
#define OBJECT_DATE_VALUE       42
#define OBJECT_DATETIMEPATTERN_VALUE 43
#define OBJECT_DATETIME_VALUE   44
#define OBJECT_INTEGER_VALUE    45
#define OBJECT_LARGE_ANALOG_VALUE 46
#define OBJECT_OCTETSTRING_VALUE 47
#define OBJECT_POSITIVE_INTEGER_VALUE 48
#define OBJECT_TIMEPATTERN_VALUE 49
#define OBJECT_TIME_VALUE       50
#define OBJECT_NOTIFICATION_FORWARDER 51
#define OBJECT_ALERT_ENROLLMENT 52
#define OBJECT_CHANNEL          53
#define OBJECT_LIGHTING_OUTPUT  54
#define OBJECT_BINARY_LIGHTING_OUTPUT 55
#define OBJECT_COLOR            56
#define OBJECT_COLOR_TEMPERATURE 57
#define OBJECT_STAGING          58
#define OBJECT_FAULT            59

// 属性ID定义
#define PROP_PRESENT_VALUE      85
#define PROP_OBJECT_NAME        77
#define PROP_DESCRIPTION        28
#define PROP_UNITS              117

// 并发读写测试
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

    // 准备读请求
    bacnet_read_t read_reqs[NUM_OBJECTS];
    bacnet_data_value_t values[NUM_OBJECTS];

    printf("📡 批量提交 %d 个读请求...\n", NUM_OBJECTS);

    // 1. 批量提交读请求
    for (int i = 0; i < NUM_OBJECTS; i++) {
        read_reqs[i] = (bacnet_read_t){
            .device_instance = 5678,  // 目标设备ID
            .object_type = objects[i].object_type,
            .object_instance = objects[i].object_instance,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &values[i],
            .check_only = false
        };

        int result = plc_proto_read(&read_reqs[i]);
        if (result == PROTO_SUCCESS) {
            printf("✅ 已提交读取 %s\n", objects[i].name);
        } else if (result == -7) {  // PROTO_NO_DATA
            printf("📭 %s 队列中暂无数据，请求已提交\n", objects[i].name);
        } else {
            printf("❌ 提交 %s 失败: %d\n", objects[i].name, result);
        }
    }

    printf("\n⏳ 等待所有响应...\n");

    // 2. 轮询等待所有结果
    int completed = 0;
    time_t start_time = time(NULL);

    while (completed < NUM_OBJECTS && (time(NULL) - start_time) < 10) {  // 10秒超时
        bacnet_event_t event;

        // 非阻塞轮询
        int result = bacnet_poll_event(&event, 100);  // 100ms超时

        if (result == PROTO_SUCCESS && event.type != BACNET_EVENT_NONE) {
            if (event.type == BACNET_EVENT_READ_COMPLETE) {
                // 找到对应的请求
                for (int i = 0; i < NUM_OBJECTS; i++) {
                    if (event.request == &read_reqs[i]) {
                        if (event.status == PROTO_SUCCESS) {
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
                                case BACNET_DATA_CHARACTER_STRING:
                                    printf("'%.*s'\n",
                                           (int)values[i].value.character_string.length,
                                           values[i].value.character_string.data);
                                    break;
                                default:
                                    printf("(类型:%d)\n", values[i].type);
                                    break;
                            }
                        } else {
                            printf("❌ %s 读取失败: %d\n", objects[i].name, event.status);
                        }
                        completed++;
                        break;
                    }
                }
            }
        } else {
            // 短暂等待后继续轮询
            usleep(10000);  // 10ms
        }
    }

    if (completed < NUM_OBJECTS) {
        printf("⚠️ 超时：只收到 %d/%d 个响应\n", completed, NUM_OBJECTS);
    } else {
        printf("🎉 所有 %d 个对象读取完成！\n", NUM_OBJECTS);
    }
}

void test_repeated_reads() {
    printf("\n🔄 测试重复读取同一个对象\n");
    printf("==========================\n");

    const int NUM_READS = 5;
    bacnet_read_t read_reqs[NUM_READS];
    bacnet_data_value_t values[NUM_READS];

    printf("📡 连续提交 %d 次读取 analog-input-1...\n", NUM_READS);

    // 1. 连续提交多次读请求
    for (int i = 0; i < NUM_READS; i++) {
        read_reqs[i] = (bacnet_read_t){
            .device_instance = 5678,
            .object_type = OBJECT_ANALOG_INPUT,
            .object_instance = 1,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &values[i],
            .check_only = false
        };

        int result = plc_proto_read(&read_reqs[i]);
        printf("📤 第 %d 次请求: ", i + 1);
        if (result == PROTO_SUCCESS) {
            printf("✅ 成功\n");
        } else if (result == -7) {
            printf("📭 队列空，请求已提交\n");
        } else {
            printf("❌ 失败: %d\n", result);
        }

        // 短暂延迟，避免请求过于密集
        usleep(50000);  // 50ms
    }

    printf("\n⏳ 等待响应...\n");

    // 2. 收集所有结果
    int completed = 0;
    time_t start_time = time(NULL);

    while (completed < NUM_READS && (time(NULL) - start_time) < 15) {
        bacnet_event_t event;
        int result = bacnet_poll_event(&event, 200);

        if (result == PROTO_SUCCESS && event.type == BACNET_EVENT_READ_COMPLETE) {
            for (int i = 0; i < NUM_READS; i++) {
                if (event.request == &read_reqs[i]) {
                    if (event.status == PROTO_SUCCESS) {
                        printf("✅ 第 %d 次读取: %.2f\n", i + 1, values[i].value.real_value);
                    } else {
                        printf("❌ 第 %d 次读取失败: %d\n", i + 1, event.status);
                    }
                    completed++;
                    break;
                }
            }
        }
    }

    printf("🎯 重复读取测试完成\n");
}

void test_concurrent_writes() {
    printf("\n✏️ 测试并发写入多个对象\n");
    printf("======================\n");

    // 定义要写入的对象
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
         {.type = BACNET_DATA_BOOLEAN, .value.boolean_value = true}},
        {OBJECT_BINARY_VALUE, 1, "binary-value-1",
         {.type = BACNET_DATA_BOOLEAN, .value.boolean_value = false}},
        {OBJECT_INTEGER_VALUE, 1, "integer-value-1",
         {.type = BACNET_DATA_SIGNED, .value.signed_value = 42}}
    };

    const int NUM_WRITES = sizeof(writes) / sizeof(writes[0]);

    printf("📝 批量提交 %d 个写请求...\n", NUM_WRITES);

    // 1. 批量提交写请求
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
                case BACNET_DATA_BOOLEAN:
                    printf("%s\n", writes[i].value.value.boolean_value ? "TRUE" : "FALSE");
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

    printf("\n⏳ 等待所有写入完成...\n");

    // 2. 等待所有写入完成
    int completed = 0;
    time_t start_time = time(NULL);

    while (completed < NUM_WRITES && (time(NULL) - start_time) < 10) {
        bacnet_event_t event;
        int result = bacnet_poll_event(&event, 100);

        if (result == PROTO_SUCCESS && event.type == BACNET_EVENT_WRITE_COMPLETE) {
            // 写入事件中没有直接的请求关联信息，我们通过计数来跟踪
            printf("✅ 写入完成 (%d/%d)\n", completed + 1, NUM_WRITES);
            completed++;
        }
    }

    if (completed < NUM_WRITES) {
        printf("⚠️ 超时：只完成 %d/%d 个写入\n", completed, NUM_WRITES);
    } else {
        printf("🎉 所有 %d 个对象写入完成！\n", NUM_WRITES);
    }
}

int main() {
    printf("🚀 BACnet 并发读写测试程序\n");
    printf("目标设备: 5678 (Living Room Thermostat)\n");
    printf("==========================================\n\n");

    // 测试并发读取
    test_concurrent_reads();

    // 测试重复读取
    test_repeated_reads();

    // 测试并发写入
    test_concurrent_writes();

    printf("\n🎯 所有测试完成！\n");
    printf("💡 提示：\n");
    printf("   - 读操作支持并发提交和批量处理\n");
    printf("   - 写操作通过队列机制支持批量写入\n");
    printf("   - 使用 bacnet_poll_event() 轮询所有结果\n");
    printf("   - 每个操作都有独立的超时控制\n");

    return 0;
}