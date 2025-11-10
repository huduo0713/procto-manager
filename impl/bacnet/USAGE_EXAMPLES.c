/* ========================================================================== */
/* BACnet 协议驱动 - 使用示例                                                 */
/* ========================================================================== */

#include "impl/bacnet/src/proto_bacnet.h"
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/* 示例 1: 读取模拟输入值（最简单的方式）                                     */
/* -------------------------------------------------------------------------- */

void example_simple_read() {
    printf("=== 示例 1: 简单读取 ===\n");
    
    // 准备读请求
    bacnet_read_t read_req = {};
    read_req.device_instance = 100;                    // 目标设备实例号
    read_req.object_type = OBJECT_ANALOG_INPUT;        // 模拟输入对象
    read_req.object_instance = 0;                      // 对象实例号
    read_req.property_id = PROP_PRESENT_VALUE;         // 当前值属性
    read_req.array_index = -1;                         // 不是数组
    read_req.timeout_ms = 5000;                        // 5秒超时
    
    // 准备接收缓冲区
    bacnet_data_value_t value = {};
    value.type = BACNET_DATA_REAL;
    read_req.value = &value;
    
    // 调用 PLC 读接口（阻塞式，自动处理连接）
    int ret = plc_proto_read(&read_req);
    
    if (ret == PROTO_SUCCESS) {
        printf("✓ 读取成功: %.2f\n", value.value.real_value);
    } else {
        printf("✗ 读取失败: %s\n", proto_status_to_string((proto_status_t)ret));
    }
}

/* -------------------------------------------------------------------------- */
/* 示例 2: 写入模拟输出值                                                     */
/* -------------------------------------------------------------------------- */

void example_simple_write() {
    printf("\n=== 示例 2: 简单写入 ===\n");
    
    // 准备写请求
    bacnet_write_t write_req = {};
    write_req.device_instance = 100;
    write_req.object_type = OBJECT_ANALOG_OUTPUT;
    write_req.object_instance = 0;
    write_req.property_id = PROP_PRESENT_VALUE;
    write_req.array_index = -1;
    write_req.priority = 16;                           // 优先级 16
    write_req.timeout_ms = 5000;
    
    // 设置写入值
    write_req.value.type = BACNET_DATA_REAL;
    write_req.value.value.real_value = 25.5f;          // 写入 25.5
    
    // 调用 PLC 写接口（阻塞式）
    int ret = plc_proto_write(&write_req);
    
    if (ret == PROTO_SUCCESS) {
        printf("✓ 写入成功: 25.5\n");
    } else {
        printf("✗ 写入失败: %s\n", proto_status_to_string((proto_status_t)ret));
    }
}

/* -------------------------------------------------------------------------- */
/* 示例 3: 读取多个属性                                                       */
/* -------------------------------------------------------------------------- */

