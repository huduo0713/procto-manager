#include "proto_bacnet_internal.hpp"
#include <bacnet/bactext.h>

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* 解析目标设备地址                                                           */
/* -------------------------------------------------------------------------- */

BACNET_ADDRESS resolve_target_address(BacnetContext *context, uint32_t device_instance, proto_status_t &status)
{
    BACNET_ADDRESS address{};
    
    if (!context) {
        status = PROTO_ERROR_PARAM;
        return address;
    }

    // 检查设备实例是否在配置范围内
    if (device_instance < context->target_device_start || device_instance > context->target_device_end) {
        log_error("[BACnet] Device instance {} not in configured range {}-{}", 
                  device_instance, context->target_device_start, context->target_device_end);
        status = PROTO_ERROR_PARAM;
        return address;
    }

    unsigned max_apdu = 0;
    bool found = address_bind_request(device_instance, &max_apdu, &address);
    
    if (!found) {
        log_error("[BACnet] Target device {} not bound in address cache", device_instance);
        status = PROTO_ERROR_CONNECT;
    } else {
        status = PROTO_SUCCESS;
        log_debug("[BACnet] Resolved address for device {} (max_apdu: {})", 
                  device_instance, max_apdu);
    }
    
    return address;
}

/* -------------------------------------------------------------------------- */
/* 执行ReadProperty操作                                                       */
/* -------------------------------------------------------------------------- */

