/**
 * @file batch_read_test.cc
 * @brief 批量读取对象属性测试程序（简化版）
 */

#include "impl/bacnet/src/proto_bacnet.h"
#include "common/utils/one_logger.hpp"
#include <bacnet/bactext.h>
#include <stdio.h>
#include <unistd.h>

#define device_id 5678

/**
 * @brief 打印对象属性详情
 */
void print_object_properties(const bacnet_object_properties_t *props) {
    log_info("========================================");
    log_info("对象属性详情:");
    log_info("========================================");
    
    /* ==================== 对象标识 ==================== */
    log_info("【对象标识】");
    if (props->object_identifier != 0) {
        log_info("  Object_Identifier: {} (0x{:08X})", 
                 props->object_identifier,
                 props->object_identifier);
        log_info("  Object_Type: {} ({})", 
                 props->object_type,
                 bactext_object_type_name(props->object_type));
    }
    
    if (props->object_name != nullptr) {
        log_info("  Object_Name: '{}'", props->object_name);
    } else {
        log_info("  Object_Name: <无效>");
    }
    
    log_info("");
    
    /* ==================== 状态信息 ==================== */
    log_info("【状态信息】");
    if (props->present_value != -1.0) {
        log_info("  Present_Value: {:.2f}", props->present_value);
    } else {
        log_info("  Present_Value: <无效>");
    }
    
    if (props->status_flags != 0xFFFFFFFF) {
        log_info("  Status_Flags: 0x{:02X}", props->status_flags);
        log_info("    - In_Alarm: {}", (props->status_flags & 0x01) ? "YES" : "NO");
        log_info("    - Fault: {}", (props->status_flags & 0x02) ? "YES" : "NO");
        log_info("    - Overridden: {}", (props->status_flags & 0x04) ? "YES" : "NO");
        log_info("    - Out_Of_Service: {}", (props->status_flags & 0x08) ? "YES" : "NO");
    } else {
        log_info("  Status_Flags: <无效>");
    }
    
    if (props->out_of_service != 0xFF) {
        log_info("  Out_Of_Service: {}", props->out_of_service ? "TRUE" : "FALSE");
    } else {
        log_info("  Out_Of_Service: <无效>");
    }
    
    if (props->event_state != 0xFFFFFFFF) {
        const char *event_state_names[] = {
            "NORMAL", "FAULT", "OFFNORMAL", "HIGH_LIMIT", "LOW_LIMIT", "LIFE_SAFETY_ALARM"
        };
        const char *state_name = (props->event_state < 6)
                                  ? event_state_names[props->event_state]
                                  : "UNKNOWN";
        log_info("  Event_State: {} ({})", props->event_state, state_name);
    } else {
        log_info("  Event_State: <无效>");
    }
    
    log_info("");
    
    /* ==================== 元数据 ==================== */
    log_info("【元数据】");
    if (props->description != nullptr) {
        log_info("  Description: '{}'", props->description);
    } else {
        log_info("  Description: <无效>");
    }
    
    if (props->units != -1) {
        log_info("  Units: {} ({})", props->units,
                 bactext_engineering_unit_name(props->units));
    } else {
        log_info("  Units: <无效或不适用>");
    }
    
    log_info("");
    
    /* ==================== 控制参数 ==================== */
    log_info("【控制参数】");
    if (props->relinquish_default != -1.0) {
        log_info("  Relinquish_Default: {:.2f}", props->relinquish_default);
    } else {
        log_info("  Relinquish_Default: <无效>");
    }
    
    if (props->cov_increment != -1.0f) {
        log_info("  COV_Increment: {:.2f}", props->cov_increment);
    } else {
        log_info("  COV_Increment: <无效>");
    }
    
    log_info("========================================");
}

/**
 * @brief 测试读取单个对象的所有属性
 */
void test_read_single_object(uint32_t device_instance, uint16_t object_type, uint32_t object_instance) {
    log_info("======================================== ");
    log_info("测试 1: 读取单个对象的所有属性");
    log_info("  设备实例: {}", device_instance);
    log_info("  对象类型: {} ({})", 
             object_type, 
             bactext_object_type_name(object_type));
    log_info("  对象实例: {}", object_instance);
    log_info("======================================== ");
    
    bacnet_object_properties_t props;
    
    int ret = plc_proto_read_object_properties(
        device_instance,
        object_type,
        object_instance,
        &props
    );
    
    if (ret == PROTO_SUCCESS) {
        log_info("✓ 批量读取成功");
        print_object_properties(&props);
        
        // 释放内存
        plc_proto_free_object_properties(&props);
    } else {
        log_error("✗ 批量读取失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
    }
    
    log_info("");
}

/**
 * @brief 测试批量读取多个对象
 */
void test_read_multiple_objects() {
    log_info("======================================== ");
    log_info("测试 2: 批量读取多个对象");
    log_info("======================================== ");
    
    // 示例：读取多个 Analog Input 对象
    struct {
        uint32_t device_instance;
        uint16_t object_type;
        uint32_t object_instance;
    } objects[] = {
        {device_id, OBJECT_ANALOG_INPUT, 1},
        {device_id, OBJECT_ANALOG_OUTPUT, 1},
        {device_id, OBJECT_ANALOG_VALUE, 1},
    };
    
    const int num_objects = sizeof(objects) / sizeof(objects[0]);
    
    for (int i = 0; i < num_objects; ++i) {
        log_info("读取对象 {}/{}: Device={}, Type={}, Instance={}", 
                 i + 1, num_objects,
                 objects[i].device_instance,
                 objects[i].object_type,
                 objects[i].object_instance);
        
        bacnet_object_properties_t props;
        
        int ret = plc_proto_read_object_properties(
            objects[i].device_instance,
            objects[i].object_type,
            objects[i].object_instance,
            &props
        );
        
        if (ret == PROTO_SUCCESS) {
            log_info("✓ 成功");
            if (props.object_name != nullptr) {
                log_info("  名称: {}", props.object_name);
            }
            if (props.present_value != -1.0) {
                log_info("  当前值: {:.2f}", props.present_value);
            }
            if (props.units != -1) {
                log_info("  单位: {}", bactext_engineering_unit_name(props.units));
            }
            
            // 释放内存
            plc_proto_free_object_properties(&props);
        } else {
            log_error("✗ 失败: {}", proto_status_to_string(static_cast<proto_status_t>(ret)));
        }
        
        log_info("");
    }
}

/**
 * @brief 主函数
 */
int main(int argc, char *argv[]) {
    log_info("======================================== ");
    log_info("BACnet 批量读取属性测试程序");
    log_info("======================================== ");
    
    // 测试 1: 详细读取单个对象
    test_read_single_object(device_id, OBJECT_ANALOG_INPUT, 1);
    
    // 等待一会儿
    sleep(2);
    
    // 测试 2: 快速读取多个对象
    test_read_multiple_objects();
    
    log_info("所有测试完成！");
    
    return 0;
}
