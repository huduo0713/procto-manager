#include "proto_bacnet_internal.hpp"
#include <bacnet/bactext.h>

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* I-Am 响应处理（设备发现）                                                  */
/* -------------------------------------------------------------------------- */

void handle_iam_callback(uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_warn("[BACnet] I-Am handler called but context not initialized");
        return;
    }

    uint32_t device_id = 0;
    unsigned max_apdu = 0;
    int segmentation = 0;
    uint16_t vendor_id = 0;

    int len = iam_decode_service_request(service_request, &device_id, &max_apdu, &segmentation, &vendor_id);
    if (len < 0) {
        log_warn("[BACnet] Failed to decode I-Am response");
        return;
    }

    // 检查是否是目标设备范围内的设备
    if (device_id < context.target_device_start || device_id > context.target_device_end) {
        log_debug("[BACnet] Received I-Am from device {} (not in target range {}-{})", 
                  device_id, context.target_device_start, context.target_device_end);
        return;
    }

    // 缓存设备地址
    address_add(device_id, max_apdu, src);
    context.device_address_to_id[*src] = device_id;
    context.target_found.store(true, std::memory_order_release);
    
    log_info("[BACnet] Target device {} discovered and cached (max_apdu: {}, vendor: {})", 
             device_id, max_apdu, vendor_id);
}

/* -------------------------------------------------------------------------- */
/* ReadProperty Ack 响应处理                                                  */
/* -------------------------------------------------------------------------- */

void handle_read_property_ack(uint8_t *service_request, uint16_t service_len, 
                               BACNET_ADDRESS *src, BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_error("[BACnet] ReadProperty Ack handler called but context not initialized");
        return;
    }

    uint8_t invoke_id = service_data->invoke_id;
    log_debug("[BACnet] ReadProperty Ack received (invoke_id: {})", invoke_id);

    // 解析ReadProperty ACK响应数据
    BACNET_READ_PROPERTY_DATA data{};
    int len = rp_ack_decode_service_request(service_request, service_len, &data);
    if (len < 0) {
        log_error("[BACnet] Failed to decode ReadProperty ACK response");
        return;
    }

    // 获取设备ID
    uint32_t device_id = context.device_address_to_id[*src];

    // 通过 invoke_id 查找 ObjectKey
    ObjectKey key;
    bool found_key = false;
    {
        std::lock_guard<std::mutex> lock(context.invoke_id_to_key_mutex);
        auto it = context.invoke_id_to_key.find(invoke_id);
        if (it != context.invoke_id_to_key.end()) {
            key = it->second;
            found_key = true;
            // 用完删除映射
            context.invoke_id_to_key.erase(it);
        }
    }

    if (!found_key) {
        log_warn("[BACnet] No ObjectKey mapping found for invoke_id: {}", invoke_id);
        tsm_free_invoke_id(invoke_id);
        return;
    }

    // 解码数据值
    BACNET_APPLICATION_DATA_VALUE value{};
    int decoded_len = bacapp_decode_application_data(
        data.application_data, data.application_data_len, &value);
    
    if (decoded_len <= 0) {
        log_error("[BACnet] Failed to decode application data (invoke_id: {})", invoke_id);
        tsm_free_invoke_id(invoke_id);
        return;
    }

    // 更新对象缓存
    {
        std::lock_guard<std::mutex> lock(context.object_states_mutex);
        auto it = context.object_states.find(key);
        if (it != context.object_states.end()) {
            auto &state = it->second;
            
            // 存储数据到缓存
            bacnet_read_t temp_req = {};
            temp_req.value = &state.cached_value;
            proto_status_t status = store_application_value(&temp_req, value); // 🔑 在这里填充 type!
            
            if (status == PROTO_SUCCESS) {
                state.has_valid_cache = true;
                state.status = PROTO_SUCCESS;
                state.timestamp = std::chrono::steady_clock::now();
                state.active_invoke_id = 0;  // 清除活跃请求标记
                
                log_info("[BACnet] ReadProperty successful, cache updated (device: {}, {}-{}, {}, invoke_id: {})", 
                         device_id, 
                         bactext_object_type_name(data.object_type), 
                         data.object_instance,
                         bactext_property_name(key.property_id),
                         invoke_id);
            } else {
                state.status = status;
                state.active_invoke_id = 0;
                log_error("[BACnet] Failed to store value to cache (status: {}, invoke_id: {})", 
                         status, invoke_id);
            }
        } else {
            log_warn("[BACnet] ObjectKey not found in object_states for invoke_id: {}", invoke_id);
        }
    }

    // 释放TSM资源
    tsm_free_invoke_id(invoke_id);
}

/* -------------------------------------------------------------------------- */
/* WriteProperty Simple Ack 响应处理                                          */
/* -------------------------------------------------------------------------- */

