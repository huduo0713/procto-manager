/**
 * @file proto_bacnet_batch.cpp
 * @brief BACnet 批量读取接口实现（同步版本）
 * 
 * 功能说明：
 * - 通过循环调用 plc_proto_read() 实现同步批量读取
 * - 等待每个属性返回后再读取下一个
 * - 字符串使用动态分配，避免固定长度溢出问题
 */

#include "proto_bacnet.h"
#include "proto_bacnet_internal.hpp"
#include <bacnet/bacenum.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

using namespace bacnet;

/* -------------------------------------------------------------------------- */
/* 辅助函数：同步读取单个属性                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief 同步读取单个属性（轮询直到成功或超时）
 * 
 * @param device_instance 设备实例号
 * @param object_type 对象类型
 * @param object_instance 对象实例号
 * @param property_id 属性 ID
 * @param value 输出值缓冲区
 * @return true 成功，false 失败
 */
static bool read_property_sync(
    uint32_t device_instance,
    uint16_t object_type,
    uint32_t object_instance,
    uint32_t property_id,
    bacnet_data_value_t *value)
{
    bacnet_read_t req = BACNET_READ_INIT(
        device_instance,
        object_type,
        object_instance,
        property_id,
        value
    );
    
    // 最多重试 10 次（减少重试次数），每次间隔 100ms，总计 1 秒超时
    const int max_retries = 10;
    const int retry_interval_ms = 100;
    
    // 构造对象键，用于检查回调设置的状态
    bacnet::ObjectKey key = {device_instance, object_type, object_instance, property_id};
    
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        int result = plc_proto_read(&req);
        
        if (result == PROTO_SUCCESS) {
            return true;  // 成功
        } else if (result == PROTO_NO_DATA) {
            // 请求已发送，等待响应
            usleep(retry_interval_ms * 1000);
            
            // 检查回调函数是否设置了 UNSUPPORTED 状态
            auto& driver = bacnet::BacnetDriver::instance();
            if (driver.is_initialized()) {
                auto* ctx = driver.get_context();
                if (ctx) {
                    std::lock_guard<std::mutex> lock(ctx->object_states_mutex);
                    auto it = ctx->object_states.find(key);
                    if (it != ctx->object_states.end() && it->second.status == PROTO_ERROR_UNSUPPORTED) {
                        log_debug("[Batch] Property {} not supported (detected via callback), skipping", property_id);
                        return false;
                    }
                }
            }
        } else if (result == PROTO_ERROR_UNSUPPORTED) {
            // 属性不存在或不支持，直接返回失败（不重试）
            log_debug("[Batch] Property {} not supported, skipping", property_id);
            return false;
        } else {
            // 其他错误（连接失败、参数错误等）
            log_debug("[Batch] Property {} read failed: {}", property_id, result);
            return false;
        }
    }
    
    log_debug("[Batch] Property {} read timeout after {} retries", property_id, max_retries);
    return false;
}

/**
 * @brief 复制字符串到动态分配的内存
 */
