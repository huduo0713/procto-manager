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

    // 对于并发操作，我们直接处理ACK，不依赖active_operation
    // BACnet协议栈的TSM会确保invoke_id的正确性

    // 解码ReadProperty响应
    BACNET_READ_PROPERTY_DATA data;
    int len = rp_ack_decode_service_request(service_request, service_len, &data);
    if (len < 0) {
        log_error("[BACnet] Failed to decode ReadProperty Ack (invoke_id: {})", service_data->invoke_id);
        // 无法确定具体操作，记录错误但不调用finalize_operation
        return;
    }

    // 提取应用层数据并推入读缓冲队列
    proto_status_t status = PROTO_ERROR_READ;
    if (data.application_data && data.application_data_len > 0) {
        BACNET_APPLICATION_DATA_VALUE value{};
        int dec_len = bacapp_decode_application_data(
            data.application_data,
            static_cast<uint8_t>(data.application_data_len),
            &value
        );
        if (dec_len > 0) {
            bacnet_data_value_t out_value{};
            bacnet_read_t temp_req = {}; // 临时请求结构体用于转换
            temp_req.value = &out_value;
            status = store_application_value(&temp_req, value);
            if (status == PROTO_SUCCESS) {
                // 推入读队列
                {
                    std::lock_guard<std::mutex> lock(context->read_queue_mutex);
                    size_t next_tail = (context->read_tail + 1) % BacnetContext::kReadQueueSize;
                    if (context->read_count < BacnetContext::kReadQueueSize) {
                        auto &item = context->read_queue[context->read_tail];
                        // 从上下文中获取目标设备ID（我们只连接到一个设备）
                        item.device_instance = context->target_device_start; // 使用配置的目标设备
                        item.object_type = data.object_type;
                        item.object_instance = data.object_instance;
                        item.property_id = data.object_property;
                        item.value = out_value;  // 复制结构和指针
                        item.timestamp = std::chrono::steady_clock::now();
                        context->read_tail = next_tail;
                        context->read_count++;
                        log_info("[BACnet] ReadProperty successful (device: {}, object: {}/{}, property: {}, invoke_id: {})", 
                                 context->target_device_start, data.object_type, data.object_instance, data.object_property, service_data->invoke_id);
                        // 注意：内存所有权已转移到队列，out_value不再拥有内存
                        std::memset(&out_value, 0, sizeof(out_value));
                    } else {
                        log_warn("[BACnet] Read queue full, discarding data (invoke_id: {})", service_data->invoke_id);
                        // 释放未使用的内存
                        bacnet_data_value_free(&out_value);
                        status = PROTO_ERROR_READ;
                    }
                }
            } else {
                log_error("[BACnet] Failed to store application value (status: {}, invoke_id: {})", status, service_data->invoke_id);
                // 释放失败时的内存
                bacnet_data_value_free(&out_value);
            }
        } else {
            log_error("[BACnet] Failed to decode application data (invoke_id: {})", service_data->invoke_id);
        }
    } else {
        log_warn("[BACnet] No application data in ReadProperty Ack (invoke_id: {})", service_data->invoke_id);
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

    // 对于并发操作，我们直接处理ACK，不依赖active_operation
    log_info("[BACnet] WriteProperty successful (invoke_id: {})", invoke_id);

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

    // 对于并发操作，我们无法确定具体的操作类型
    // 记录错误并释放TSM资源
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
