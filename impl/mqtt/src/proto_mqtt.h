#ifndef PROTO_MQTT_H
#define PROTO_MQTT_H

#include "common/api/proto_common.h" 
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <MQTTAsync.h>

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_MESSAGE_SIZE 1024
#define MAX_MSG_BUFFER_SIZE 100
// 移除固定保活间隔，使用配置参数
#define DEFAULT_TIMEOUT_MS 5000
#define HEARTBEAT_PREFIX "HEARTBEAT_"

// 连接状态定义（状态机）
typedef enum {
    CON_OK = 0,        // 连接成功
    CON_IDLE = 1,      // 未连接
    CON_PENDING = 2,   // 连接中
    DCON_OK = 3,       // 已断开
    DCON_PENDING = 4   // 断开中
} connect_status_t;

// 操作状态定义（用于循环调用判断）
typedef enum {
    OP_IDLE = 0,       // 空闲状态
    OP_PENDING = 1,    // 操作进行中
    OP_SUCCESS = 2,    // 操作成功
    OP_FAILED = 3      // 操作失败
} operation_status_t;

// 错误码定义
typedef enum {
    CON_PARA_ERROR = -1,  // 参数错误
    CON_TIMEOUT = -2,     // 连接超时
    CON_REFUSED = -3,     // 拒绝连接
    CLI_ERROR = -4,       // 内部错误
    DCON_TIMEOUT = -5,    // 断开超时
    DCON_FAILED = -6,     // 断开失败
    PUB_FAILED = -7,      // 发布失败
    SUB_FAILED = -8,      // 订阅失败
    NO_DATA = -9          // 无数据可读
} error_code_t;

// MQTT配置结构体
typedef struct {
    char broker[128];
    char client_id[64];
    char username[64];
    char password[64];
    char pub_topic[64];
    char sub_topic[64];
    int qos;
    int retained;                 // 发布消息是否保留（0: 不保留，1: 保留）
    int timeout_ms;
    int keepalive_interval;    // 保活间隔（秒）
    int reconnect_interval;    // 重连间隔（秒）
    int max_reconnect_attempts; // 最大重连次数（0表示无限重连）
    int enable_auto_reconnect;  // 是否启用自动重连
} mqtt_config_t;

// 消息结构体
typedef struct {
    char topic[128];
    char payload[MAX_MESSAGE_SIZE];
    int payloadlen;                    // 实际载荷长度（支持二进制数据）
    long long timestamp_ms;
} MQTT_Message;

// 异步回调函数类型定义
typedef void (*proto_async_callback_t)(proto_ctx_t *ctx, int result, void *userdata);

// MQTT扩展上下文结构（包含消息缓冲区和重连机制）
typedef struct {
    proto_ctx_t base;                    // 基础上下文
    MQTT_Message msg_buffer[MAX_MSG_BUFFER_SIZE];  // 消息缓冲区
    int msg_buffer_head;                 // 缓冲区头指针
    int msg_buffer_tail;                 // 缓冲区尾指针
    int msg_buffer_count;                // 缓冲区消息数量
    pthread_mutex_t msg_mutex;           // 消息缓冲区互斥锁
    pthread_cond_t msg_cond;             // 消息缓冲区条件变量
    
    // 实例状态管理
    MQTTAsync client;                    // MQTT客户端实例
    connect_status_t connect_status;     // 连接状态
    operation_status_t operation_status; // 操作状态
    int timeout_ms;                      // 超时时间
    pthread_mutex_t state_mutex;         // 状态互斥锁
    pthread_cond_t conn_cond;            // 连接状态条件变量
    
    // 重连机制
    int reconnect_attempts;              // 当前重连尝试次数
    int connection_lost_time;            // 连接丢失时间（毫秒），用于延迟重连
    
    // 异步回调支持
    proto_async_callback_t connect_callback;    // 连接回调函数
    proto_async_callback_t disconnect_callback; // 断开回调函数
    proto_async_callback_t send_callback;       // 发送回调函数
    void* connect_userdata;              // 连接用户数据
    void* disconnect_userdata;           // 断开用户数据
    void* send_userdata;                 // 发送用户数据
} mqtt_ctx_t;


