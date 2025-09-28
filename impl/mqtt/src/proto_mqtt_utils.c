#include "proto_mqtt.h"

// 用于保活机制和连接质量监控的时间计算
int get_current_time_ms(void) { 
    return (int)(time(NULL) * 1000);
}

/**
 * @brief 创建连接选项的辅助函数
 * 功能：
 * 1. 初始化MQTT连接选项
 * 2. 设置保活间隔和超时时间
 * 3. 配置异步回调函数
 * 4. 设置用户名和密码（如果提供）
 * 
 * 用途：避免在多个地方重复相同的连接选项配置代码
 */
void create_connect_options(MQTTAsync_connectOptions* conn_opts, mqtt_config_t* cfg) {
    // 使用标准初始化器初始化连接选项
    MQTTAsync_connectOptions temp = MQTTAsync_connectOptions_initializer;
    *conn_opts = temp;
    
    // 确保结构体版本字段被正确设置
    conn_opts->struct_id[0] = 'M';
    conn_opts->struct_id[1] = 'Q';
    conn_opts->struct_id[2] = 'T';
    conn_opts->struct_id[3] = 'C';
    conn_opts->struct_version = 0;
    
    // 设置连接参数
    conn_opts->keepAliveInterval = cfg->keepalive_interval > 0 ? cfg->keepalive_interval : 60;  // 使用配置的保活间隔，默认60秒
    conn_opts->cleansession = 1;                         // 清理会话
    conn_opts->connectTimeout = cfg->timeout_ms / 1000;  // 连接超时（秒）
    conn_opts->retryInterval = 0;                        // 重试间隔
    conn_opts->maxInflight = 20;                         // 最大未确认消息数
    
    // 设置异步回调函数
    conn_opts->onSuccess = onConnectSuccess;   // 连接成功回调
    conn_opts->onFailure = onConnectFailure;   // 连接失败回调
    conn_opts->context = NULL;                 // 上下文指针（将在调用处设置）
    
    // 设置认证信息（如果提供）
    if (cfg->username && strlen(cfg->username) > 0) {
        conn_opts->username = cfg->username;
        conn_opts->password = cfg->password;
    }
    
    // 设置SSL选项（如果需要）
    conn_opts->ssl = NULL;                     // SSL选项
    conn_opts->serverURIcount = 0;             // 服务器URI数量
    conn_opts->serverURIs = NULL;              // 服务器URI列表
}

/**
 * @brief 创建断开选项的辅助函数
 * 功能：
 * 1. 初始化MQTT断开选项
 * 2. 设置超时时间
 * 3. 配置异步回调函数
 * 
 * 用途：避免在多个地方重复相同的断开选项配置代码
 */
void create_disconnect_options(MQTTAsync_disconnectOptions* disc_opts, int timeout_ms, mqtt_ctx_t* ctx) {
    // 使用标准初始化器初始化断开选项
    MQTTAsync_disconnectOptions temp = MQTTAsync_disconnectOptions_initializer;
    *disc_opts = temp;
    
    // 确保结构体版本字段被正确设置
    disc_opts->struct_id[0] = 'M';
    disc_opts->struct_id[1] = 'Q';
    disc_opts->struct_id[2] = 'T';
    disc_opts->struct_id[3] = 'D';
    disc_opts->struct_version = 0;
    
    // 设置断开参数
    disc_opts->timeout = timeout_ms;           // 断开超时时间
    
    // 设置异步回调函数
    disc_opts->onSuccess = onDisconnectSuccess; // 断开成功回调
    disc_opts->onFailure = onDisconnectFailure; // 断开失败回调
    disc_opts->context = ctx;                  // 上下文指针
}

/**
 * @brief 设置连接状态（线程安全）
 * @param ctx MQTT上下文指针
 * @param status 新的连接状态
 */
 void set_connect_status(mqtt_ctx_t* ctx, connect_status_t status) {
    if (!ctx) return;
    pthread_mutex_lock(&ctx->state_mutex);
    ctx->connect_status = status;
    pthread_cond_signal(&ctx->conn_cond);
    pthread_mutex_unlock(&ctx->state_mutex);
}

/**
 * @brief 获取连接状态（线程安全）
 * @param ctx MQTT上下文指针
 * @return 当前连接状态
 */
connect_status_t get_connect_status(mqtt_ctx_t* ctx) {
    if (!ctx) return CON_IDLE;
    pthread_mutex_lock(&ctx->state_mutex);
    connect_status_t status = ctx->connect_status;
    pthread_mutex_unlock(&ctx->state_mutex);
    return status;
}

/**
 * @brief 设置操作状态（线程安全）
 * @param ctx MQTT上下文指针
 * @param status 新的操作状态
 */
void set_operation_status(mqtt_ctx_t* ctx, operation_status_t status) {
    if (!ctx) return;
    pthread_mutex_lock(&ctx->state_mutex);
    ctx->operation_status = status;
    pthread_mutex_unlock(&ctx->state_mutex);
}

/**
 * @brief 获取操作状态（线程安全）
 * @param ctx MQTT上下文指针
 * @return 当前操作状态
 */
operation_status_t get_operation_status(mqtt_ctx_t* ctx) {
    if (!ctx) return OP_IDLE;
    pthread_mutex_lock(&ctx->state_mutex);
    operation_status_t status = ctx->operation_status;
    pthread_mutex_unlock(&ctx->state_mutex);
    return status;
}
