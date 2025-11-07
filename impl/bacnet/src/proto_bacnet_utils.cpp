#include "proto_bacnet_internal.hpp"
#include <bacnet/bactext.h>

#include <yaml.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <strings.h>

extern "C" {
#include "bacnet/bacdef.h"
#include "bacnet/bacenum.h"
#include "bacnet/bacapp.h"
}

namespace {

constexpr int kDefaultWhoIsRetry = 3;
constexpr uint32_t kDefaultDiscoveryTimeoutMs = 5000;
constexpr uint32_t kDefaultReadTimeoutMs = 6000;
constexpr uint32_t kDefaultWriteTimeoutMs = 6000;
constexpr uint8_t kDefaultWritePriority = 0;
constexpr uint16_t kDefaultPort = 47808;

void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dst_size, "%s", src);
}

int parse_bool(const char *value, int default_val)
{
    if (!value) {
        return default_val;
    }
    if (!strcasecmp(value, "true") || !strcasecmp(value, "yes") || !strcmp(value, "1")) {
        return 1;
    }
    if (!strcasecmp(value, "false") || !strcasecmp(value, "no") || !strcmp(value, "0")) {
        return 0;
    }
    return default_val;
}

enum class Section {
    Root,
    Common,
    Protocols,
    Bacnet,
    Discovery,
    LocalDevice,
    Network,
    Services
};

} // namespace

extern "C" {

int bacnet_load_config_from_yaml(const char *yaml_path, bacnet_config_t *cfg)
{
    if (!yaml_path || !cfg) {
        log_error("[BACnet][Config] Invalid parameters");
        return -1;
    }

    log_debug("[BACnet][Config] Using default configuration (skipping file parsing for now)");

    // 初始化为默认值
    std::memset(cfg, 0, sizeof(*cfg));

    // 设置默认值
    copy_str(cfg->common.environment, sizeof(cfg->common.environment), "development");
    copy_str(cfg->common.log_level, sizeof(cfg->common.log_level), "debug");
    copy_str(cfg->common.log_file, sizeof(cfg->common.log_file), "bacnet.log");

    cfg->bacnet.enabled = true;
    cfg->bacnet.discovery.target_device_start = 5678;  // 测试设备实例
    cfg->bacnet.discovery.target_device_end = 5678;    // 测试设备实例
    cfg->bacnet.discovery.whois_retry = 3;
    cfg->bacnet.discovery.response_timeout_ms = 5000;

    cfg->bacnet.local_device.instance_id = 4194303;
    cfg->bacnet.local_device.max_apdu = 1476;

    cfg->bacnet.network.port = 47808;
    copy_str(cfg->bacnet.network.broadcast_address,
             sizeof(cfg->bacnet.network.broadcast_address),
             "255.255.255.255");

    cfg->bacnet.services.read_timeout_ms = 6000;
    cfg->bacnet.services.write_timeout_ms = 6000;
    cfg->bacnet.services.default_priority = 8;
    cfg->bacnet.services.cache_expiry_ms = 1000;     // 默认缓存1秒过期
    cfg->bacnet.services.cache_strategy = 0;         // 默认激进策略(每次都发送)

    log_debug("[BACnet][Config] Configuration loaded with default values (target device: {}-{})",
              cfg->bacnet.discovery.target_device_start, cfg->bacnet.discovery.target_device_end);
    return 0;
}

/**
 * @brief 释放 bacnet_data_value_t 中动态分配的内存
 * @param value 要释放内存的 bacnet_data_value_t 结构体
 */
void bacnet_data_value_free(bacnet_data_value_t *value)
{
    if (!value) {
        return;
    }
    
    switch (value->type) {
        case BACNET_DATA_OCTET_STRING:
            if (value->value.octet_string.data) {
                free(value->value.octet_string.data);
                value->value.octet_string.data = nullptr;
                value->value.octet_string.length = 0;
            }
            break;
            
        case BACNET_DATA_CHARACTER_STRING:
            if (value->value.character_string.data) {
                free(value->value.character_string.data);
                value->value.character_string.data = nullptr;
                value->value.character_string.length = 0;
            }
            break;
            
        default:
            // 其他类型没有动态分配的内存
            break;
    }
}

} // extern "C"

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* 状态转换函数（增强日志可读性）                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief 将连接状态转换为可读字符串
 */