void example_read_multiple_properties() {
    printf("\n=== 示例 3: 读取多个属性 ===\n");
    
    // 定义要读取的属性列表
    struct {
        uint32_t property_id;
        const char *name;
    } properties[] = {
        {PROP_PRESENT_VALUE, "当前值"},
        {PROP_OUT_OF_SERVICE, "离线状态"},
        {PROP_UNITS, "单位"},
        {PROP_DESCRIPTION, "描述"},
    };
    
    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
        bacnet_read_t read_req = {};
        read_req.device_instance = 100;
        read_req.object_type = OBJECT_ANALOG_INPUT;
        read_req.object_instance = 0;
        read_req.property_id = properties[i].property_id;
        read_req.array_index = -1;
        read_req.timeout_ms = 5000;
        
        bacnet_data_value_t value = {};
        read_req.value = &value;
        
        int ret = plc_proto_read(&read_req);
        
        printf("[%s] ", properties[i].name);
        if (ret == PROTO_SUCCESS) {
            switch (value.type) {
                case BACNET_DATA_REAL:
                    printf("%.2f\n", value.value.real_value);
                    break;
                case BACNET_DATA_BOOLEAN:
                    printf("%s\n", value.value.boolean_value ? "true" : "false");
                    break;
                case BACNET_DATA_UNSIGNED:
                    printf("%u\n", value.value.unsigned_value);
                    break;
                case BACNET_DATA_CHARACTER_STRING:
                    printf("%.*s\n", (int)value.value.character_string.length,
                           value.value.character_string.data);
                    break;
                default:
                    printf("(未知类型)\n");
                    break;
            }
        } else {
            printf("✗ 失败\n");
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 示例 4: 循环读取（监控）                                                   */
/* -------------------------------------------------------------------------- */

void example_continuous_monitoring() {
    printf("\n=== 示例 4: 连续监控（10次） ===\n");
    
    for (int i = 0; i < 10; i++) {
        bacnet_read_t read_req = {};
        read_req.device_instance = 100;
        read_req.object_type = OBJECT_ANALOG_INPUT;
        read_req.object_instance = 0;
        read_req.property_id = PROP_PRESENT_VALUE;
        read_req.array_index = -1;
        read_req.timeout_ms = 5000;
        
        bacnet_data_value_t value = {};
        value.type = BACNET_DATA_REAL;
        read_req.value = &value;
        
        int ret = plc_proto_read(&read_req);
        
        printf("[%d] ", i + 1);
        if (ret == PROTO_SUCCESS) {
            printf("✓ %.2f\n", value.value.real_value);
        } else {
            printf("✗ %s\n", proto_status_to_string((proto_status_t)ret));
        }
        
        // 等待 1 秒
        sleep(1);
    }
}

/* -------------------------------------------------------------------------- */
/* 示例 5: 写入不同数据类型                                                   */
/* -------------------------------------------------------------------------- */

void example_write_different_types() {
    printf("\n=== 示例 5: 写入不同类型 ===\n");
    
    // 写入浮点数
    {
        bacnet_write_t write_req = {};
        write_req.device_instance = 100;
        write_req.object_type = OBJECT_ANALOG_VALUE;
        write_req.object_instance = 0;
        write_req.property_id = PROP_PRESENT_VALUE;
        write_req.array_index = -1;
        write_req.priority = 16;
        write_req.timeout_ms = 5000;
        
        write_req.value.type = BACNET_DATA_REAL;
        write_req.value.value.real_value = 36.5f;
        
        int ret = plc_proto_write(&write_req);
        printf("写入浮点数 36.5: %s\n", ret == PROTO_SUCCESS ? "✓ 成功" : "✗ 失败");
    }
    
    // 写入布尔值
    {
        bacnet_write_t write_req = {};
        write_req.device_instance = 100;
        write_req.object_type = OBJECT_BINARY_VALUE;
        write_req.object_instance = 0;
        write_req.property_id = PROP_PRESENT_VALUE;
        write_req.array_index = -1;
        write_req.priority = 16;
        write_req.timeout_ms = 5000;
        
        write_req.value.type = BACNET_DATA_BOOLEAN;
        write_req.value.value.boolean_value = true;
        
        int ret = plc_proto_write(&write_req);
        printf("写入布尔值 true: %s\n", ret == PROTO_SUCCESS ? "✓ 成功" : "✗ 失败");
    }
    
    // 写入无符号整数
    {
        bacnet_write_t write_req = {};
        write_req.device_instance = 100;
        write_req.object_type = OBJECT_ANALOG_VALUE;
        write_req.object_instance = 1;
        write_req.property_id = PROP_PRESENT_VALUE;
        write_req.array_index = -1;
        write_req.priority = 16;
        write_req.timeout_ms = 5000;
        
        write_req.value.type = BACNET_DATA_UNSIGNED;
        write_req.value.value.unsigned_value = 100;
        
        int ret = plc_proto_write(&write_req);
        printf("写入无符号整数 100: %s\n", ret == PROTO_SUCCESS ? "✓ 成功" : "✗ 失败");
    }
}

/* -------------------------------------------------------------------------- */
/* 示例 6: 错误处理                                                           */
/* -------------------------------------------------------------------------- */

void example_error_handling() {
    printf("\n=== 示例 6: 错误处理 ===\n");
    
    bacnet_read_t read_req = {};
    read_req.device_instance = 9999;                   // 不存在的设备
    read_req.object_type = OBJECT_ANALOG_INPUT;
    read_req.object_instance = 0;
    read_req.property_id = PROP_PRESENT_VALUE;
    read_req.array_index = -1;
    read_req.timeout_ms = 5000;
    
    bacnet_data_value_t value = {};
    read_req.value = &value;
    
    int ret = plc_proto_read(&read_req);
    
    printf("尝试读取不存在的设备: ");
    switch (ret) {
        case PROTO_SUCCESS:
            printf("✓ 成功\n");
            break;
        case PROTO_ERROR_CONNECT:
            printf("✗ 连接失败（设备不存在或网络问题）\n");
            break;
        case PROTO_ERROR_READ:
            printf("✗ 读取失败（超时或协议错误）\n");
            break;
        case PROTO_ERROR_PARAM:
            printf("✗ 参数错误\n");
            break;
        default:
            printf("✗ 未知错误 (%d)\n", ret);
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* 主函数                                                                     */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("BACnet 协议驱动 - 使用示例\n");
    printf("========================================\n\n");
    
    // 注意：所有示例都使用 plc_proto_read/write 接口
    // 这些接口会自动处理：
    // 1. 驱动初始化
    // 2. 设备连接
    // 3. 热配置
    // 4. 资源管理
    
    // 运行示例（根据需要选择）
    if (argc > 1) {
        int example = atoi(argv[1]);
        switch (example) {
            case 1: example_simple_read(); break;
            case 2: example_simple_write(); break;
            case 3: example_read_multiple_properties(); break;
            case 4: example_continuous_monitoring(); break;
            case 5: example_write_different_types(); break;
            case 6: example_error_handling(); break;
            default:
                printf("用法: %s [1-6]\n", argv[0]);
                printf("  1 - 简单读取\n");
                printf("  2 - 简单写入\n");
                printf("  3 - 读取多个属性\n");
                printf("  4 - 连续监控\n");
                printf("  5 - 写入不同类型\n");
                printf("  6 - 错误处理\n");
                break;
        }
    } else {
        // 默认运行所有示例
        example_simple_read();
        example_simple_write();
        example_read_multiple_properties();
        // example_continuous_monitoring();  // 太耗时，默认不运行
        example_write_different_types();
        example_error_handling();
    }
    
    printf("\n========================================\n");
    printf("示例运行完成\n");
    printf("========================================\n");
    
    return 0;
}

/* -------------------------------------------------------------------------- */
/* 关键要点                                                                   */
/* -------------------------------------------------------------------------- */

/*
 * 1. 对外只使用 plc_proto_read() 和 plc_proto_write()
 *    - 这两个接口是 C 接口，可以被 C/C++ 代码调用
 *    - 自动处理连接、初始化、热配置等
 *    - 阻塞等待结果返回
 * 
 * 2. 内部全部使用 C++ 实现
 *    - 现代 C++ 特性（智能指针、原子变量、RAII）
 *    - 线程安全（互斥锁 + 条件变量）
 *    - 事件驱动（异步操作 + 事件队列）
 * 
 * 3. 热配置通过配置文件 + 信号机制
 *    - 修改 config.yaml 会触发重载
 *    - 自动重新初始化驱动
 *    - 无需手动干预
 * 
 * 4. 错误处理
 *    - 检查返回值
 *    - PROTO_SUCCESS 表示成功
 *    - 其他值表示错误类型
 * 
 * 5. 超时设置
 *    - 建议设置合理的超时值（5-10秒）
 *    - 避免无限等待
 *    - 超时后返回错误码
 */