proto_status_t execute_read_property(BacnetContext *context, bacnet_read_t *req, uint8_t *invoke_id_out)
{
    if (!context || !req) {
        return PROTO_ERROR_PARAM;
    }

    if (!req->value) {
        log_error("[BACnet] Read request value buffer is null");
        return PROTO_ERROR_PARAM;
    }

    // 检查连接状态
    if (get_connection_state(context) != BACNET_CONN_CONNECTED) {
        log_error("[BACnet] Not connected, cannot execute read operation");
        return PROTO_ERROR_CONNECT;
    }

    // 解析目标设备地址
    proto_status_t status = PROTO_SUCCESS;
    BACNET_ADDRESS target = resolve_target_address(context, req->device_instance, status);
    if (status != PROTO_SUCCESS) {
        return status;
    }

    // BACnet协议栈支持并发操作，我们不需要严格的单操作限制
    // 只需要确保有足够的资源处理并发事务
    {
        std::lock_guard<std::mutex> lock(context->operation_mutex);
        
        // 设置活动操作（用于超时检测，但允许多个并发操作）
        context->active_operation.type = OperationKind::Read;
        context->active_operation.request = req;
        context->active_operation.target_address = target;
        context->active_operation.device_instance = req->device_instance;
        context->active_operation.start_time = std::chrono::steady_clock::now();
        context->active_operation.timeout_ms = req->timeout_ms ? req->timeout_ms : 
                                                context->config.bacnet.services.read_timeout_ms;
        if (context->active_operation.timeout_ms == 0) {
            context->active_operation.timeout_ms = kDefaultReadTimeoutMs;
        }

        // 发送ReadProperty请求
        uint32_t array_index = (req->array_index < 0) ? BACNET_ARRAY_ALL : 
                                static_cast<uint32_t>(req->array_index);
        
        uint8_t invoke_id = Send_Read_Property_Request(
            req->device_instance,
            static_cast<BACNET_OBJECT_TYPE>(req->object_type),
            req->object_instance,
            static_cast<BACNET_PROPERTY_ID>(req->property_id),
            array_index
        );

        if (invoke_id == 0) {
            log_error("[BACnet] Failed to send ReadProperty request");
            context->active_operation.reset();
            return PROTO_ERROR_READ;
        }

        context->active_operation.invoke_id = invoke_id;
        set_operation_state(context, BACNET_OP_PENDING);

        // 将请求放入读队列，等待回调函数更新
        {
            std::lock_guard<std::mutex> lock_queue(context->read_queue_mutex);
            
            // 使用哈希表存储，invoke_id 作为 key
            BacnetContext::ReadBufferItem item{};
            item.device_instance = req->device_instance;
            item.object_type = req->object_type;
            item.object_instance = req->object_instance;
            item.property_id = req->property_id;
            item.value = *req->value;  // 复制初始值
            item.status = PROTO_SUCCESS;  // 初始状态
            item.is_completed = false;  // 等待回调更新
            item.timestamp = std::chrono::steady_clock::now();
            item.original_request = req;
            item.invoke_id = invoke_id;  // 保存invoke_id用于匹配
            
            // 插入哈希表：O(1) 操作
            context->read_queue[invoke_id] = item;
            
            log_debug("[BACnet] Read request added to map (invoke_id: {}, map size: {})", 
                     invoke_id, context->read_queue.size());
        }

        // 输出invoke_id
        if (invoke_id_out) {
            *invoke_id_out = invoke_id;
        }

        log_info("[BACnet] ReadProperty request sent (device: {}, object: {}-{}, property: {}, invoke_id: {})",
                 req->device_instance, 
                 bactext_object_type_name(req->object_type), 
                 req->object_instance,
                 bactext_property_name(req->property_id),
                 invoke_id);
    }

    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 执行WriteProperty操作                                                      */
/* -------------------------------------------------------------------------- */

proto_status_t execute_write_property(BacnetContext *context, const bacnet_write_t *req, uint8_t *invoke_id_out)
{
    if (!context || !req) {
        return PROTO_ERROR_PARAM;
    }

    // 检查连接状态
    if (get_connection_state(context) != BACNET_CONN_CONNECTED) {
        log_error("[BACnet] Not connected, cannot execute write operation");
        return PROTO_ERROR_CONNECT;
    }

    // 解析目标设备地址
    proto_status_t status = PROTO_SUCCESS;
    BACNET_ADDRESS target = resolve_target_address(context, req->device_instance, status);
    if (status != PROTO_SUCCESS) {
        return status;
    }

    // 转换数据值
    BACNET_APPLICATION_DATA_VALUE value{};
    if (!convert_to_application_value(req->value, value)) {
        log_error("[BACnet] Failed to convert data value for write operation");
        return PROTO_ERROR_PARAM;
    }

    // BACnet协议栈支持并发操作，我们不需要严格的单操作限制
    {
        std::lock_guard<std::mutex> lock(context->operation_mutex);
        
        // 设置活动操作（用于超时检测，但允许多个并发操作）
        context->active_operation.type = OperationKind::Write;
        context->active_operation.request = const_cast<bacnet_write_t *>(req);
        context->active_operation.target_address = target;
        context->active_operation.device_instance = req->device_instance;
        context->active_operation.start_time = std::chrono::steady_clock::now();
        context->active_operation.timeout_ms = req->timeout_ms ? req->timeout_ms : 
                                                context->config.bacnet.services.write_timeout_ms;
        if (context->active_operation.timeout_ms == 0) {
            context->active_operation.timeout_ms = kDefaultWriteTimeoutMs;
        }

        // 确定写入优先级
        uint8_t priority = (req->priority == 0) ? 
                           context->config.bacnet.services.default_priority : req->priority;
        if (priority == 0) {
            priority = BACNET_MAX_PRIORITY;
        }

        // 发送WriteProperty请求
        uint32_t array_index = (req->array_index < 0) ? BACNET_ARRAY_ALL : 
                                static_cast<uint32_t>(req->array_index);
        
        uint8_t invoke_id = Send_Write_Property_Request(
            req->device_instance,
            static_cast<BACNET_OBJECT_TYPE>(req->object_type),
            req->object_instance,
            static_cast<BACNET_PROPERTY_ID>(req->property_id),
            &value,
            priority,
            array_index
        );

        if (invoke_id == 0) {
            log_error("[BACnet] Failed to send WriteProperty request");
            context->active_operation.reset();
            return PROTO_ERROR_WRITE;
        }

        context->active_operation.invoke_id = invoke_id;
        set_operation_state(context, BACNET_OP_PENDING);

        // 将请求添加到写队列
        {
            std::lock_guard<std::mutex> lock_queue(context->write_queue_mutex);
            
            // 使用哈希表存储，invoke_id 作为 key
            BacnetContext::WriteBufferItem item{};
            item.device_instance = req->device_instance;
            item.object_type = req->object_type;
            item.object_instance = req->object_instance;
            item.property_id = req->property_id;
            item.invoke_id = invoke_id;
            item.status = PROTO_SUCCESS;  // 初始状态为成功，等待ACK或错误
            item.is_completed = false;    // 标记为未完成
            item.timestamp = std::chrono::steady_clock::now();

            // 插入哈希表：O(1) 操作
            context->write_queue[invoke_id] = item;
            
            log_debug("[BACnet] Write request added to map (invoke_id: {}, map size: {})", 
                     invoke_id, context->write_queue.size());
        }

        // 输出invoke_id
        if (invoke_id_out) {
            *invoke_id_out = invoke_id;
        }

        log_info("[BACnet] WriteProperty request sent and added to map (device: {}, object: {}-{}, property: {}, priority: {}, invoke_id: {})",
                 req->device_instance, 
                 bactext_object_type_name(req->object_type), 
                 req->object_instance,
                 bactext_property_name(req->property_id),
                 priority, 
                 invoke_id);
    }

    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 转换用户数据值为应用层数据值                                                */
/* -------------------------------------------------------------------------- */

bool convert_to_application_value(const bacnet_data_value_t &input, BACNET_APPLICATION_DATA_VALUE &output)
{
    std::memset(&output, 0, sizeof(output));
    output.context_specific = false;
    output.context_tag = 0;
    output.next = nullptr;

    switch (input.type) {
        case BACNET_DATA_NULL:
            output.tag = BACNET_APPLICATION_TAG_NULL;
            break;

        case BACNET_DATA_BOOLEAN:
            output.tag = BACNET_APPLICATION_TAG_BOOLEAN;
            output.type.Boolean = input.value.boolean_value;
            break;

        case BACNET_DATA_UNSIGNED:
            output.tag = BACNET_APPLICATION_TAG_UNSIGNED_INT;
            output.type.Unsigned_Int = input.value.unsigned_value;
            break;

        case BACNET_DATA_SIGNED:
            output.tag = BACNET_APPLICATION_TAG_SIGNED_INT;
            output.type.Signed_Int = input.value.signed_value;
            break;

        case BACNET_DATA_REAL:
            output.tag = BACNET_APPLICATION_TAG_REAL;
            output.type.Real = input.value.real_value;
            break;

        case BACNET_DATA_DOUBLE:
            output.tag = BACNET_APPLICATION_TAG_DOUBLE;
            output.type.Double = input.value.double_value;
            break;

        case BACNET_DATA_ENUM:
            output.tag = BACNET_APPLICATION_TAG_ENUMERATED;
            output.type.Enumerated = input.value.enum_value;
            break;

        case BACNET_DATA_OCTET_STRING:
            if (!input.value.octet_string.data || input.value.octet_string.length == 0 ||
                input.value.octet_string.length > MAX_OCTET_STRING_BYTES) {
                log_error("[BACnet] Invalid octet string data");
                return false;
            }
            output.tag = BACNET_APPLICATION_TAG_OCTET_STRING;
            octetstring_init(&output.type.Octet_String,
                             input.value.octet_string.data,
                             static_cast<uint16_t>(input.value.octet_string.length));
            break;

        case BACNET_DATA_CHARACTER_STRING:
            if (!input.value.character_string.data || input.value.character_string.length == 0 ||
                input.value.character_string.length >= MAX_CHARACTER_STRING_BYTES) {
                log_error("[BACnet] Invalid character string data");
                return false;
            }
            output.tag = BACNET_APPLICATION_TAG_CHARACTER_STRING;
            {
                const char *data = input.value.character_string.data;
                const bool has_trailing_null = data[input.value.character_string.length - 1] == '\0';
                if (has_trailing_null) {
                    characterstring_init_ansi(&output.type.Character_String, data);
                } else {
                    std::string temp(data, data + input.value.character_string.length);
                    temp.push_back('\0');
                    characterstring_init_ansi(&output.type.Character_String, temp.c_str());
                }
            }
            break;

        default:
            log_error("[BACnet] Unsupported data type: {}", static_cast<int>(input.type));
            return false;
    }

    return true;
}

} // namespace bacnet
