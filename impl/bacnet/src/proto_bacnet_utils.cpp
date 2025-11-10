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

// 使用统一的默认值命名空间
using namespace bacnet::defaults;

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
    Services,
    Connection,
    HotConfig
};

// 辅助函数：将 ConfigSource 转换为字符串
inline const char* source_to_string(bacnet::ConfigSource src) {
    return (src == bacnet::ConfigSource::Yaml) ? "YAML   " : "DEFAULT";
}

// 打印配置表（使用元数据标记来源）
void print_config_table(const bacnet_config_t *cfg, const bacnet::ConfigMetadata *meta) {
    log_info("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓");
    log_info("┃          BACnet Configuration Loaded                         ┃");
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Common配置
    log_info("┃ [Common]                                                     ┃");
    log_info("┃   environment        : {:20s}  [{:7s}] ┃", 
             cfg->common.environment, source_to_string(meta->environment));
    log_info("┃   log_level          : {:20s}  [{:7s}] ┃", 
             cfg->common.log_level, source_to_string(meta->log_level));
    log_info("┃   log_file           : {:20s}  [{:7s}] ┃", 
             cfg->common.log_file, source_to_string(meta->log_file));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // BACnet基础配置
    log_info("┃ [BACnet]                                                     ┃");
    log_info("┃   enabled            : {:20s}  [{:7s}] ┃", 
             cfg->bacnet.enabled ? "true" : "false", 
             source_to_string(meta->bacnet_enabled));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Discovery配置
    log_info("┃ [BACnet.Discovery]                                           ┃");
    log_info("┃   target_device_start: {:20d}  [{:7s}] ┃", 
             cfg->bacnet.discovery.target_device_start,
             source_to_string(meta->target_device_start));
    log_info("┃   target_device_end  : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.discovery.target_device_end,
             source_to_string(meta->target_device_end));
    log_info("┃   whois_retry        : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.discovery.whois_retry,
             source_to_string(meta->whois_retry));
    log_info("┃   response_timeout_ms: {:20d}  [{:7s}] ┃", 
             cfg->bacnet.discovery.response_timeout_ms,
             source_to_string(meta->response_timeout_ms));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // LocalDevice配置
    log_info("┃ [BACnet.LocalDevice]                                         ┃");
    log_info("┃   instance_id        : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.local_device.instance_id,
             source_to_string(meta->instance_id));
    log_info("┃   max_apdu           : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.local_device.max_apdu,
             source_to_string(meta->max_apdu));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Network配置
    log_info("┃ [BACnet.Network]                                             ┃");
    log_info("┃   interface          : {:20s}  [{:7s}] ┃", 
             (cfg->bacnet.network.interface_name[0] != '\0') ? cfg->bacnet.network.interface_name : "(auto)",
             source_to_string(meta->interface_name));
    log_info("┃   port               : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.network.port,
             source_to_string(meta->port));
    log_info("┃   broadcast_address  : {:20s}  [{:7s}] ┃", 
             cfg->bacnet.network.broadcast_address,
             source_to_string(meta->broadcast_address));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Services配置
    log_info("┃ [BACnet.Services]                                            ┃");
    log_info("┃   read_timeout_ms    : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.services.read_timeout_ms,
             source_to_string(meta->read_timeout_ms));
    log_info("┃   write_timeout_ms   : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.services.write_timeout_ms,
             source_to_string(meta->write_timeout_ms));
    log_info("┃   default_priority   : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.services.default_priority,
             source_to_string(meta->default_priority));
    log_info("┃   cache_expiry_ms    : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.services.cache_expiry_ms,
             source_to_string(meta->cache_expiry_ms));
    log_info("┃   cache_strategy     : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.services.cache_strategy,
             source_to_string(meta->cache_strategy));
    log_info("┃   datalink_maint_ms  : {:20d}  [{:7s}] ┃", 
             cfg->bacnet.services.datalink_maintenance_ms,
             source_to_string(meta->datalink_maintenance_ms));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Connection配置
    log_info("┃ [BACnet.Connection]                                          ┃");
    log_info("┃   max_reconnect_attempts: {:17d}  [{:7s}] ┃", 
             cfg->bacnet.connection.max_reconnect_attempts,
             source_to_string(meta->max_reconnect_attempts));
    log_info("┃   reconnect_interval_ms : {:17d}  [{:7s}] ┃", 
             cfg->bacnet.connection.reconnect_interval_ms,
             source_to_string(meta->reconnect_interval_ms));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // HotConfig配置
    log_info("┃ [BACnet.HotConfig]                                           ┃");
    log_info("┃   enabled            : {:<20}  [{:7s}] ┃", 
             cfg->bacnet.hot_config.enabled ? "true" : "false",
             source_to_string(meta->hot_config_enabled));
    log_info("┃   polling_interval_ms: {:20d}  [{:7s}] ┃", 
             cfg->bacnet.hot_config.polling_interval_ms,
             source_to_string(meta->hot_config_polling_interval_ms));
    
    log_info("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛");
}

} // namespace

