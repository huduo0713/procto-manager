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
    conn_opts->keepAliveInterval = KEEP_ALIVE_INTERVAL;  // 保活间隔
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
void* keepalive_monitor_thread(void* arg) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)arg;
    if (!mqtt_ctx || !mqtt_ctx->base.config) {
        printf("[KeepAlive] Invalid context, thread exiting\n");
        return NULL;
    }
    
    mqtt_config_t* cfg = (mqtt_config_t*)mqtt_ctx->base.config;
    printf("[KeepAlive] Monitor thread started\n");
    
    // 主循环：每秒执行一次保活检查
    while (mqtt_ctx->keepalive_running) {
        pthread_mutex_lock(&mqtt_ctx->keepalive_mutex);
        
        int current_time = get_current_time_ms();
        
        if (get_connect_status(mqtt_ctx) == CON_OK) {
            // 连接正常，执行保活操作
            mqtt_ctx->last_heartbeat_time = current_time;
            
            // 发送心跳消息（如果配置了保活间隔）
            if (cfg->keepalive_interval > 0) {
                char heartbeat_msg[64];
                snprintf(heartbeat_msg, sizeof(heartbeat_msg), "%s%d", HEARTBEAT_PREFIX, current_time);
                
                MQTTAsync_message msg = MQTTAsync_message_initializer;
                msg.payload = heartbeat_msg;
                msg.payloadlen = strlen(heartbeat_msg);
                msg.qos = 0;        // 心跳消息使用QoS 0，确保快速传输
                msg.retained = 0;   // 不保留心跳消息
                
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                if (MQTTAsync_sendMessage(mqtt_ctx->client, cfg->pub_topic, &msg, &opts) == MQTTASYNC_SUCCESS) {
                    mqtt_ctx->heartbeat_sent_count++;
                }
            }
            
            // 计算连接质量（心跳确认率）
            if (mqtt_ctx->heartbeat_sent_count > 0) {
                mqtt_ctx->connection_quality = (mqtt_ctx->heartbeat_ack_count * 100) / mqtt_ctx->heartbeat_sent_count;
                
                // 每5次心跳显示统计信息
                if (mqtt_ctx->heartbeat_sent_count % 5 == 0) {
                    printf("[KeepAlive] Stats: Sent=%d, ACK=%d, Quality=%d%%\n", 
                           mqtt_ctx->heartbeat_sent_count, 
                           mqtt_ctx->heartbeat_ack_count, 
                           mqtt_ctx->connection_quality);
                }
                
                // 连接质量警告
                if (mqtt_ctx->connection_quality < 50) {
                    printf("[KeepAlive] WARNING: Connection quality is low (%d%%)\n", mqtt_ctx->connection_quality);
                }
            }
        } else if (get_connect_status(mqtt_ctx) == DCON_OK && mqtt_ctx->connection_lost_time == 0) {
            // 检测到连接丢失
            mqtt_ctx->connection_lost_time = current_time;
            printf("[KeepAlive] Connection lost detected at %d\n", current_time);
        }
        
        pthread_mutex_unlock(&mqtt_ctx->keepalive_mutex);
        sleep(1);  // 每秒检查一次
    }
    
    printf("[KeepAlive] Monitor thread stopped\n");
    return NULL;
}

/**
 * @brief 自动重连线程函数
 * 功能：
 * 1. 监控连接丢失事件
 * 2. 按配置间隔自动尝试重连
 * 3. 限制最大重连次数
 * 4. 处理重连失败和成功情况
 * 5. 线程安全的重连状态管理
 * 
 * 重连策略：
 * - 检测到连接丢失后等待reconnect_interval秒
 * - 最多尝试max_reconnect_attempts次
 * - 每次重连失败后增加重连计数
 * - 重连成功后重置计数
 */
void* auto_reconnect_thread(void* arg) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)arg;
    if (!mqtt_ctx || !mqtt_ctx->base.config) {
        printf("[AutoReconnect] Invalid context, thread exiting\n");
        return NULL;
    }
    
    mqtt_config_t* cfg = (mqtt_config_t*)mqtt_ctx->base.config;
    printf("[AutoReconnect] Reconnect thread started\n");
    
    // 主循环：每2秒检查一次重连条件
    while (mqtt_ctx->reconnect_running) {
        pthread_mutex_lock(&mqtt_ctx->keepalive_mutex);
        
        // 检查是否需要重连的条件：
        // 1. 连接状态为断开
        // 2. 已记录连接丢失时间
        // 3. 启用了自动重连
        // 4. 未超过最大重连次数
        if (get_connect_status(mqtt_ctx) == DCON_OK && 
            mqtt_ctx->connection_lost_time > 0 &&
            cfg->enable_auto_reconnect &&
            mqtt_ctx->reconnect_attempts < cfg->max_reconnect_attempts) {
            
            int current_time = get_current_time_ms();
            int time_since_lost = current_time - mqtt_ctx->connection_lost_time;
            
            // 检查是否到了重连时间
            if (time_since_lost >= cfg->reconnect_interval * 1000) {
                printf("[AutoReconnect] Attempting reconnection (attempt %d/%d)\n", 
                       mqtt_ctx->reconnect_attempts + 1, cfg->max_reconnect_attempts);
                
                pthread_mutex_unlock(&mqtt_ctx->keepalive_mutex);
                
                // 检查MQTT客户端是否仍然有效（防止在释放过程中重连）
                if (mqtt_ctx->client == NULL) {
                    printf("[AutoReconnect] MQTT client is NULL, stopping reconnection thread\n");
                    pthread_mutex_lock(&mqtt_ctx->keepalive_mutex);
                    mqtt_ctx->reconnect_running = 0;
                    pthread_mutex_unlock(&mqtt_ctx->keepalive_mutex);
                    break;
                }
                
                // 配置重连选项
                MQTTAsync_connectOptions conn_opts;
                create_connect_options(&conn_opts, cfg);
                conn_opts.context = mqtt_ctx;  // 设置上下文
                
                // 发起异步重连
                int result = MQTTAsync_connect(mqtt_ctx->client, &conn_opts);
                
                pthread_mutex_lock(&mqtt_ctx->keepalive_mutex);
                
                if (result == MQTTASYNC_SUCCESS) {
                    printf("[AutoReconnect] Reconnection request sent successfully!\n");
                } else {
                    // 重连请求失败，增加重连计数
                    mqtt_ctx->reconnect_attempts++;
                    printf("[AutoReconnect] Reconnection request failed (attempt %d/%d)\n", 
                           mqtt_ctx->reconnect_attempts, cfg->max_reconnect_attempts);
                }
            }
        }
        
        pthread_mutex_unlock(&mqtt_ctx->keepalive_mutex);
        sleep(2);  // 每2秒检查一次
    }
    
    printf("[AutoReconnect] Reconnect thread stopped\n");
    return NULL;
}
