#pragma once
#include "common/api/proto_common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 常量定义                                                                   */
/* -------------------------------------------------------------------------- */

#define BACNET_MAX_ENV_LEN            16
#define BACNET_MAX_LOG_LEVEL_LEN      16
#define BACNET_MAX_LOG_PATH_LEN       128
#define BACNET_MAX_INTERFACE_LEN      32
#define BACNET_MAX_ADDRESS_LEN        48

/* -------------------------------------------------------------------------- */
/* 配置数据结构                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    char environment[BACNET_MAX_ENV_LEN];
    char log_level[BACNET_MAX_LOG_LEVEL_LEN];
    char log_file[BACNET_MAX_LOG_PATH_LEN];
} bacnet_common_settings_t;

typedef struct {
    uint32_t target_device_start;       /* 目标设备实例范围起始 (含) */
    uint32_t target_device_end;         /* 目标设备实例范围结束 (含) */
    uint8_t  whois_retry;               /* Who-Is 重试次数 */
    uint32_t response_timeout_ms;       /* 等待 I-Am 回应的超时 */
} bacnet_discovery_config_t;

typedef struct {
    uint32_t instance_id;               /* 本地设备实例 ID */
    uint16_t max_apdu;                  /* 本地支持的最大 APDU 长度 */
} bacnet_local_device_config_t;

typedef struct {
    char     interface_name[BACNET_MAX_INTERFACE_LEN]; /* 指定网络接口 */
    uint16_t port;                                     /* UDP 端口 (默认 47808) */
    char     broadcast_address[BACNET_MAX_ADDRESS_LEN];/* 广播地址 */
} bacnet_network_config_t;

typedef struct {
    uint32_t read_timeout_ms;           /* ReadProperty 操作超时 */
    uint32_t write_timeout_ms;          /* WriteProperty 操作超时 */
    uint8_t  default_priority;          /* WriteProperty 默认优先级 (0 表示未指定) */
    uint32_t cache_expiry_ms;           /* 读缓存过期时间 (毫秒，默认 1000) */
    uint8_t  cache_strategy;            /* 缓存策略: 0=激进(每次都发), 1=保守(用缓存) */
    uint32_t datalink_maintenance_ms;   /* DataLink维护定时器间隔 (毫秒，默认 1000) */
} bacnet_service_config_t;

typedef struct {
    uint8_t  max_reconnect_attempts;    /* 最大重连次数 (默认 5) */
    uint32_t reconnect_interval_ms;     /* 重连间隔 (毫秒，默认 3000) */
} bacnet_connection_config_t;

typedef struct {
    bool     enabled;                   /* 是否启用热配置监控 (默认 true) */
    uint32_t polling_interval_ms;       /* 配置文件轮询间隔 (毫秒，默认 1000) */
} bacnet_hot_config_t;

typedef struct {
    bool                         enabled;      /* 是否启用 BACnet 协议栈 */
    bacnet_discovery_config_t    discovery;    /* 设备发现配置 */
    bacnet_local_device_config_t local_device; /* 本地设备参数 */
    bacnet_network_config_t      network;      /* 网络层配置 */
    bacnet_service_config_t      services;     /* 服务行为配置 */
    bacnet_connection_config_t   connection;   /* 连接管理配置 */
    bacnet_hot_config_t          hot_config;   /* 热配置监控配置 */
} bacnet_protocol_config_t;

typedef struct {
    bacnet_common_settings_t common;  /* 通用配置 */
    bacnet_protocol_config_t bacnet;  /* BACnet 协议配置 */
} bacnet_config_t;

/* -------------------------------------------------------------------------- */
/* 事件定义                                                                   */
/* -------------------------------------------------------------------------- */

typedef enum {
    BACNET_DATA_NULL = 0,
    BACNET_DATA_BOOLEAN,
    BACNET_DATA_UNSIGNED,
    BACNET_DATA_SIGNED,
    BACNET_DATA_REAL,
    BACNET_DATA_DOUBLE,
    BACNET_DATA_ENUM,
    BACNET_DATA_OCTET_STRING,
    BACNET_DATA_CHARACTER_STRING
} bacnet_data_type_t;

typedef struct {
    bacnet_data_type_t type;
    union {
        bool        boolean_value;
        uint32_t    unsigned_value;
        int32_t     signed_value;
        float       real_value;
        double      double_value;
        uint32_t    enum_value;
        struct {
            uint8_t *data;
            size_t   length;
        } octet_string;
        struct {
            char   *data;
            size_t length;
        } character_string;
    } value;
} bacnet_data_value_t;

typedef struct {
    uint32_t            device_instance;   /* 目标设备实例 (必填) */
    uint16_t            object_type;       /* 对象类型 (必填) */
    uint32_t            object_instance;   /* 对象实例 (必填) */
    uint32_t            property_id;       /* 属性 ID (必填) */
    bacnet_data_value_t *value;            /* 输出值缓冲区 (必填) */
    
    /* 以下为可选字段，不填写则使用默认值 */
    int32_t             array_index;       /* 属性数组索引，-1 表示整个数组 (默认: -1) */
    uint32_t            timeout_ms;        /* 操作超时，0表示使用配置文件默认值 (默认: 0=使用配置) */
    bool                check_only;        /* 是否仅检查队列而不发送新请求 (默认: false) */
    uint8_t             invoke_id;         /* 输出：BACnet调用ID，用于匹配响应 */
} bacnet_read_t;

/* 便捷初始化宏：只需填写四元组 + value缓冲区 */
#define BACNET_READ_INIT(dev, obj_type, obj_inst, prop, val_ptr) \
    { \
        .device_instance = (dev), \
        .object_type = (obj_type), \
        .object_instance = (obj_inst), \
        .property_id = (prop), \
        .value = (val_ptr), \
        .array_index = -1, \
        .timeout_ms = 0, \
        .check_only = false, \
        .invoke_id = 0 \
    }