void handle_write_property_ack(BACNET_ADDRESS *src, uint8_t invoke_id)
{
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        log_error("[BACnet] WriteProperty Ack handler called but context not initialized");
        return;
    }

    log_debug("[BACnet] WriteProperty Ack received (invoke_id: {})", invoke_id);

    // 通过 invoke_id 查找对应的待确认写操作
    {
        std::lock_guard<std::mutex> lock(context.write_pending_mutex);
        
        // 使用哈希表查找：O(1) 操作
        auto it = context.write_pending_map.find(invoke_id);
        if (it != context.write_pending_map.end()) {
            auto &item = it->second;
            
            // 构建值的字符串表示
            std::string value_str;
            switch (item.value.type) {
                case BACNET_DATA_REAL:
                    value_str = std::to_string(item.value.value.real_value);
                    break;
                case BACNET_DATA_BOOLEAN:
                    value_str = item.value.value.boolean_value ? "TRUE" : "FALSE";
                    break;
                case BACNET_DATA_UNSIGNED:
                    value_str = std::to_string(item.value.value.unsigned_value);
                    break;
                case BACNET_DATA_SIGNED:
                    value_str = std::to_string(item.value.value.signed_value);
                    break;
                case BACNET_DATA_ENUM:
                    value_str = std::to_string(item.value.value.enum_value);
                    break;
                default:
                    value_str = "(type:" + std::to_string(item.value.type) + ")";
                    break;
            }
            
            // 记录详细的成功日志
            log_info("[BACnet] ✅ WriteProperty successful (device: {}, {}-{}, {}, value: {}, priority: {}, invoke_id: {})", 
                     item.device_instance, 
                     bactext_object_type_name(item.object_type), 
                     item.object_instance,
                     bactext_property_name(item.property_id),
                     value_str,
                     item.priority,
                     invoke_id);
            
            // 立即清除：因为没有 poll 接口，用户无法查询，直接删除避免内存泄漏
            context.write_pending_map.erase(it);
        } else {
            log_warn("[BACnet] No matching write pending item found for invoke_id: {}", invoke_id);
        }
    }

    // 释放TSM资源
    if (invoke_id != 0) {
        tsm_free_invoke_id(invoke_id);
    }
}

/* -------------------------------------------------------------------------- */
/* 错误响应处理                                                               */
/* -------------------------------------------------------------------------- */

void handle_error_response(BACNET_ADDRESS *src, uint8_t invoke_id, 
                           BACNET_ERROR_CLASS error_class, BACNET_ERROR_CODE error_code)
{
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        return;
    }

    log_error("[BACnet] Error response received: {} - {} (invoke_id: {})",
              bactext_error_class_name(error_class),
              bactext_error_code_name(error_code),
              invoke_id);

    // 通过 invoke_id 查找 ObjectKey（读操作）
    ObjectKey key;
    bool found_key = false;
    {
        std::lock_guard<std::mutex> lock(context.invoke_id_to_key_mutex);
        auto it = context.invoke_id_to_key.find(invoke_id);
        if (it != context.invoke_id_to_key.end()) {
            key = it->second;
            found_key = true;
            context.invoke_id_to_key.erase(it);
        }
    }

    if (found_key) {
        // 读操作错误
        std::lock_guard<std::mutex> lock(context.object_states_mutex);
        auto it = context.object_states.find(key);
        if (it != context.object_states.end()) {
            auto &state = it->second;
            
            // 区分不同类型的错误
            if (error_code == ERROR_CODE_UNKNOWN_PROPERTY || 
                error_code == ERROR_CODE_UNKNOWN_OBJECT ||
                error_code == ERROR_CODE_UNSUPPORTED_OBJECT_TYPE) {
                // 属性/对象不存在或不支持
                state.status = PROTO_ERROR_UNSUPPORTED;
            } else {
                // 其他读取错误
                state.status = PROTO_ERROR_READ;
            }
            state.active_invoke_id = 0;  // 清除活跃请求
            
            log_error("[BACnet] ReadProperty failed (device: {}, {}-{}, {}, error: {}, invoke_id: {})", 
                     key.device_instance, 
                     bactext_object_type_name(key.object_type), 
                     key.object_instance,
                     bactext_property_name(key.property_id),
                     bactext_error_code_name(error_code), 
                     invoke_id);
        }
    } else {
        // 检查写操作待确认表
        std::lock_guard<std::mutex> lock_write(context.write_pending_mutex);
        auto it = context.write_pending_map.find(invoke_id);
        if (it != context.write_pending_map.end()) {
            auto &item = it->second;
            
            // 构建值的字符串表示
            std::string value_str;
            switch (item.value.type) {
                case BACNET_DATA_REAL:
                    value_str = std::to_string(item.value.value.real_value);
                    break;
                case BACNET_DATA_BOOLEAN:
                    value_str = item.value.value.boolean_value ? "TRUE" : "FALSE";
                    break;
                case BACNET_DATA_UNSIGNED:
                    value_str = std::to_string(item.value.value.unsigned_value);
                    break;
                case BACNET_DATA_SIGNED:
                    value_str = std::to_string(item.value.value.signed_value);
                    break;
                case BACNET_DATA_ENUM:
                    value_str = std::to_string(item.value.value.enum_value);
                    break;
                default:
                    value_str = "(type:" + std::to_string(item.value.type) + ")";
                    break;
            }
            
            // 记录详细的错误日志
            log_error("[BACnet] ❌ WriteProperty failed (device: {}, {}-{}, {}, value: {}, priority: {}, error: {} - {}, invoke_id: {})", 
                     item.device_instance,
                     bactext_object_type_name(item.object_type), 
                     item.object_instance,
                     bactext_property_name(item.property_id),
                     value_str,
                     item.priority,
                     bactext_error_class_name(error_class),
                     bactext_error_code_name(error_code), 
                     invoke_id);
            
            // 立即清除：错误也要删除，避免内存泄漏
            context.write_pending_map.erase(it);
        } else {
            log_warn("[BACnet] No matching request found for error response (invoke_id: {})", invoke_id);
        }
    }

    // 释放TSM资源
    if (invoke_id != 0) {
        tsm_free_invoke_id(invoke_id);
    }
}