// 全局配置元数据（用于追踪配置来源）
static bacnet::ConfigMetadata g_config_metadata;

int bacnet_load_config_from_yaml(const char *yaml_path, bacnet_config_t *cfg)
{
    if (!yaml_path || !cfg) {
        log_error("[BACnet][Config] Invalid parameters");
        return -1;
    }

    // 1. 初始化为默认值（兜底配置）
    std::memset(cfg, 0, sizeof(*cfg));
    
    copy_str(cfg->common.environment, sizeof(cfg->common.environment), kEnvironment);
    copy_str(cfg->common.log_level, sizeof(cfg->common.log_level), kLogLevel);
    copy_str(cfg->common.log_file, sizeof(cfg->common.log_file), kLogFile);

    cfg->bacnet.enabled = true;
    cfg->bacnet.discovery.target_device_start = kTargetDeviceStart;
    cfg->bacnet.discovery.target_device_end = kTargetDeviceEnd;
    cfg->bacnet.discovery.whois_retry = kWhoIsRetry;
    cfg->bacnet.discovery.response_timeout_ms = kDiscoveryTimeoutMs;

    cfg->bacnet.local_device.instance_id = kLocalDeviceInstance;
    cfg->bacnet.local_device.max_apdu = kMaxApdu;

    cfg->bacnet.network.port = kPort;
    copy_str(cfg->bacnet.network.broadcast_address,
             sizeof(cfg->bacnet.network.broadcast_address),
             kBroadcastAddress);

    cfg->bacnet.services.read_timeout_ms = kReadTimeoutMs;
    cfg->bacnet.services.write_timeout_ms = kWriteTimeoutMs;
    cfg->bacnet.services.default_priority = kDefaultPriority;
    cfg->bacnet.services.cache_expiry_ms = kCacheExpiryMs;
    cfg->bacnet.services.cache_strategy = kCacheStrategy;
    cfg->bacnet.services.datalink_maintenance_ms = kDatalinkMaintenanceMs;

    cfg->bacnet.connection.max_reconnect_attempts = kMaxReconnectAttempts;
    cfg->bacnet.connection.reconnect_interval_ms = kReconnectIntervalMs;

    cfg->bacnet.hot_config.enabled = kHotConfigEnabled;
    cfg->bacnet.hot_config.polling_interval_ms = kHotConfigPollingIntervalMs;

    // 2. 尝试打开配置文件
    FILE *file = fopen(yaml_path, "r");
    if (!file) {
        log_warn("[BACnet][Config] Cannot open config file '{}', using default values", yaml_path);
        print_config_table(cfg, &g_config_metadata);
        return 0;  // 使用默认值也算成功
    }

    // 3. 初始化 YAML 解析器
    yaml_parser_t parser;
    yaml_event_t event;
    
    if (!yaml_parser_initialize(&parser)) {
        log_error("[BACnet][Config] Failed to initialize YAML parser");
        fclose(file);
        return 0;  // 保留默认值
    }

    yaml_parser_set_input_file(&parser, file);

    // 4. 解析 YAML 文件
    Section current_section = Section::Root;
    std::string last_key;
    bool done = false;
    bool parse_error = false;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            log_error("[BACnet][Config] YAML parse error at line {}", parser.problem_mark.line);
            parse_error = true;
            break;
        }

        switch (event.type) {
            case YAML_SCALAR_EVENT: {
                const char *value = reinterpret_cast<const char *>(event.data.scalar.value);
                
                if (last_key.empty()) {
                    // 这是一个键，先判断是否是 section 切换键
                    bool is_section_key = false;
                    
                    if (current_section == Section::Root) {
                        if (strcmp(value, "common") == 0) {
                            current_section = Section::Common;
                            is_section_key = true;
                        } else if (strcmp(value, "protocols") == 0) {
                            current_section = Section::Protocols;
                            is_section_key = true;
                        }
                    } else if (current_section == Section::Protocols) {
                        if (strcmp(value, "bacnet") == 0) {
                            current_section = Section::Bacnet;
                            is_section_key = true;
                        }
                    } else if (current_section == Section::Bacnet) {
                        if (strcmp(value, "discovery") == 0) {
                            current_section = Section::Discovery;
                            is_section_key = true;
                        } else if (strcmp(value, "local_device") == 0) {
                            current_section = Section::LocalDevice;
                            is_section_key = true;
                        } else if (strcmp(value, "network") == 0) {
                            current_section = Section::Network;
                            is_section_key = true;
                        } else if (strcmp(value, "services") == 0) {
                            current_section = Section::Services;
                            is_section_key = true;
                        } else if (strcmp(value, "connection") == 0) {
                            current_section = Section::Connection;
                            is_section_key = true;
                        } else if (strcmp(value, "hot_config") == 0) {
                            current_section = Section::HotConfig;
                            is_section_key = true;
                        }
                    }
                    
                    // 如果不是 section 切换键，才保存为 last_key
                    if (!is_section_key) {
                        last_key = value;
                    }
                } else {
                    // 这是一个值，根据当前 section 和 key 设置配置
                    if (current_section == Section::Common) {
                        if (last_key == "environment") {
                            copy_str(cfg->common.environment, sizeof(cfg->common.environment), value);
                            g_config_metadata.environment = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "log_level") {
                            copy_str(cfg->common.log_level, sizeof(cfg->common.log_level), value);
                            g_config_metadata.log_level = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "log_file") {
                            copy_str(cfg->common.log_file, sizeof(cfg->common.log_file), value);
                            g_config_metadata.log_file = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Bacnet) {
                        if (last_key == "enabled") {
                            cfg->bacnet.enabled = parse_bool(value, true);
                            g_config_metadata.bacnet_enabled = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Discovery) {
                        if (last_key == "target_device_start") {
                            cfg->bacnet.discovery.target_device_start = std::atoi(value);
                            g_config_metadata.target_device_start = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "target_device_end") {
                            cfg->bacnet.discovery.target_device_end = std::atoi(value);
                            g_config_metadata.target_device_end = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "whois_retry") {
                            cfg->bacnet.discovery.whois_retry = std::atoi(value);
                            g_config_metadata.whois_retry = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "response_timeout_ms") {
                            cfg->bacnet.discovery.response_timeout_ms = std::atoi(value);
                            g_config_metadata.response_timeout_ms = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::LocalDevice) {
                        if (last_key == "instance_id") {
                            cfg->bacnet.local_device.instance_id = std::atoi(value);
                            g_config_metadata.instance_id = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "max_apdu") {
                            cfg->bacnet.local_device.max_apdu = std::atoi(value);
                            g_config_metadata.max_apdu = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Network) {
                        if (last_key == "interface") {
                            copy_str(cfg->bacnet.network.interface_name, sizeof(cfg->bacnet.network.interface_name), value);
                            g_config_metadata.interface_name = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "port") {
                            cfg->bacnet.network.port = std::atoi(value);
                            g_config_metadata.port = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "broadcast_address") {
                            copy_str(cfg->bacnet.network.broadcast_address, 
                                    sizeof(cfg->bacnet.network.broadcast_address), value);
                            g_config_metadata.broadcast_address = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Services) {
                        if (last_key == "read_timeout_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->bacnet.services.read_timeout_ms = (val > 0) ? val : kReadTimeoutMs;
                            g_config_metadata.read_timeout_ms = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "write_timeout_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->bacnet.services.write_timeout_ms = (val > 0) ? val : kWriteTimeoutMs;
                            g_config_metadata.write_timeout_ms = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "default_priority") {
                            uint8_t val = std::atoi(value);
                            cfg->bacnet.services.default_priority = (val > 0) ? val : kDefaultPriority;
                            g_config_metadata.default_priority = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "cache_expiry_ms") {
                            cfg->bacnet.services.cache_expiry_ms = std::atoi(value);
                            g_config_metadata.cache_expiry_ms = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "cache_strategy") {
                            cfg->bacnet.services.cache_strategy = std::atoi(value);
                            g_config_metadata.cache_strategy = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "datalink_maintenance_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->bacnet.services.datalink_maintenance_ms = (val > 0) ? val : kDatalinkMaintenanceMs;
                            g_config_metadata.datalink_maintenance_ms = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Connection) {
                        if (last_key == "max_reconnect_attempts") {
                            uint8_t val = std::atoi(value);
                            cfg->bacnet.connection.max_reconnect_attempts = (val > 0) ? val : kMaxReconnectAttempts;
                            g_config_metadata.max_reconnect_attempts = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "reconnect_interval_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->bacnet.connection.reconnect_interval_ms = (val > 0) ? val : kReconnectIntervalMs;
                            g_config_metadata.reconnect_interval_ms = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::HotConfig) {
                        if (last_key == "enabled") {
                            cfg->bacnet.hot_config.enabled = parse_bool(value, true);
                            g_config_metadata.hot_config_enabled = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "polling_interval_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->bacnet.hot_config.polling_interval_ms = (val > 0) ? val : kHotConfigPollingIntervalMs;
                            g_config_metadata.hot_config_polling_interval_ms = bacnet::ConfigSource::Yaml;
                        }
                    }
                    
                    last_key.clear();
                }
                break;
            }
            
            case YAML_MAPPING_END_EVENT:
                // 映射结束，返回上一级
                if (current_section == Section::Discovery || 
                    current_section == Section::LocalDevice ||
                    current_section == Section::Network ||
                    current_section == Section::Services ||
                    current_section == Section::Connection ||
                    current_section == Section::HotConfig) {
                    current_section = Section::Bacnet;
                } else if (current_section == Section::Bacnet) {
                    current_section = Section::Protocols;
                } else if (current_section == Section::Common || current_section == Section::Protocols) {
                    current_section = Section::Root;
                }
                break;
                
            case YAML_STREAM_END_EVENT:
                done = true;
                break;
                
            default:
                break;
        }
        
        yaml_event_delete(&event);
    }

    // 5. 清理资源
    yaml_parser_delete(&parser);
    fclose(file);

    // 6. 打印配置表
    if (parse_error) {
        log_warn("[BACnet][Config] YAML parse error, some values may use defaults");
    } else {
        log_info("[BACnet][Config] Configuration loaded from '{}'", yaml_path);
    }
    
    print_config_table(cfg, &g_config_metadata);

    return 0;
}

extern "C" {

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

/* -------------------------------------------------------------------------- */
/* 错误码转字符串（C 接口）                                                   */
/* -------------------------------------------------------------------------- */

extern "C" {

const char* proto_status_to_string(proto_status_t status) {
    switch (status) {
        case PROTO_SUCCESS:
            return "成功 (PROTO_SUCCESS)";
        case PROTO_ERROR_INIT:
            return "初始化失败 (PROTO_ERROR_INIT)";
        case PROTO_ERROR_CONNECT:
            return "连接失败 (PROTO_ERROR_CONNECT)";
        case PROTO_ERROR_READ:
            return "读取失败 (PROTO_ERROR_READ)";
        case PROTO_ERROR_WRITE:
            return "写入失败 (PROTO_ERROR_WRITE)";
        case PROTO_ERROR_UNSUPPORTED:
            return "不支持的操作 (PROTO_ERROR_UNSUPPORTED)";
        case PROTO_ERROR_PARAM:
            return "参数错误 (PROTO_ERROR_PARAM)";
        case PROTO_NO_DATA:
            return "暂无数据 (PROTO_NO_DATA)";
        case PROTO_ERROR_MEMORY:
            return "内存错误 (PROTO_ERROR_MEMORY)";
        case PROTO_TIMEOUT:
            return "超时 (PROTO_TIMEOUT)";
        default:
            return "未知错误";
    }
}

} // extern "C"

} // namespace bacnet
