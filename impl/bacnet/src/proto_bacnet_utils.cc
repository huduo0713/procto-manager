#include "proto_bacnet_internal.hpp"
#include <bacnet/bactext.h>

#include <yaml.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <strings.h>
#include <vector>
#include <unistd.h>      // fsync, fileno, getpid
#include <sys/file.h>    // flock

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
    Protocols,
    Bacnet,
    Discovery,
    LocalDevice,
    Network,
    MSTP,          // 新增：MS/TP 串口配置 section
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
    log_info("┊          BACnet Configuration Loaded                         ┃");
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Discovery配置
    log_info("┊ [Discovery]                                                  ┃");
    log_info("┃   target_device_start: {:20d}  [{:7s}] ┃", 
             cfg->target_device_start,
             source_to_string(meta->target_device_start));
    log_info("┃   target_device_end  : {:20d}  [{:7s}] ┃", 
             cfg->target_device_end,
             source_to_string(meta->target_device_end));
    log_info("┃   whois_retry        : {:20d}  [{:7s}] ┃", 
             cfg->whois_retry,
             source_to_string(meta->whois_retry));
    log_info("┃   response_timeout_ms: {:20d}  [{:7s}] ┃", 
             cfg->response_timeout_ms,
             source_to_string(meta->response_timeout_ms));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // LocalDevice配置
    log_info("┃ [LocalDevice]                                                ┃");
    log_info("┃   instance_id        : {:20d}  [{:7s}] ┃", 
             cfg->local_instance_id,
             source_to_string(meta->instance_id));
    log_info("┃   max_apdu           : {:20d}  [{:7s}] ┃", 
             cfg->local_max_apdu,
             source_to_string(meta->max_apdu));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Network配置
    log_info("┃ [Network]                                                    ┃");
    log_info("┃   interface          : {:20s}  [{:7s}] ┃", 
             (cfg->interface_name[0] != '\0') ? cfg->interface_name : "(auto)",
             source_to_string(meta->interface_name));
    log_info("┃   port               : {:20d}  [{:7s}] ┃", 
             cfg->port,
             source_to_string(meta->port));
    log_info("┃   broadcast_address  : {:20s}  [{:7s}] ┃", 
             cfg->broadcast_address,
             source_to_string(meta->broadcast_address));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // MS/TP 配置（如果启用）
    if (cfg->mstp_port[0] != '\0') {
        log_info("┃ [MS/TP] (RS-485 Serial)                                      ┃");
        log_info("┃   port               : {:20s}  [{:7s}] ┃", 
                 cfg->mstp_port,
                 source_to_string(meta->mstp_port));
        log_info("┃   baud_rate          : {:20d}  [{:7s}] ┃", 
                 cfg->mstp_baud,
                 source_to_string(meta->mstp_baud));
        log_info("┃   mac_address        : {:20d}  [{:7s}] ┃", 
                 cfg->mstp_mac,
                 source_to_string(meta->mstp_mac));
        log_info("┃   max_master         : {:20d}  [{:7s}] ┃", 
                 cfg->mstp_max_master,
                 source_to_string(meta->mstp_max_master));
        log_info("┃   max_info_frames    : {:20d}  [{:7s}] ┃", 
                 cfg->mstp_max_frames,
                 source_to_string(meta->mstp_max_frames));
    } else {
        log_info("┃ [MS/TP] (RS-485 Serial)                                      ┃");
        log_info("┃   Status: Disabled (no port configured)                      ┃");
    }
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Services配置
    log_info("┃ [Services]                                                   ┃");
    log_info("┃   read_timeout_ms    : {:20d}  [{:7s}] ┃", 
             cfg->read_timeout_ms,
             source_to_string(meta->read_timeout_ms));
    log_info("┃   write_timeout_ms   : {:20d}  [{:7s}] ┃", 
             cfg->write_timeout_ms,
             source_to_string(meta->write_timeout_ms));
    log_info("┃   default_priority   : {:20d}  [{:7s}] ┃", 
             cfg->default_priority,
             source_to_string(meta->default_priority));
    log_info("┃   cache_expiry_ms    : {:20d}  [{:7s}] ┃", 
             cfg->cache_expiry_ms,
             source_to_string(meta->cache_expiry_ms));
    log_info("┃   cache_strategy     : {:20d}  [{:7s}] ┃", 
             cfg->cache_strategy,
             source_to_string(meta->cache_strategy));
    log_info("┃   datalink_maint_ms  : {:20d}  [{:7s}] ┃", 
             cfg->datalink_maintenance_ms,
             source_to_string(meta->datalink_maintenance_ms));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // Connection配置
    log_info("┃ [Connection]                                                 ┃");
    log_info("┃   max_reconnect_attempts: {:17d}  [{:7s}] ┃", 
             cfg->max_reconnect_attempts,
             source_to_string(meta->max_reconnect_attempts));
    log_info("┃   reconnect_interval_ms : {:17d}  [{:7s}] ┃", 
             cfg->reconnect_interval_ms,
             source_to_string(meta->reconnect_interval_ms));
    
    log_info("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫");
    
    // HotConfig配置
    log_info("┃ [HotConfig]                                                  ┃");
    log_info("┃   enabled            : {:<20}  [{:7s}] ┃", 
             cfg->hot_config_enabled ? "true" : "false",
             source_to_string(meta->hot_config_enabled));
    log_info("┃   polling_interval_ms: {:20d}  [{:7s}] ┃", 
             cfg->hot_config_polling_ms,
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
    
    // 设备发现配置
    cfg->target_device_start = kTargetDeviceStart;
    cfg->target_device_end = kTargetDeviceEnd;
    cfg->whois_retry = kWhoIsRetry;
    cfg->response_timeout_ms = kDiscoveryTimeoutMs;

    // 本地设备参数
    cfg->local_instance_id = kLocalDeviceInstance;
    cfg->local_max_apdu = kMaxApdu;

    // 网络层配置
    cfg->port = kPort;
    copy_str(cfg->broadcast_address, sizeof(cfg->broadcast_address), kBroadcastAddress);

    // MS/TP 串口数据链路层配置
    copy_str(cfg->mstp_port, sizeof(cfg->mstp_port), kMstpPort);
    cfg->mstp_baud = kMstpBaudRate;
    cfg->mstp_mac = kMstpMacAddress;
    cfg->mstp_max_master = kMstpMaxMaster;
    cfg->mstp_max_frames = kMstpMaxInfoFrames;

    // 服务行为配置
    cfg->read_timeout_ms = kReadTimeoutMs;
    cfg->write_timeout_ms = kWriteTimeoutMs;
    cfg->default_priority = kDefaultPriority;
    cfg->cache_expiry_ms = kCacheExpiryMs;
    cfg->cache_strategy = kCacheStrategy;
    cfg->datalink_maintenance_ms = kDatalinkMaintenanceMs;

    // 连接管理配置
    cfg->max_reconnect_attempts = kMaxReconnectAttempts;
    cfg->reconnect_interval_ms = kReconnectIntervalMs;

    // 热配置监控
    cfg->hot_config_enabled = kHotConfigEnabled;
    cfg->hot_config_polling_ms = kHotConfigPollingIntervalMs;

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
                        if (strcmp(value, "protocols") == 0) {
                            current_section = Section::Protocols;
                            is_section_key = true;
                        }
                    } else if (current_section == Section::Protocols) {
                        if (strcmp(value, "bacnet") == 0) {
                            current_section = Section::Bacnet;
                            is_section_key = true;
                        }
                    } else if (current_section == Section::Bacnet || 
                               current_section == Section::Discovery || 
                               current_section == Section::LocalDevice || 
                               current_section == Section::Network || 
                               current_section == Section::MSTP || 
                               current_section == Section::Services || 
                               current_section == Section::Connection || 
                               current_section == Section::HotConfig) {
                        // 在 bacnet 层级下，允许在各个子 section 之间切换
                        if (strcmp(value, "discovery") == 0) {
                            current_section = Section::Discovery;
                            is_section_key = true;
                        } else if (strcmp(value, "local_device") == 0) {
                            current_section = Section::LocalDevice;
                            is_section_key = true;
                        } else if (strcmp(value, "network") == 0) {
                            current_section = Section::Network;
                            is_section_key = true;
                        } else if (strcmp(value, "mstp") == 0) {
                            current_section = Section::MSTP;
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
                    if (current_section == Section::Discovery) {
                        if (last_key == "target_device_start") {
                            cfg->target_device_start = std::atoi(value);
                            g_config_metadata.target_device_start = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "target_device_end") {
                            cfg->target_device_end = std::atoi(value);
                            g_config_metadata.target_device_end = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "whois_retry") {
                            cfg->whois_retry = std::atoi(value);
                            g_config_metadata.whois_retry = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "response_timeout_ms") {
                            cfg->response_timeout_ms = std::atoi(value);
                            g_config_metadata.response_timeout_ms = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::LocalDevice) {
                        if (last_key == "instance_id") {
                            cfg->local_instance_id = std::atoi(value);
                            g_config_metadata.instance_id = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "max_apdu") {
                            cfg->local_max_apdu = std::atoi(value);
                            g_config_metadata.max_apdu = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Network) {
                        if (last_key == "interface") {
                            copy_str(cfg->interface_name, sizeof(cfg->interface_name), value);
                            g_config_metadata.interface_name = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "port") {
                            cfg->port = std::atoi(value);
                            g_config_metadata.port = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "broadcast_address") {
                            copy_str(cfg->broadcast_address, sizeof(cfg->broadcast_address), value);
                            g_config_metadata.broadcast_address = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::MSTP) {
                        // MS/TP 串口配置解析
                        if (last_key == "port") {
                            copy_str(cfg->mstp_port, sizeof(cfg->mstp_port), value);
                            g_config_metadata.mstp_port = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "baud_rate") {
                            cfg->mstp_baud = std::atoi(value);
                            g_config_metadata.mstp_baud = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "mac_address") {
                            cfg->mstp_mac = std::atoi(value);
                            g_config_metadata.mstp_mac = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "max_master") {
                            cfg->mstp_max_master = std::atoi(value);
                            g_config_metadata.mstp_max_master = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "max_info_frames") {
                            cfg->mstp_max_frames = std::atoi(value);
                            g_config_metadata.mstp_max_frames = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Services) {
                        if (last_key == "read_timeout_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->read_timeout_ms = (val > 0) ? val : kReadTimeoutMs;
                            g_config_metadata.read_timeout_ms = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "write_timeout_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->write_timeout_ms = (val > 0) ? val : kWriteTimeoutMs;
                            g_config_metadata.write_timeout_ms = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "default_priority") {
                            uint8_t val = std::atoi(value);
                            cfg->default_priority = (val > 0) ? val : kDefaultPriority;
                            g_config_metadata.default_priority = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "cache_expiry_ms") {
                            cfg->cache_expiry_ms = std::atoi(value);
                            g_config_metadata.cache_expiry_ms = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "cache_strategy") {
                            cfg->cache_strategy = std::atoi(value);
                            g_config_metadata.cache_strategy = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "datalink_maintenance_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->datalink_maintenance_ms = (val > 0) ? val : kDatalinkMaintenanceMs;
                            g_config_metadata.datalink_maintenance_ms = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::Connection) {
                        if (last_key == "max_reconnect_attempts") {
                            uint8_t val = std::atoi(value);
                            cfg->max_reconnect_attempts = (val > 0) ? val : kMaxReconnectAttempts;
                            g_config_metadata.max_reconnect_attempts = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "reconnect_interval_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->reconnect_interval_ms = (val > 0) ? val : kReconnectIntervalMs;
                            g_config_metadata.reconnect_interval_ms = bacnet::ConfigSource::Yaml;
                        }
                    } else if (current_section == Section::HotConfig) {
                        if (last_key == "enabled") {
                            cfg->hot_config_enabled = parse_bool(value, true);
                            g_config_metadata.hot_config_enabled = bacnet::ConfigSource::Yaml;
                        } else if (last_key == "polling_interval_ms") {
                            uint32_t val = std::atoi(value);
                            cfg->hot_config_polling_ms = (val > 0) ? val : kHotConfigPollingIntervalMs;
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
                } else if (current_section == Section::Protocols) {
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

/**
 * @brief 更新配置文件（config.yaml）
 * 
 * 策略：
 * 1. 读取现有配置文件的所有行
 * 2. 查找匹配的配置项（通过键名 + section 上下文）
 * 3. 替换对应的值（保留注释和格式）
 * 4. 写回文件
 * 
 * 这种实现方式的优点：
 * - 保留原文件格式（缩进、空行、注释）
 * - 只修改需要改变的值
 * - 不需要完整的 YAML 序列化器
 */
int config_update(const bacnet_config_t *cfg) {
    if (!cfg) {
        log_error("[BACnet][Config] config_update: cfg is NULL");
        return PROTO_ERROR_PARAM;
    }

    const char *config_path = bacnet::defaults::kConfigPath;
    
    // 1. 读取现有配置文件
    FILE *file = fopen(config_path, "r");
    if (!file) {
        log_error("[BACnet][Config] Failed to open config file: {}", config_path);
        return PROTO_ERROR_INIT;
    }

    // 读取所有行到内存
    std::vector<std::string> lines;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), file)) {
        lines.emplace_back(buffer);
    }
    fclose(file);

    // 2. 定义配置更新辅助函数
    enum class CurrentSection {
        None,
        Bacnet,
        Discovery,
        LocalDevice,
        Network,
        Services,
        Connection,
        HotConfig
    };

    auto update_line = [](std::string &line, const char *key, const char *new_value) -> bool {
        // 查找 "key:" 模式（允许前导空格）
        size_t key_len = strlen(key);
        size_t pos = line.find(key);
        if (pos == std::string::npos) return false;
        
        // 确保是完整匹配（后面跟着 ':' 和空格）
        size_t colon_pos = pos + key_len;
        if (colon_pos >= line.length() || line[colon_pos] != ':') return false;
        
        // 查找值的起始位置（跳过冒号和空格）
        size_t value_start = line.find_first_not_of(": \t", colon_pos);
        if (value_start == std::string::npos) return false;
        
        // 查找注释位置（如果有）
        size_t comment_pos = line.find('#', value_start);
        
        // 构造新行：保留缩进 + key + ": " + new_value + 空格 + 注释
        std::string indent = line.substr(0, pos);
        std::string new_line = indent + key + ": " + new_value;
        
        if (comment_pos != std::string::npos) {
            // 添加固定宽度的空格（对齐注释）
            size_t padding = 35;  // 目标列位置
            if (new_line.length() < padding) {
                new_line.append(padding - new_line.length(), ' ');
            } else {
                new_line += "  ";  // 至少两个空格
            }
            new_line += line.substr(comment_pos);
        } else {
            new_line += "\n";
        }
        
        line = new_line;
        return true;
    };

    // 3. 遍历所有行，更新匹配的配置项
    CurrentSection current_section = CurrentSection::None;
    int updates_count = 0;

    for (auto &line : lines) {
        // 检测 section 切换
        if (line.find("bacnet:") != std::string::npos) {
            current_section = CurrentSection::Bacnet;
        } else if (line.find("discovery:") != std::string::npos) {
            current_section = CurrentSection::Discovery;
        } else if (line.find("local_device:") != std::string::npos) {
            current_section = CurrentSection::LocalDevice;
        } else if (line.find("network:") != std::string::npos) {
            current_section = CurrentSection::Network;
        } else if (line.find("services:") != std::string::npos) {
            current_section = CurrentSection::Services;
        } else if (line.find("connection:") != std::string::npos) {
            current_section = CurrentSection::Connection;
        } else if (line.find("hot_config:") != std::string::npos) {
            current_section = CurrentSection::HotConfig;
        }

        // 根据当前 section 更新配置值
        char value_buf[256];  // 扩大缓冲区以容纳长路径（最大 127 字节 + 引号 + null）
        
        switch (current_section) {
            case CurrentSection::Discovery:
                if (cfg->target_device_start > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->target_device_start);
                    if (update_line(line, "target_device_start", value_buf)) updates_count++;
                }
                if (cfg->target_device_end > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->target_device_end);
                    if (update_line(line, "target_device_end", value_buf)) updates_count++;
                }
                if (cfg->whois_retry > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->whois_retry);
                    if (update_line(line, "whois_retry", value_buf)) updates_count++;
                }
                if (cfg->response_timeout_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->response_timeout_ms);
                    if (update_line(line, "response_timeout_ms", value_buf)) updates_count++;
                }
                break;

            case CurrentSection::LocalDevice:
                if (cfg->local_instance_id > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->local_instance_id);
                    if (update_line(line, "instance_id", value_buf)) updates_count++;
                }
                if (cfg->local_max_apdu > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->local_max_apdu);
                    if (update_line(line, "max_apdu", value_buf)) updates_count++;
                }
                break;

            case CurrentSection::Network:
                if (cfg->interface_name[0]) {
                    snprintf(value_buf, sizeof(value_buf), "\"%s\"", cfg->interface_name);
                    if (update_line(line, "interface", value_buf)) updates_count++;
                }
                if (cfg->port > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->port);
                    if (update_line(line, "port", value_buf)) updates_count++;
                }
                if (cfg->broadcast_address[0]) {
                    snprintf(value_buf, sizeof(value_buf), "\"%s\"", cfg->broadcast_address);
                    if (update_line(line, "broadcast_address", value_buf)) updates_count++;
                }
                break;

            case CurrentSection::Services:
                if (cfg->read_timeout_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->read_timeout_ms);
                    if (update_line(line, "read_timeout_ms", value_buf)) updates_count++;
                }
                if (cfg->write_timeout_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->write_timeout_ms);
                    if (update_line(line, "write_timeout_ms", value_buf)) updates_count++;
                }
                if (cfg->default_priority > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->default_priority);
                    if (update_line(line, "default_priority", value_buf)) updates_count++;
                }
                if (cfg->cache_expiry_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->cache_expiry_ms);
                    if (update_line(line, "cache_expiry_ms", value_buf)) updates_count++;
                }
                snprintf(value_buf, sizeof(value_buf), "%u", cfg->cache_strategy);
                if (update_line(line, "cache_strategy", value_buf)) updates_count++;
                
                if (cfg->datalink_maintenance_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->datalink_maintenance_ms);
                    if (update_line(line, "datalink_maintenance_ms", value_buf)) updates_count++;
                }
                break;

            case CurrentSection::Connection:
                if (cfg->max_reconnect_attempts > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->max_reconnect_attempts);
                    if (update_line(line, "max_reconnect_attempts", value_buf)) updates_count++;
                }
                if (cfg->reconnect_interval_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->reconnect_interval_ms);
                    if (update_line(line, "reconnect_interval_ms", value_buf)) updates_count++;
                }
                break;

            case CurrentSection::HotConfig:
                // hot_config.enabled (默认不更新布尔字段)
                
                if (cfg->hot_config_polling_ms > 0) {
                    snprintf(value_buf, sizeof(value_buf), "%u", cfg->hot_config_polling_ms);
                    if (update_line(line, "polling_interval_ms", value_buf)) updates_count++;
                }
                break;

            default:
                break;
        }
    }

    // 4. 原子写入：使用文件锁 + 唯一临时文件名（防止多进程/多线程竞态）
    
    // 4.1 使用锁文件保护写入过程
    std::string lock_path = std::string(config_path) + ".lock";
    FILE *lock_file = fopen(lock_path.c_str(), "w");
    if (!lock_file) {
        log_error("[BACnet][Config] Failed to create lock file: {}", lock_path);
        return PROTO_ERROR_WRITE;
    }
    
    // 使用 flock 实现文件锁（会阻塞等待）
    int lock_fd = fileno(lock_file);
    if (flock(lock_fd, LOCK_EX) != 0) {
        log_error("[BACnet][Config] Failed to acquire file lock");
        fclose(lock_file);
        return PROTO_ERROR_WRITE;
    }
    
    log_debug("[BACnet][Config] File lock acquired, starting atomic write");
    
    // 4.2 生成唯一临时文件名（PID + 时间戳，避免多进程冲突）
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d.%lu", 
             config_path, getpid(), (unsigned long)time(nullptr));
    
    // 4.3 写入临时文件
    file = fopen(temp_path, "w");
    if (!file) {
        log_error("[BACnet][Config] Failed to open temp config file for writing: {}", temp_path);
        flock(lock_fd, LOCK_UN);
        fclose(lock_file);
        remove(lock_path.c_str());
        return PROTO_ERROR_WRITE;
    }

    for (const auto &line : lines) {
        fputs(line.c_str(), file);
    }
    
    // 4.4 强制刷新到磁盘
    fflush(file);
    fsync(fileno(file));  // 确保数据写入磁盘
    fclose(file);
    
    // 4.5 原子替换：rename 是原子操作，不会出现半成品文件
    if (rename(temp_path, config_path) != 0) {
        log_error("[BACnet][Config] Failed to replace config file");
        remove(temp_path);
        flock(lock_fd, LOCK_UN);
        fclose(lock_file);
        remove(lock_path.c_str());
        return PROTO_ERROR_WRITE;
    }
    
    // 4.6 释放锁并清理
    flock(lock_fd, LOCK_UN);
    fclose(lock_file);
    remove(lock_path.c_str());  // 删除锁文件
    
    log_debug("[BACnet][Config] Atomic write completed, lock released");

    log_info("[BACnet][Config] Configuration updated successfully ({} items changed)", updates_count);
    log_info("[BACnet][Config] File: {}", config_path);
    
    // 如果启用了热配置，修改会自动生效
    if (cfg->hot_config_enabled) {
        log_info("[BACnet][Config] Hot config is enabled, changes will take effect within {} ms", 
                 cfg->hot_config_polling_ms);
    } else {
        log_warn("[BACnet][Config] Hot config is disabled, please restart the application for changes to take effect");
    }

    return PROTO_SUCCESS;
}

} // extern "C"

} // namespace bacnet
