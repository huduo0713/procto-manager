#include "impl/bacnet/src/proto_bacnet.h"
#include <bacnet/bacenum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// 注意：所有 BACnet 宏定义已在 bacnet 库头文件中定义
// 这里直接使用，不需要重复定义

// 并发读写测试
void test_concurrent_reads() {
    printf("🔄 测试并发读取多个对象的值\n");
    printf("================================\n");

    // 定义要读取的对象列表
    struct {
        uint16_t object_type;
        uint32_t object_instance;
        const char* name;
        uint8_t invoke_id;
    } objects[] = {
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        // {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        {OBJECT_ANALOG_OUTPUT, 1, "analog-output-1", 0},
        {OBJECT_ANALOG_VALUE, 1, "analog-value-1", 0},
        {OBJECT_BINARY_INPUT, 1, "binary-input-1", 0},
        {OBJECT_BINARY_OUTPUT, 1, "binary-output-1", 0},
        {OBJECT_BINARY_VALUE, 1, "binary-value-1", 0},
        {OBJECT_INTEGER_VALUE, 1, "integer-value-1", 0},
        {OBJECT_CHARACTERSTRING_VALUE, 1, "characterstring-value-1", 0}
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
            .check_only = false,
            .invoke_id = 0
        };

        int result = plc_proto_read(&read_reqs[i]);
        if (result == PROTO_SUCCESS) {
            objects[i].invoke_id = read_reqs[i].invoke_id;  // 保存invoke_id
            printf("✅ 已提交读取 %s (invoke_id: %d)\n", objects[i].name, objects[i].invoke_id);
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
                // 通过invoke_id匹配原始请求
                for (int i = 0; i < NUM_OBJECTS; i++) {
                    if (objects[i].invoke_id == event.invoke_id) {
                        if (event.status == PROTO_SUCCESS) {
                            printf("✅ %s = ", objects[i].name);
                            // 使用事件中的数据，而不是values数组
                            switch (event.data.read_complete.value.type) {
                                case BACNET_DATA_REAL:
                                    printf("%.2f\n", event.data.read_complete.value.value.real_value);
                                    break;
                                case BACNET_DATA_BOOLEAN:
                                    printf("%s\n", event.data.read_complete.value.value.boolean_value ? "TRUE" : "FALSE");
                                    break;
                                case BACNET_DATA_UNSIGNED:
                                    printf("%u\n", event.data.read_complete.value.value.unsigned_value);
                                    break;
                                case BACNET_DATA_SIGNED:
                                    printf("%d\n", event.data.read_complete.value.value.signed_value);
                                    break;
                                case BACNET_DATA_ENUM:
                                    printf("%u\n", event.data.read_complete.value.value.enum_value);
                                    break;
                                case BACNET_DATA_OCTET_STRING:
                                    printf("octet string\n");
                                    break;
                                case BACNET_DATA_CHARACTER_STRING:
                                    printf("'%.*s'\n",
                                           (int)event.data.read_complete.value.value.character_string.length,
                                           event.data.read_complete.value.value.character_string.data);
                                    break;
                                default:
                                    printf("(类型:%d)\n", event.data.read_complete.value.type);
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

    // 清理任何残留事件
    printf("🧹 清理残留事件...\n");
    int cleaned = 0;
    while (true) {
        bacnet_event_t event;
        int result = bacnet_poll_event(&event, 0);
        if (result != PROTO_SUCCESS) {
            break;
        }
        printf("🗑️ 清理残留事件: type=%d, invoke_id=%d\n", event.type, event.invoke_id);
        cleaned++;
    }
    if (cleaned > 0) {
        printf("✅ 清理了 %d 个残留事件\n", cleaned);
    } else {
        printf("✅ 没有残留事件\n");
    }

    // 定义要读取的对象列表（5个相同的对象）
    struct {
        uint16_t object_type;
        uint32_t object_instance;
        const char* name;
        uint8_t invoke_id;
    } objects[] = {
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0},
        {OBJECT_ANALOG_INPUT, 1, "analog-input-1", 0}
    };

    const int NUM_OBJECTS = sizeof(objects) / sizeof(objects[0]);

    // 准备读请求
    bacnet_read_t read_reqs[NUM_OBJECTS];
    bacnet_data_value_t values[NUM_OBJECTS];

    printf("📡 批量提交 %d 次读取 analog-input-1...\n", NUM_OBJECTS);

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
            .check_only = false,
            .invoke_id = 0
        };

        int result = plc_proto_read(&read_reqs[i]);
        if (result == PROTO_SUCCESS) {
            objects[i].invoke_id = read_reqs[i].invoke_id;  // 保存invoke_id
            printf("✅ 已提交读取 %s (invoke_id: %d)\n", objects[i].name, objects[i].invoke_id);
        } else if (result == -7) {  // PROTO_NO_DATA
            printf("📭 %s 队列中暂无数据，请求已提交\n", objects[i].name);
        } else {
            printf("❌ 提交 %s 失败: %d\n", objects[i].name, result);
        }
    }

    printf("\n⏳ 等待所有响应...\n");

    // 2. 轮询等待所有结果（类似test_concurrent_reads的逻辑）
    int completed = 0;
    time_t start_time = time(NULL);

    while (completed < NUM_OBJECTS && (time(NULL) - start_time) < 10) {  // 10秒超时
        bacnet_event_t event;

        // 非阻塞轮询
        int result = bacnet_poll_event(&event, 100);  // 100ms超时

        if (result == PROTO_SUCCESS && event.type != BACNET_EVENT_NONE) {
            if (event.type == BACNET_EVENT_READ_COMPLETE) {
                // 通过invoke_id匹配原始请求
                for (int i = 0; i < NUM_OBJECTS; i++) {
                    if (objects[i].invoke_id == event.invoke_id) {
                        if (event.status == PROTO_SUCCESS) {
                            printf("✅ %s = ", objects[i].name);
                            // 使用事件中的数据，而不是values数组
                            switch (event.data.read_complete.value.type) {
                                case BACNET_DATA_REAL:
                                    printf("%.2f\n", event.data.read_complete.value.value.real_value);
                                    break;
                                case BACNET_DATA_BOOLEAN:
                                    printf("%s\n", event.data.read_complete.value.value.boolean_value ? "TRUE" : "FALSE");
                                    break;
                                case BACNET_DATA_UNSIGNED:
                                    printf("%u\n", event.data.read_complete.value.value.unsigned_value);
                                    break;
                                case BACNET_DATA_SIGNED:
                                    printf("%d\n", event.data.read_complete.value.value.signed_value);
                                    break;
                                case BACNET_DATA_ENUM:
                                    printf("%u\n", event.data.read_complete.value.value.enum_value);
                                    break;
                                case BACNET_DATA_OCTET_STRING:
                                    printf("octet string\n");
                                    break;
                                case BACNET_DATA_CHARACTER_STRING:
                                    printf("'%.*s'\n",
                                           (int)event.data.read_complete.value.value.character_string.length,
                                           event.data.read_complete.value.value.character_string.data);
                                    break;
                                default:
                                    printf("(类型:%d)\n", event.data.read_complete.value.type);
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
        printf("🎉 所有 %d 次重复读取完成！\n", NUM_OBJECTS);
    }
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
        uint8_t invoke_id;
    } writes[] = {
        {OBJECT_ANALOG_OUTPUT, 1, "analog-output-1",
         {.type = BACNET_DATA_REAL, .value.real_value = 25.5f}, 0},
        {OBJECT_ANALOG_VALUE, 1, "analog-value-1",
         {.type = BACNET_DATA_REAL, .value.real_value = 75.0f}, 0},
        {OBJECT_BINARY_OUTPUT, 1, "binary-output-1",
         {.type = BACNET_DATA_ENUM, .value.enum_value = 1}, 0},
        {OBJECT_BINARY_VALUE, 1, "binary-value-1",
         {.type = BACNET_DATA_ENUM, .value.enum_value = 0}, 0},
        {OBJECT_INTEGER_VALUE, 1, "integer-value-1",
         {.type = BACNET_DATA_SIGNED, .value.signed_value = 42}, 0}
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
            .value = writes[i].value,
            .invoke_id = 0
        };

        int result = plc_proto_write(&write_req);
        if (result == PROTO_SUCCESS) {
            writes[i].invoke_id = write_req.invoke_id;  // 保存invoke_id
            printf("✅ 已提交写入 %s = ", writes[i].name);
            switch (writes[i].value.type) {
                case BACNET_DATA_REAL:
                    printf("%.2f (invoke_id: %d)\n", writes[i].value.value.real_value, writes[i].invoke_id);
                    break;
                case BACNET_DATA_BOOLEAN:
                    printf("%s (invoke_id: %d)\n", writes[i].value.value.boolean_value ? "TRUE" : "FALSE", writes[i].invoke_id);
                    break;
                case BACNET_DATA_ENUM:
                    printf("%u (invoke_id: %d)\n", writes[i].value.value.enum_value, writes[i].invoke_id);
                    break;
                case BACNET_DATA_SIGNED:
                    printf("%d (invoke_id: %d)\n", writes[i].value.value.signed_value, writes[i].invoke_id);
                    break;
                default:
                    printf("(类型:%d, invoke_id: %d)\n", writes[i].value.type, writes[i].invoke_id);
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
            // 通过invoke_id匹配写入请求
            for (int i = 0; i < NUM_WRITES; i++) {
                if (writes[i].invoke_id == event.invoke_id) {
                    printf("✅ 写入完成: %s (%d/%d)\n", writes[i].name, completed + 1, NUM_WRITES);
                    completed++;
                    break;
                }
            }
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

    printf("\n🔁 测试长期运行 - 循环读取 analog-input-1\n");
    printf("==========================================\n");
    printf("💡 模拟真实生产环境，按 Ctrl+C 停止\n\n");

    int loop_count = 0;
    while (true) {
        loop_count++;
        printf("[循环 #%d] 📡 读取 analog-input-1...\n", loop_count);

        bacnet_data_value_t value;
        bacnet_read_t read_req = {
            .device_instance = 5678,
            .object_type = OBJECT_ANALOG_INPUT,
            .object_instance = 1,
            .property_id = PROP_PRESENT_VALUE,
            .array_index = -1,
            .timeout_ms = 5000,
            .value = &value,
            .check_only = false,
            .invoke_id = 0
        };

        int result = plc_proto_read(&read_req);
        if (result == PROTO_SUCCESS) {
            uint8_t invoke_id = read_req.invoke_id;
            printf("  ✅ 请求已提交 (invoke_id: %d)\n", invoke_id);

            // 等待响应
            time_t start_time = time(NULL);
            bool got_response = false;

            while (!got_response && (time(NULL) - start_time) < 5) {
                bacnet_event_t event;
                int poll_result = bacnet_poll_event(&event, 100);

                if (poll_result == PROTO_SUCCESS && event.type == BACNET_EVENT_READ_COMPLETE) {
                    if (event.invoke_id == invoke_id) {
                        if (event.status == PROTO_SUCCESS) {
                            printf("  ✅ 读取成功: %.2f\n", 
                                   event.data.read_complete.value.value.real_value);
                        } else {
                            printf("  ❌ 读取失败: status=%d\n", event.status);
                        }
                        got_response = true;
                    }
                }
            }

            if (!got_response) {
                printf("  ⚠️ 超时，未收到响应\n");
            }
        } else {
            printf("  ❌ 请求提交失败: %d\n", result);
        }

        // 延时 2 秒后继续下一次循环
        printf("  ⏱️ 等待 2 秒...\n\n");
        sleep(2);
    }

    printf("\n🎯 所有测试完成！\n");
    printf("💡 提示：\n");
    printf("   - 读操作支持并发提交和批量处理\n");
    printf("   - 写操作通过队列机制支持批量写入\n");
    printf("   - 使用 bacnet_poll_event() 轮询所有结果\n");
    printf("   - 每个操作都有独立的超时控制\n");

    return 0;
}