typedef struct {
    uint32_t           device_instance;    /* 目标设备实例 (必填) */
    uint16_t           object_type;        /* 对象类型 (必填) */
    uint32_t           object_instance;    /* 对象实例 (必填) */
    uint32_t           property_id;        /* 属性 ID (必填) */
    bacnet_data_value_t value;             /* 写入值 (必填) */
    
    /* 以下为可选字段，不填写则使用默认值 */
    int32_t            array_index;        /* 属性数组索引，-1 表示整个数组 (默认: -1) */
    uint8_t            priority;           /* 写入优先级，0 表示使用配置默认值 (默认: 0=使用配置) */
    uint32_t           timeout_ms;         /* 操作超时，0表示使用配置文件默认值 (默认: 0=使用配置) */
    uint8_t            invoke_id;          /* 输出：BACnet调用ID，用于匹配响应 */
} bacnet_write_t;

/* 便捷初始化宏：只需填写四元组 + value */
#define BACNET_WRITE_INIT(dev, obj_type, obj_inst, prop, val) \
    { \
        .device_instance = (dev), \
        .object_type = (obj_type), \
        .object_instance = (obj_inst), \
        .property_id = (prop), \
        .value = (val), \
        .array_index = -1, \
        .priority = 0, \
        .timeout_ms = 0, \
        .invoke_id = 0 \
    }

/* -------------------------------------------------------------------------- */
/* 状态机定义                                                                 */
/* -------------------------------------------------------------------------- */

typedef enum {
    BACNET_CONN_IDLE = 0,        /* 尚未建立连接 */
    BACNET_CONN_CONNECTING,      /* 正在建立连接 */
    BACNET_CONN_CONNECTED,       /* 已成功连接 */
    BACNET_CONN_DISCONNECTING,   /* 正在断开连接 */
    BACNET_CONN_DISCONNECTED     /* 已断开 */
} bacnet_connection_state_t;

typedef enum {
    BACNET_OP_IDLE = 0,          /* 空闲，无活动操作 */
    BACNET_OP_PENDING,            /* 有操作正在进行 */
    BACNET_OP_SUCCESS,            /* 最后一次操作成功 */
    BACNET_OP_FAILED              /* 最后一次操作失败 */
} bacnet_operation_state_t;

/* -------------------------------------------------------------------------- */
/* C 接口函数声明                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief 从 YAML 配置文件加载 BACnet 设置
 * @param yaml_path 配置文件路径
 * @param cfg 输出配置结构体指针
 * @return 0 成功，其余为错误码
 */
int bacnet_load_config_from_yaml(const char *yaml_path, bacnet_config_t *cfg);

/* -------------------------------------------------------------------------- */
/* 对外暴露的 C 接口（PLC 层使用）- 异步非阻塞                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief PLC 读取接口（异步非阻塞）
 * @param req 读请求指针 (bacnet_read_t*)
 * @return PROTO_SUCCESS 请求已提交，其他值为错误码
 * 
 * 功能：
 * 1. 自动检查连接状态（未连接则自动连接）
 * 2. 提交异步读取请求
 * 3. 立即返回（不等待结果）
 * 4. 结果通过队列异步返回
 * 
 * 使用方式：
 *   int ret = plc_proto_read(&req);
 *   if (ret == PROTO_SUCCESS) {
 *       // 请求已提交，数据将通过队列返回
 *   }
 */
int plc_proto_read(void *req);

/**
 * @brief PLC 写入接口（异步非阻塞）
 * @param req 写请求指针 (bacnet_write_t*)
 * @return PROTO_SUCCESS 请求已提交，其他值为错误码
 * 
 * 功能：
 * 1. 自动检查连接状态（未连接则自动连接）
 * 2. 提交异步写入请求
 * 3. 立即返回（不等待结果）
 * 4. 结果通过队列异步反馈
 * 
 * 使用方式：
 *   int ret = plc_proto_write(&req);
 *   if (ret == PROTO_SUCCESS) {
 *       // 请求已提交，写入将异步完成
 *   }
 */
int plc_proto_write(void *req);

/* -------------------------------------------------------------------------- */
/* 配置管理接口                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief 触发配置重载（通过信号或自动监控调用）
 * @return PROTO_SUCCESS 成功，其他值为错误码
 * 
 * 说明：
 * 1. 由外部信号处理函数调用（如 SIGUSR1）
 * 2. 由文件监控线程自动调用（检测到配置文件变化时）
 * 3. 释放当前驱动并清空状态
 * 4. 下次调用 plc_proto_read/write 时自动重新加载配置
 * 
 * 注意：
 * - 配置文件监控由 BACnet 驱动自动管理
 * - 用户无需手动启动/停止监控
 * - 只需在特殊场景下手动调用此函数
 */
int bacnet_reload_config(void);

/* -------------------------------------------------------------------------- */
/* 工具函数                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 释放 bacnet_data_value_t 中动态分配的内存
 * @param value 要释放内存的 bacnet_data_value_t 结构体指针
 */
void bacnet_data_value_free(bacnet_data_value_t *value);

/**
 * @brief 将错误码转换为可读字符串
 * @param status 错误码
 * @return 错误描述字符串
 * 
 * 示例：
 *   int ret = plc_proto_read(&req);
 *   if (ret != PROTO_SUCCESS) {
 *       printf("Error: %s\n", proto_status_to_string(ret));
 *   }
 */
const char* proto_status_to_string(proto_status_t status);

#ifdef __cplusplus
}
#endif
