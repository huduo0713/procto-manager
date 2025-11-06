#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {    
    PROTO_TYPE_MODBUS,    
    PROTO_TYPE_OPCUA,    
    PROTO_TYPE_BACNET,    
    PROTO_TYPE_MQTT,    
    // 可扩展更多协议
} proto_type_t;
typedef enum {    
    PROTO_SUCCESS = 0,    
    PROTO_ERROR_INIT = -1,    
    PROTO_ERROR_CONNECT = -2,    
    PROTO_ERROR_READ = -3,    
    PROTO_ERROR_WRITE = -4,    
    PROTO_ERROR_UNSUPPORTED = -5,    
    PROTO_ERROR_PARAM = -6,
    PROTO_ERROR_MEMORY = -8,
    PROTO_NO_DATA = -7,
    PROTO_TIMEOUT = -9,
} proto_status_t;
typedef struct {    
    char resource_name[64];    
    uint16_t address;    
    uint16_t quantity;    
    void *value;              // 读写数据缓冲区
} proto_request_t;
typedef struct {    
    proto_type_t type;    
    void *client;             // 协议客户端上下文    
    void *config;             // 协议配置（结构体或JSON）    
    void *userdata;           // 用户自定义数据
} proto_ctx_t;
#ifdef __cplusplus
}
#endif