/* -------------------------------------------------------------------------- */
/* Abort 响应处理                                                             */
/* -------------------------------------------------------------------------- */

void handle_abort_response(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool /*server*/)
{
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        return;
    }

    log_error("[BACnet] Abort received: {} (invoke_id: {})",
              bactext_abort_reason_name(abort_reason),
              invoke_id);

    // 清理反向映射和对象状态
    {
        std::lock_guard<std::mutex> lock(context.invoke_id_to_key_mutex);
        auto it = context.invoke_id_to_key.find(invoke_id);
        if (it != context.invoke_id_to_key.end()) {
            ObjectKey key = it->second;
            context.invoke_id_to_key.erase(it);
            
            // 清除对象状态中的活跃请求标记
            std::lock_guard<std::mutex> lock_states(context.object_states_mutex);
            auto state_it = context.object_states.find(key);
            if (state_it != context.object_states.end()) {
                state_it->second.active_invoke_id = 0;
                state_it->second.status = PROTO_ERROR_READ;
            }
        }
    }

    // 释放TSM资源
    if (invoke_id != 0) {
        tsm_free_invoke_id(invoke_id);
    }
}

/* -------------------------------------------------------------------------- */
/* Reject 响应处理                                                            */
/* -------------------------------------------------------------------------- */

void handle_reject_response(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    auto& context = BacnetContext::instance();
    if (!context.is_initialized()) {
        return;
    }

    log_error("[BACnet] Reject received: {} (invoke_id: {})",
              bactext_reject_reason_name(reject_reason),
              invoke_id);

    // 清理反向映射和对象状态
    {
        std::lock_guard<std::mutex> lock(context.invoke_id_to_key_mutex);
        auto it = context.invoke_id_to_key.find(invoke_id);
        if (it != context.invoke_id_to_key.end()) {
            ObjectKey key = it->second;
            context.invoke_id_to_key.erase(it);
            
            // 清除对象状态中的活跃请求标记
            std::lock_guard<std::mutex> lock_states(context.object_states_mutex);
            auto state_it = context.object_states.find(key);
            if (state_it != context.object_states.end()) {
                state_it->second.active_invoke_id = 0;
                state_it->second.status = PROTO_ERROR_READ;
            }
        }
    }

    // 释放TSM资源
    if (invoke_id != 0) {
        tsm_free_invoke_id(invoke_id);
    }
}

/* -------------------------------------------------------------------------- */
/* 注册BACnet协议栈回调处理函数                                                */
/* -------------------------------------------------------------------------- */

void register_bacnet_handlers(BacnetContext *context)
{
    if (!context) {
        return;
    }

    // 设置服务处理器
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    
    // 设置确认应答处理器
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY, handle_read_property_ack);
    
    // 设置未确认服务处理器
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handle_iam_callback);
    
    // 设置错误处理器
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, handle_error_response);
    apdu_set_error_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handle_error_response);
    
    // 设置简单应答处理器
    apdu_set_confirmed_simple_ack_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handle_write_property_ack);
    
    // 设置异常处理器
    apdu_set_abort_handler(handle_abort_response);
    apdu_set_reject_handler(handle_reject_response);

    log_info("[BACnet] Protocol stack handlers registered");
}

} // namespace bacnet