const char* connection_state_to_string(bacnet_connection_state_t state)
{
    switch (state) {
        case BACNET_CONN_IDLE:
            return "IDLE";
        case BACNET_CONN_CONNECTING:
            return "CONNECTING";
        case BACNET_CONN_CONNECTED:
            return "CONNECTED";
        case BACNET_CONN_DISCONNECTING:
            return "DISCONNECTING";
        case BACNET_CONN_DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief 将缓存策略转换为可读字符串
 */
const char* cache_strategy_to_string(CacheStrategy strategy)
{
    switch (strategy) {
        case CacheStrategy::Aggressive:
            return "Aggressive (always send requests)";
        case CacheStrategy::Conservative:
            return "Conservative (use unexpired cache)";
        default:
            return "Unknown";
    }
}

/**
 * @brief 检查 invoke_id 是否有效
 */
bool is_invoke_id_valid(uint8_t invoke_id)
{
    return invoke_id != 0xFF;  // 0xFF 表示无效
}

/**
 * @brief 将 invoke_id 转换为可读字符串
 */
const char* invoke_id_to_string(uint8_t invoke_id, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return "";
    }
    
    if (is_invoke_id_valid(invoke_id)) {
        snprintf(buffer, buffer_size, "%u", invoke_id);
    } else {
        snprintf(buffer, buffer_size, "INVALID");
    }
    
    return buffer;
}

/* -------------------------------------------------------------------------- */
/* 应用数据值转换函数                                                         */
/* -------------------------------------------------------------------------- */

proto_status_t store_application_value(bacnet_read_t *req, const BACNET_APPLICATION_DATA_VALUE &value)
{
    if (!req || !req->value) {
        return PROTO_ERROR_PARAM;
    }

    bacnet_data_value_t *out_value = req->value;
    
    switch (value.tag) {
        case BACNET_APPLICATION_TAG_NULL:
            out_value->type = BACNET_DATA_NULL;
            break;
            
        case BACNET_APPLICATION_TAG_BOOLEAN:
            out_value->type = BACNET_DATA_BOOLEAN;
            out_value->value.boolean_value = value.type.Boolean;
            break;
            
        case BACNET_APPLICATION_TAG_UNSIGNED_INT:
            out_value->type = BACNET_DATA_UNSIGNED;
            out_value->value.unsigned_value = value.type.Unsigned_Int;
            break;
            
        case BACNET_APPLICATION_TAG_SIGNED_INT:
            out_value->type = BACNET_DATA_SIGNED;
            out_value->value.signed_value = value.type.Signed_Int;
            break;
            
        case BACNET_APPLICATION_TAG_REAL:
            out_value->type = BACNET_DATA_REAL;
            out_value->value.real_value = value.type.Real;
            break;
            
        case BACNET_APPLICATION_TAG_DOUBLE:
            out_value->type = BACNET_DATA_DOUBLE;
            out_value->value.double_value = value.type.Double;
            break;
            
        case BACNET_APPLICATION_TAG_ENUMERATED:
            out_value->type = BACNET_DATA_ENUM;
            out_value->value.enum_value = value.type.Enumerated;
            break;
            
        case BACNET_APPLICATION_TAG_OCTET_STRING: {
            out_value->type = BACNET_DATA_OCTET_STRING;
            // 分配内存并复制数据，避免const转换问题
            size_t len = value.type.Octet_String.length;
            out_value->value.octet_string.data = static_cast<uint8_t*>(malloc(len));
            if (!out_value->value.octet_string.data) {
                log_error("[BACnet] Failed to allocate memory for octet string");
                return PROTO_ERROR_MEMORY;
            }
            memcpy(out_value->value.octet_string.data, value.type.Octet_String.value, len);
            out_value->value.octet_string.length = len;
            break;
        }
            
        case BACNET_APPLICATION_TAG_CHARACTER_STRING: {
            out_value->type = BACNET_DATA_CHARACTER_STRING;
            // 分配内存并复制数据，避免const转换问题
            size_t len = value.type.Character_String.length;
            out_value->value.character_string.data = static_cast<char*>(malloc(len + 1)); // +1 for null terminator
            if (!out_value->value.character_string.data) {
                log_error("[BACnet] Failed to allocate memory for character string");
                return PROTO_ERROR_MEMORY;
            }
            memcpy(out_value->value.character_string.data, value.type.Character_String.value, len);
            out_value->value.character_string.data[len] = '\0'; // null terminate
            out_value->value.character_string.length = len;
            break;
        }
            
        default:
            log_warn("[BACnet] Unsupported application data tag: {} ({})", 
                     bactext_application_tag_name(value.tag), 
                     static_cast<int>(value.tag));
            return PROTO_ERROR_UNSUPPORTED;
    }
    
    return PROTO_SUCCESS;
}

} // namespace bacnet
