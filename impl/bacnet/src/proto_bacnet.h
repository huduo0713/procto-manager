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

/**
 * @brief BACnet 配置结构体（扁平化设计）
 * 
 * 说明：
 * - 所有配置项在同一层级，避免嵌套访问
 * - 用于 config_update() API 时，数值字段为 0 表示不更新
 * - 布尔字段为 -1 表示不更新，0=false, 1=true
 * - 配置优先级：用户传入 > config.yaml > 代码默认值
 */
typedef struct {
    /* ==================== BACnet 协议栈开关 ==================== */
    int8_t   enabled;                       /* BACnet协议栈是否启用 (-1=不更新, 0=false, 1=true) */
    
    /* ==================== 设备发现配置 ==================== */
    uint32_t target_device_start;           /* 目标设备实例范围起始 (含，默认: 5678) */
    uint32_t target_device_end;             /* 目标设备实例范围结束 (含，默认: 5678) */
    uint8_t  whois_retry;                   /* Who-Is 重试次数 (默认: 3) */
    uint32_t response_timeout_ms;           /* I-Am 响应超时 (毫秒，默认: 5000) */
    
    /* ==================== 本地设备参数 ==================== */
    uint32_t local_instance_id;             /* 本地设备实例 ID (默认: 4194303) */
    uint16_t local_max_apdu;                /* 本地最大 APDU 长度 (默认: 1476) */
    
    /* ==================== 网络层配置 ==================== */
    char     interface_name[BACNET_MAX_INTERFACE_LEN];  /* 网络接口名称 (默认: ""=自动) */
    uint16_t port;                          /* UDP 端口 (默认: 47808) */
    char     broadcast_address[BACNET_MAX_ADDRESS_LEN]; /* 广播地址 (默认: "255.255.255.255") */
    
    /* ==================== 服务行为配置 ==================== */
    uint32_t read_timeout_ms;               /* ReadProperty 超时 (毫秒，默认: 6000) */
    uint32_t write_timeout_ms;              /* WriteProperty 超时 (毫秒，默认: 6000) */
    uint8_t  default_priority;              /* 写入默认优先级 (默认: 8) */
    uint32_t cache_expiry_ms;               /* 读缓存过期时间 (毫秒，默认: 1000) */
    uint8_t  cache_strategy;                /* 缓存策略 (0=激进每次发送, 1=保守用缓存，默认: 0) */
    uint32_t datalink_maintenance_ms;       /* DataLink 维护间隔 (毫秒，默认: 1000) */
    
    /* ==================== 连接管理配置 ==================== */
    uint8_t  max_reconnect_attempts;        /* 最大重连次数 (默认: 5) */
    uint32_t reconnect_interval_ms;         /* 重连间隔 (毫秒，默认: 3000) */
    
    /* ==================== 热配置监控 ==================== */
    int8_t   hot_config_enabled;            /* 是否启用热配置监控 (-1=不更新, 0=false, 1=true，默认: true) */
    uint32_t hot_config_polling_ms;         /* 配置文件轮询间隔 (毫秒，默认: 1000) */
} bacnet_config_t;

/* -------------------------------------------------------------------------- */
/* 配置初始化宏（用于 config_update）                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化配置结构体（用于配置更新）
 * 
 * 使用方法：
 *   bacnet_config_t cfg = BACNET_CONFIG_INIT;
 *   cfg.read_timeout_ms = 8000;  // 只修改这个字段
 *   config_update(&cfg);
 * 
 * 说明：
 * - 数值字段初始化为 0（表示不修改）
 * - 布尔字段初始化为 -1（表示不修改）
 * - 字符串字段初始化为空（表示不修改）
 * - 用户可以直接传递 true/false 给布尔字段，会自动转换为 1/0
 * 
 * 示例：
 *   bacnet_config_t cfg = BACNET_CONFIG_INIT;
 *   cfg.enabled = true;              // 转换为 1，更新为 true
 *   cfg.hot_config_enabled = false;  // 转换为 0，更新为 false
 *   cfg.read_timeout_ms = 8000;      // 更新超时
 *   // 不设置的字段保持初始值，不会被更新
 */
#define BACNET_CONFIG_INIT { \
    -1, 0, 0, 0, 0, \
    0, 0, {0}, 0, {0}, \
    0, 0, 0, 0, 0, \
    0, 0, 0, -1, 0 \
}

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
/* 对外暴露的 C 接口（PLC 层使用）                                            */
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

/* -------------------------------------------------------------------------- */
/* 配置管理 API                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief 更新配置文件（config.yaml）
 * @param cfg 要更新的配置数据（传入需要修改的字段）
 * @return PROTO_SUCCESS(0) 表示成功，其他值表示失败
 * 
 * 功能说明：
 * - 直接修改 config.yaml 文件
 * - 如果启用了热配置监控，修改会在 1 秒内自动生效
 * - 如果未启用热配置，需要重启程序生效
 * 
 * 注意事项：
 * - 传入的配置结构体只需要填写需要修改的字段
 * - 数值字段为 0 时不更新，字符串为空时不更新
 * - 布尔字段为 -1 时不更新，0=false, 1=true (支持隐式转换)
 * - 配置文件格式会自动保留（注释、缩进等）
 * 
 * 示例 1: 只更新数值字段
 *   bacnet_config_t cfg = BACNET_CONFIG_INIT;
 *   cfg.read_timeout_ms = 8000;   // 修改读超时
 *   cfg.write_timeout_ms = 8000;  // 修改写超时
 *   int ret = config_update(&cfg);
 * 
 * 示例 2: 更新布尔字段
 *   bacnet_config_t cfg = BACNET_CONFIG_INIT;
 *   cfg.enabled = true;               // 隐式转换为 1，更新为 true
 *   cfg.hot_config_enabled = false;   // 隐式转换为 0，更新为 false
 *   int ret = config_update(&cfg);
 * 
 * 示例 3: 混合更新
 *   bacnet_config_t cfg = BACNET_CONFIG_INIT;
 *   cfg.read_timeout_ms = 5000;
 *   cfg.cache_strategy = 1;           // 保守策略
 *   cfg.hot_config_enabled = true;    // 会更新
 *   // cfg.enabled 保持为 -1，不会更新
 *   int ret = config_update(&cfg);
 */
int config_update(const bacnet_config_t *cfg);

#ifdef __cplusplus
}
#endif