// 对外是同步接口，内部使用状态机处理异步逻辑
// 调用方式：循环调用，根据返回值判断操作状态
// 返回值：PROTO_SUCCESS(成功) 或 错误码

// 初始化MQTT客户端
int proto_driver_init(proto_ctx_t *ctx);

// 释放MQTT客户端资源  
void proto_driver_release(proto_ctx_t *ctx);

// 连接到MQTT服务器
int proto_connect(proto_ctx_t *ctx);

// 断开MQTT连接
void proto_disconnect(proto_ctx_t *ctx);

// 写入MQTT消息
int proto_write(proto_ctx_t *ctx, proto_request_t *req);

// 读取MQTT消息
int proto_read(proto_ctx_t *ctx, proto_request_t *req);


// ============================================================================
// 异步回调函数声明
// ============================================================================
void onConnectSuccess(void* context, MQTTAsync_successData* response);  // 连接成功回调
void onConnectFailure(void* context, MQTTAsync_failureData* response);   // 连接失败回调
void onDisconnectSuccess(void* context, MQTTAsync_successData* response); // 断开成功回调
void onDisconnectFailure(void* context, MQTTAsync_failureData* response); // 断开失败回调
void onSendSuccess(void* context, MQTTAsync_successData* response);      // 发送成功回调
void onSendFailure(void* context, MQTTAsync_failureData* response);      // 发送失败回调
void onSubscribeSuccess(void* context, MQTTAsync_successData* response); // 订阅成功回调
void onSubscribeFailure(void* context, MQTTAsync_failureData* response); // 订阅失败回调
void onConnectionLost(void* context, char* cause);                        // 连接丢失回调
int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message); // 消息到达回调

// ============================================================================
// 线程安全的状态管理函数声明
// ============================================================================
void set_connect_status(mqtt_ctx_t* ctx, connect_status_t status);      // 设置连接状态
connect_status_t get_connect_status(mqtt_ctx_t* ctx);                   // 获取连接状态
void set_operation_status(mqtt_ctx_t* ctx, operation_status_t status);  // 设置操作状态
operation_status_t get_operation_status(mqtt_ctx_t* ctx);               // 获取操作状态
int wait_for_connection(mqtt_ctx_t* ctx, int timeout_ms);               // 等待连接完成

// ============================================================================
// 工具函数声明
// ============================================================================
// 保活监控线程已移除，使用MQTT协议原生保活机制
int get_current_time_ms(void);                                          // 获取当前时间（毫秒）
void create_connect_options(MQTTAsync_connectOptions* conn_opts, mqtt_config_t* cfg); // 创建连接选项
void create_disconnect_options(MQTTAsync_disconnectOptions* disc_opts, int timeout_ms, mqtt_ctx_t* ctx); // 创建断开选项

// 配置加载
int load_mqtt_config_from_yaml(const char* yaml_path, mqtt_config_t* cfg);

// =========================================================================
// 数据格式化（对外）
// =========================================================================
// 简单类型枚举（与用户侧保持一致: INT32/FLOAT，可扩展）
typedef enum {
    ENUM_INT32 = 0,
    ENUM_FLOAT = 1,
    ENUM_STRING = 2,
    ENUM_BOOL = 3
} TypeData;

// 将一对 key/value 以 JSON 形式追加到 dst。
// - 成功返回 PROTO_SUCCESS；长度不足返回 PROTO_ERROR_WRITE；参数错误返回 PROTO_ERROR_PARAM
int mqtt_data_format(const char* key, const void* data_ptr, TypeData type, char* dst);


#ifdef __cplusplus
}
#endif

#endif // PROTO_MQTT_H