static char* strdup_safe(const char* src, size_t len) {
    if (!src || len == 0) {
        return nullptr;
    }
    char* dst = (char*)malloc(len + 1);
    if (dst) {
        memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return dst;
}

/* -------------------------------------------------------------------------- */
/* 批量读取实现                                                               */
/* -------------------------------------------------------------------------- */

extern "C" {

int plc_proto_read_object_properties(
    uint32_t device_instance,
    uint16_t object_type,
    uint32_t object_instance,
    bacnet_object_properties_t *properties)
{
    if (!properties) {
        return PROTO_ERROR_PARAM;
    }
    
    // ========================================================================
    // 初始化所有字段为无效值
    // ========================================================================
    properties->object_identifier = 0;                // 稍后从参数填充
    properties->object_type = object_type;            // 从参数填充
    properties->object_name = nullptr;                // NULL = 无效
    properties->present_value = -1.0;                 // -1.0 = 无效
    properties->status_flags = 0xFFFFFFFF;            // 0xFFFFFFFF = 无效
    properties->out_of_service = 0xFF;                // 0xFF = 无效
    properties->event_state = 0xFFFFFFFF;             // 0xFFFFFFFF = 无效
    properties->description = nullptr;                // NULL = 无效
    properties->units = -1;                           // -1 = 无效
    properties->relinquish_default = -1.0;            // -1.0 = 无效
    properties->cov_increment = -1.0f;                // -1.0f = 无效
    
    int success_count = 0;
    bacnet_data_value_t temp_value;
    
    log_info("[Batch] Synchronously reading properties for {}-{}-{}", 
             device_instance, bactext_object_type_name(object_type), object_instance);
    
    /* ========================================================================== */
    /* 同步循环读取每个属性                                                        */
    /* ========================================================================== */
    
    // 1. Object_Identifier - 直接从参数填充（不需要读取）
    // BACnet Object ID 由 (object_type << 22) | object_instance 组成
    properties->object_identifier = ((uint32_t)object_type << 22) | object_instance;
    success_count++;
    log_debug("[Batch] Object_Identifier: {} (filled from params)", properties->object_identifier);
    
    // 2. Object_Name (字符串 - 动态分配)
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_OBJECT_NAME, &temp_value)) {
        if (temp_value.type == BACNET_DATA_CHARACTER_STRING) {
            properties->object_name = strdup_safe(
                temp_value.value.character_string.data,
                temp_value.value.character_string.length
            );
            success_count++;
            log_debug("[Batch] Object_Name: {}", properties->object_name);
        }
    }
    
    // 3. Present_Value
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_PRESENT_VALUE, &temp_value)) {
        if (temp_value.type == BACNET_DATA_REAL) {
            properties->present_value = temp_value.value.real_value;
        } else if (temp_value.type == BACNET_DATA_DOUBLE) {
            properties->present_value = temp_value.value.double_value;
        } else if (temp_value.type == BACNET_DATA_UNSIGNED) {
            properties->present_value = (double)temp_value.value.unsigned_value;
        } else if (temp_value.type == BACNET_DATA_UNSIGNED) {
            properties->present_value = (double)temp_value.value.signed_value;
        }
        success_count++;
        log_debug("[Batch] Present_Value: {}", properties->present_value);
    }
    
    // 4. Status_Flags (位字段，现在作为 UNSIGNED 类型返回）
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_STATUS_FLAGS, &temp_value)) {
        if (temp_value.type == BACNET_DATA_UNSIGNED) {
            properties->status_flags = temp_value.value.unsigned_value & 0x0F;
        }
        success_count++;
        log_debug("[Batch] Status_Flags: {}", properties->status_flags);
    }
    
    // 5. Out_Of_Service
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_OUT_OF_SERVICE, &temp_value)) {
        properties->out_of_service = temp_value.value.boolean_value ? 1 : 0;
        success_count++;
        log_debug("[Batch] Out_Of_Service: {}", properties->out_of_service);
    }
    
    // 6. Event_State
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_EVENT_STATE, &temp_value)) {
        properties->event_state = (uint32_t)temp_value.value.enum_value;
        success_count++;
        log_debug("[Batch] Event_State: {}", properties->event_state);
    }
    
    // 7. Description (字符串 - 动态分配)
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_DESCRIPTION, &temp_value)) {
        if (temp_value.type == BACNET_DATA_CHARACTER_STRING && 
            temp_value.value.character_string.data != nullptr) {
            properties->description = strdup_safe(
                temp_value.value.character_string.data,
                temp_value.value.character_string.length
            );
            success_count++;
            log_debug("[Batch] Description: {}", properties->description ? properties->description : "(empty)");
        }
    }
    
    // 8. Units (可选)
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_UNITS, &temp_value)) {
        properties->units = (int32_t)temp_value.value.enum_value;
        log_debug("[Batch] Units: {}", properties->units);
    }
    
    // 9. Relinquish_Default (可选)
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_RELINQUISH_DEFAULT, &temp_value)) {
        if (temp_value.type == BACNET_DATA_REAL) {
            properties->relinquish_default = temp_value.value.real_value;
        } else if (temp_value.type == BACNET_DATA_DOUBLE) {
            properties->relinquish_default = temp_value.value.double_value;
        }
        log_debug("[Batch] Relinquish_Default: {}", properties->relinquish_default);
    }
    
    // 10. COV_Increment (可选)
    if (read_property_sync(device_instance, object_type, object_instance, 
                           PROP_COV_INCREMENT, &temp_value)) {
        properties->cov_increment = temp_value.value.real_value;
        log_debug("[Batch] COV_Increment: {}", properties->cov_increment);
    }
    
    /* ========================================================================== */
    /* 返回结果                                                                   */
    /* ========================================================================== */
    
    log_info("[Batch] Read completed: {}/7 required properties", success_count);
    
    return (success_count > 0) ? PROTO_SUCCESS : PROTO_TIMEOUT;
}

void plc_proto_free_object_properties(bacnet_object_properties_t *properties)
{
    if (!properties) {
        return;
    }
    
    // 释放动态分配的字符串
    if (properties->object_name) {
        free(properties->object_name);
        properties->object_name = nullptr;
    }
    
    if (properties->description) {
        free(properties->description);
        properties->description = nullptr;
    }
    
    log_debug("[Batch] Freed object properties memory");
}

} // extern "C"
