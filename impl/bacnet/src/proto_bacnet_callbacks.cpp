#include "proto_bacnet_internal.hpp"

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* I-Am 响应处理（设备发现）                                                  */
/* -------------------------------------------------------------------------- */

void handle_iam_callback(uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    BacnetContext *context = g_ctx;
    if (!context) {
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
    if (device_id < context->target_device_start || device_id > context->target_device_end) {
        log_debug("[BACnet] Received I-Am from device {} (not in target range {}-{})", 
                  device_id, context->target_device_start, context->target_device_end);
        return;
    }

    // 缓存设备地址
    address_add(device_id, max_apdu, src);
    context->device_address_to_id[*src] = device_id;
    context->target_found.store(true, std::memory_order_release);
    
    log_info("[BACnet] Target device {} discovered and cached (max_apdu: {}, vendor: {})", 
             device_id, max_apdu, vendor_id);
}

/* -------------------------------------------------------------------------- */
/* ReadProperty Ack 响应处理                                                  */
/* -------------------------------------------------------------------------- */

void handle_read_property_ack(uint8_t *service_request, uint16_t service_len, 
                               BACNET_ADDRESS *src, BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    BacnetContext *context = g_ctx;
    if (!context) {
        log_error("[BACnet] ReadProperty Ack handler called but no context");
        return;
    }

    log_debug("[BACnet] ReadProperty Ack received (invoke_id: {})", service_data->invoke_id);

    // 解析ReadProperty ACK响应数据
    BACNET_READ_PROPERTY_DATA data{};
    int len = rp_ack_decode_service_request(service_request, service_len, &data);
    if (len < 0) {
        log_error("[BACnet] Failed to decode ReadProperty ACK response");
        return;
    }

    // 获取设备ID
    uint32_t device_id = context->device_address_to_id[*src];

    // 通过invoke_id查找对应的读队列项（优先匹配），如果找不到则通过设备+对象匹配
    bool found_item = false;
    {
        std::lock_guard<std::mutex> lock(context->read_queue_mutex);
        
        // 首先尝试通过invoke_id精确匹配
        for (size_t i = 0; i < context->read_count; ++i) {
            size_t idx = (context->read_head + i) % BacnetContext::kReadQueueSize;
            auto &item = context->read_queue[idx];
            
            if (item.invoke_id == service_data->invoke_id && !item.is_completed) {
                log_debug("[BACnet] Found queue item by invoke_id: {}", service_data->invoke_id);
                found_item = true;
                
                // 解码并更新数据
                BACNET_APPLICATION_DATA_VALUE value{};
                int decoded_len = bacapp_decode_application_data(
                    data.application_data, data.application_data_len, &value);
                
                if (decoded_len > 0) {
                    bacnet_read_t temp_req = {};
                    temp_req.value = &item.value;
                    proto_status_t status = store_application_value(&temp_req, value);
                    
                    if (status == PROTO_SUCCESS) {
                        item.status = PROTO_SUCCESS;
                        item.is_completed = true;
                        log_info("[BACnet] ReadProperty successful (device: {}, object: {}/{}, property: {}, invoke_id: {})", 
                                 device_id, data.object_type, data.object_instance, item.property_id, service_data->invoke_id);
                    } else {
                        log_error("[BACnet] Failed to store application value (status: {}, invoke_id: {})", status, service_data->invoke_id);
                        bacnet_data_value_free(&item.value);
                        item.status = status;
                        item.is_completed = true;
                    }
                } else {
                    log_error("[BACnet] Failed to decode application data (invoke_id: {})", service_data->invoke_id);
                    bacnet_data_value_free(&item.value);
                    item.status = PROTO_ERROR_READ;
                    item.is_completed = true;
                }
                break;
            }
        }
        
        // 如果通过invoke_id没找到，尝试通过设备+对象匹配（兼容旧逻辑）
        if (!found_item) {
            log_debug("[BACnet] invoke_id match failed, trying device+object match for invoke_id: {}", service_data->invoke_id);
            for (size_t i = 0; i < context->read_count; ++i) {
                size_t idx = (context->read_head + i) % BacnetContext::kReadQueueSize;
                auto &item = context->read_queue[idx];
                
                if (item.device_instance == device_id && 
                    item.object_type == data.object_type && 
                    item.object_instance == data.object_instance && 
                    !item.is_completed) {
                    log_warn("[BACnet] Using fallback device+object match for invoke_id: {} (device: {}, object: {}/{})", 
                             service_data->invoke_id, device_id, data.object_type, data.object_instance);
                    
                    // 解码并更新数据
                    BACNET_APPLICATION_DATA_VALUE value{};
                    int decoded_len = bacapp_decode_application_data(
                        data.application_data, data.application_data_len, &value);
                    
                    if (decoded_len > 0) {
                        bacnet_read_t temp_req = {};
                        temp_req.value = &item.value;
                        proto_status_t status = store_application_value(&temp_req, value);
                        
                        if (status == PROTO_SUCCESS) {
                            item.status = PROTO_SUCCESS;
                            item.is_completed = true;
                            log_info("[BACnet] ReadProperty successful via fallback (device: {}, object: {}/{}, property: {}, invoke_id: {})", 
                                     device_id, data.object_type, data.object_instance, item.property_id, service_data->invoke_id);
                            found_item = true;
                        } else {
                            log_error("[BACnet] Failed to store application value via fallback (status: {}, invoke_id: {})", status, service_data->invoke_id);
                            bacnet_data_value_free(&item.value);
                            item.status = status;
                            item.is_completed = true;
                        }
                    } else {
                        log_error("[BACnet] Failed to decode application data via fallback (invoke_id: {})", service_data->invoke_id);
                        bacnet_data_value_free(&item.value);
                        item.status = PROTO_ERROR_READ;
                        item.is_completed = true;
                    }
                    break;
                }
            }
        }
    }
    
    if (!found_item) {
        log_warn("[BACnet] No matching read queue item found for invoke_id: {} (device: {}, object: {}/{})", 
                 service_data->invoke_id, device_id, data.object_type, data.object_instance);
    }

    // 释放TSM资源
    if (service_data->invoke_id != 0) {
        tsm_free_invoke_id(service_data->invoke_id);
    }
}

/* -------------------------------------------------------------------------- */
/* WriteProperty Simple Ack 响应处理                                          */
/* -------------------------------------------------------------------------- */

void handle_write_property_ack(BACNET_ADDRESS *src, uint8_t invoke_id)
{
    BacnetContext *context = g_ctx;
    if (!context) {
        log_error("[BACnet] WriteProperty Ack handler called but no context");
        return;
    }

    log_debug("[BACnet] WriteProperty Ack received (invoke_id: {})", invoke_id);

    // 通过invoke_id查找对应的写队列项
    bool found_item = false;
    {
        std::lock_guard<std::mutex> lock(context->write_queue_mutex);
        for (size_t i = 0; i < context->write_count; ++i) {
            size_t idx = (context->write_head + i) % BacnetContext::kWriteQueueSize;
            auto &item = context->write_queue[idx];
            
            if (item.invoke_id == invoke_id) {
                // 找到了对应的队列项，标记为成功
                item.status = PROTO_SUCCESS;
                item.is_completed = true;  // 标记为已完成
                log_info("[BACnet] WriteProperty successful (device: {}, object: {}/{}, property: {}, invoke_id: {})", 
                         item.device_instance, item.object_type, item.object_instance, item.property_id, invoke_id);
                found_item = true;
                break;
            }
        }
    }
    
    if (!found_item) {
        log_warn("[BACnet] No matching write queue item found for invoke_id: {}", invoke_id);
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
    BacnetContext *context = g_ctx;
    if (!context) {
        return;
    }

    log_error("[BACnet] Error response received: {} - {} (invoke_id: {})",
              bactext_error_class_name(error_class),
              bactext_error_code_name(error_code),
              invoke_id);

    // 通过invoke_id查找对应的队列项并更新状态
    bool found_item = false;
    {
        // 首先尝试在读队列中查找
        std::lock_guard<std::mutex> lock_read(context->read_queue_mutex);
        for (size_t i = 0; i < context->read_count; ++i) {
            size_t idx = (context->read_head + i) % BacnetContext::kReadQueueSize;
            auto &item = context->read_queue[idx];
            
            if (item.invoke_id == invoke_id) {
                item.status = PROTO_ERROR_READ;
                item.is_completed = true;  // 标记为已完成
                log_error("[BACnet] ReadProperty failed (device: {}, object: {}/{}, property: {}, error: {}, invoke_id: {})", 
                         item.device_instance, item.object_type, item.object_instance, 
                         item.property_id, bactext_error_code_name(error_code), invoke_id);
                found_item = true;
                break;
            }
        }
    }
    
    if (!found_item) {
        // 如果读队列中没找到，尝试在写队列中查找
        std::lock_guard<std::mutex> lock_write(context->write_queue_mutex);
        for (size_t i = 0; i < context->write_count; ++i) {
            size_t idx = (context->write_head + i) % BacnetContext::kWriteQueueSize;
            auto &item = context->write_queue[idx];
            
            if (item.invoke_id == invoke_id) {
                item.status = PROTO_ERROR_WRITE;
                item.is_completed = true;  // 标记为已完成
                log_error("[BACnet] WriteProperty failed (device: {}, object: {}/{}, property: {}, error: {}, invoke_id: {})", 
                         item.device_instance, item.object_type, item.object_instance, 
                         item.property_id, bactext_error_code_name(error_code), invoke_id);
                found_item = true;
                break;
            }
        }
    }

    if (!found_item) {
        log_warn("[BACnet] No matching queue item found for error response (invoke_id: {})", invoke_id);
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
    BacnetContext *context = g_ctx;
    if (!context) {
        return;
    }

    log_error("[BACnet] Abort received: {} (invoke_id: {})",
              bactext_abort_reason_name(abort_reason),
              invoke_id);

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
    BacnetContext *context = g_ctx;
    if (!context) {
        return;
    }

    log_error("[BACnet] Reject received: {} (invoke_id: {})",
              bactext_reject_reason_name(reject_reason),
              invoke_id);

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
