#include "proto_mqtt.h"

/**
 * @brief 连接丢失回调函数
 * @param context MQTT上下文指针
 * @param cause 连接丢失原因
 * 功能：
 * 1. 检测到连接丢失时自动调用
 * 2. 更新实例连接状态为断开
 * 3. 根据配置决定是否重连（支持有限次数或无限重连）
 */
void onConnectionLost(void* context, char* cause) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Connection lost but context is NULL\n");
        return;
    }
    
    // 只有在连接状态为正常或连接中时才处理连接丢失
    if (get_connect_status(mqtt_ctx) == CON_OK || get_connect_status(mqtt_ctx) == CON_PENDING) {
        set_connect_status(mqtt_ctx, DCON_OK);  // 更新为断开状态
        printf("[MQTT] Connection lost. Cause: %s\n", (cause != NULL) ? cause : "Unknown");
        
        // 灵活重连机制：支持有限次数重连和无限重连
        mqtt_config_t* cfg = (mqtt_config_t*)mqtt_ctx->base.config;
        if (cfg && cfg->enable_auto_reconnect) {
            // 检查重连条件
            int should_reconnect = 0;
            
            if (cfg->max_reconnect_attempts == 0) {
                // 无限重连模式
                should_reconnect = 1;
                mqtt_ctx->reconnect_attempts++;
                printf("[AutoReconnect] Attempting immediate reconnection (attempt %d, infinite mode)\n", 
                       mqtt_ctx->reconnect_attempts);
            } else if (mqtt_ctx->reconnect_attempts < cfg->max_reconnect_attempts) {
                // 有限次数重连模式
                should_reconnect = 1;
                mqtt_ctx->reconnect_attempts++;
                printf("[AutoReconnect] Attempting immediate reconnection (attempt %d/%d)\n", 
                       mqtt_ctx->reconnect_attempts, cfg->max_reconnect_attempts);
            } else {
                // 已达到最大重连次数
                printf("[AutoReconnect] Max reconnection attempts (%d) reached\n", cfg->max_reconnect_attempts);
            }
            
            if (should_reconnect) {
                // 配置重连选项
                MQTTAsync_connectOptions conn_opts;
                create_connect_options(&conn_opts, cfg);
                conn_opts.context = mqtt_ctx;  // 设置上下文
                
                // 发起异步重连
                int result = MQTTAsync_connect(mqtt_ctx->client, &conn_opts);
                if (result == MQTTASYNC_SUCCESS) {
                    printf("[AutoReconnect] Reconnection request sent successfully!\n");
                } else {
                    printf("[AutoReconnect] Reconnection request failed: %s\n", MQTTAsync_strerror(result));
                }
            }
        }
    }
}

/**
 * @brief 消息到达回调函数
 * @param context MQTT上下文指针
 * @param topicName 主题名称
 * @param topicLen 主题长度
 * @param message 消息内容
 * @return 1表示成功处理
 * 
 * 功能：
 * 1. 接收所有到达的MQTT消息
 * 2. 实现环形缓冲区存储消息（支持二进制数据）
 * 3. 通知等待的读取线程
 * 4. 自动释放MQTT库内存
 */
int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Warning: context is NULL, discarding message\n");
        MQTTAsync_freeMessage(&message);
        MQTTAsync_free(topicName);
        return 1;
    }
    
    printf("[MQTT] Message arrived on topic: %.*s, payloadlen: %d\n", 
           topicLen, topicName, (int)message->payloadlen);
    
    pthread_mutex_lock(&mqtt_ctx->msg_mutex);
    
    // 环形缓冲区满处理：丢弃最旧的消息
    if (mqtt_ctx->msg_buffer_count >= MAX_MSG_BUFFER_SIZE) {
        mqtt_ctx->msg_buffer_head = (mqtt_ctx->msg_buffer_head + 1) % MAX_MSG_BUFFER_SIZE;
        mqtt_ctx->msg_buffer_count--;
        printf("[MQTT] Message buffer full. Discard oldest message.\n");
    }
    
    // 存储消息到环形缓冲区尾部
    MQTT_Message* new_msg = &mqtt_ctx->msg_buffer[mqtt_ctx->msg_buffer_tail];
    
    // 复制主题名称
    strncpy(new_msg->topic, topicName, sizeof(new_msg->topic) - 1);
    new_msg->topic[sizeof(new_msg->topic) - 1] = '\0';
    
    // 处理消息载荷（支持二进制数据）
    if (message->payload != NULL && message->payloadlen > 0) {
        // 安全复制消息内容，防止缓冲区溢出
        size_t copy_len = (message->payloadlen < MAX_MESSAGE_SIZE) ? message->payloadlen : MAX_MESSAGE_SIZE;
        memcpy(new_msg->payload, message->payload, copy_len);
        new_msg->payloadlen = copy_len;  // 保存实际长度
    } else {
        new_msg->payload[0] = '\0';
        new_msg->payloadlen = 0;
    }
    
    // 记录消息时间戳
    new_msg->timestamp_ms = (long long)(time(NULL) * 1000);
    
    // 更新环形缓冲区指针和计数
    mqtt_ctx->msg_buffer_tail = (mqtt_ctx->msg_buffer_tail + 1) % MAX_MSG_BUFFER_SIZE;
    mqtt_ctx->msg_buffer_count++;
    
    // 通知等待的读取线程有新消息到达
    pthread_cond_signal(&mqtt_ctx->msg_cond);
    
    pthread_mutex_unlock(&mqtt_ctx->msg_mutex);
    
    // 释放MQTT库分配的内存（重要：防止内存泄漏）
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

/**
 * @brief 连接成功回调函数
 * @param context MQTT上下文指针
 * @param response 连接成功响应数据
 * 
 * 功能：
 * 1. 异步连接成功时自动调用
 * 2. 更新实例连接状态为已连接
 * 3. 重置重连计数
 * 4. 通知等待连接完成的线程
 */
void onConnectSuccess(void* context, MQTTAsync_successData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Connect success but context is NULL\n");
        return;
    }
    
    // 只有在非连接状态时才更新状态
    if (get_connect_status(mqtt_ctx) != CON_OK) {
        set_connect_status(mqtt_ctx, CON_OK);  // 更新为连接成功状态
        set_operation_status(mqtt_ctx, OP_SUCCESS);
        printf("[MQTT] Connect success!\n");
        
        // 重置重连相关状态
        mqtt_ctx->reconnect_attempts = 0;      // 重置重连计数
        printf("[AutoReconnect] Reconnection successful!\n");
        
        // 连接成功后自动订阅topic
        mqtt_config_t* cfg = (mqtt_config_t*)mqtt_ctx->base.config;
        if (cfg && strlen(cfg->sub_topic) > 0) {
            MQTTAsync_responseOptions sub_opts = MQTTAsync_responseOptions_initializer;
            sub_opts.onSuccess = onSubscribeSuccess;
            sub_opts.onFailure = onSubscribeFailure;
            sub_opts.context = mqtt_ctx;
            
            int rc = MQTTAsync_subscribe(mqtt_ctx->client, cfg->sub_topic, cfg->qos, &sub_opts);
            if (rc == MQTTASYNC_SUCCESS) {
                printf("[MQTT] Subscribe request sent for topic: %s\n", cfg->sub_topic);
            } else {
                printf("[MQTT] Subscribe failed for topic: %s, error: %s\n", cfg->sub_topic, MQTTAsync_strerror(rc));
            }
        }
    }
    
    // 调用用户回调函数
    if (mqtt_ctx->connect_callback) {
        mqtt_ctx->connect_callback(&mqtt_ctx->base, PROTO_SUCCESS, mqtt_ctx->connect_userdata);
    }
}

/**
 * @brief 连接失败回调函数
 * @param context MQTT上下文指针
 * @param response 连接失败响应数据
 * 
 * 功能：
 * 1. 异步连接失败时自动调用
 * 2. 更新实例连接状态为断开
 * 3. 记录失败原因和错误码
 * 4. 通知等待连接完成的线程
 */
void onConnectFailure(void* context, MQTTAsync_failureData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Connect failed but context is NULL\n");
        return;
    }
    
    set_connect_status(mqtt_ctx, DCON_OK);  // 更新为断开状态
    set_operation_status(mqtt_ctx, OP_FAILED);
    
    // 记录详细的失败信息
    if (response != NULL) {
        printf("[MQTT] Connect failed. Reason Code: %d, Message: %s\n", 
               response->code, response->message);
    } else {
        printf("[MQTT] Connect failed. Unknown reason\n");
    }
    
    // 调用用户回调函数
    if (mqtt_ctx->connect_callback) {
        mqtt_ctx->connect_callback(&mqtt_ctx->base, PROTO_ERROR_CONNECT, mqtt_ctx->connect_userdata);
    }
}

/**
 * @brief 断开连接成功回调函数
 * @param context MQTT上下文指针
 * @param response 断开成功响应数据
 * 
 * 功能：
 * 1. 异步断开连接成功时自动调用
 * 2. 更新实例连接状态为断开
 * 3. 通知等待断开完成的线程
 */
void onDisconnectSuccess(void* context, MQTTAsync_successData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Disconnect success but context is NULL\n");
        return;
    }
    
    // 只有在连接状态为正常或断开中时才处理
    if (get_connect_status(mqtt_ctx) == CON_OK || get_connect_status(mqtt_ctx) == DCON_PENDING) {
        set_connect_status(mqtt_ctx, DCON_OK);  // 更新为断开状态
        set_operation_status(mqtt_ctx, OP_SUCCESS);
        printf("[MQTT] Disconnect success (active)\n");
    }
    
    // 调用用户回调函数
    if (mqtt_ctx->disconnect_callback) {
        mqtt_ctx->disconnect_callback(&mqtt_ctx->base, PROTO_SUCCESS, mqtt_ctx->disconnect_userdata);
    }
}

/**
 * @brief 断开连接失败回调函数
 * @param context MQTT上下文指针
 * @param response 断开失败响应数据
 * 
 * 功能：
 * 1. 异步断开连接失败时自动调用
 * 2. 更新实例连接状态为断开
 * 3. 记录失败原因和错误码
 * 4. 通知等待断开完成的线程
 */
void onDisconnectFailure(void* context, MQTTAsync_failureData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Disconnect failed but context is NULL\n");
        return;
    }
    
    set_connect_status(mqtt_ctx, DCON_OK);  // 更新为断开状态
    set_operation_status(mqtt_ctx, OP_FAILED);
    
    // 记录详细的失败信息
    if (response != NULL) {
        printf("[MQTT] Disconnect failed (active). Reason Code: %d, Message: %s\n",
               response->code, response->message);
    } else {
        printf("[MQTT] Disconnect failed (active). Unknown reason\n");
    }
    
    // 调用用户回调函数
    if (mqtt_ctx->disconnect_callback) {
        mqtt_ctx->disconnect_callback(&mqtt_ctx->base, PROTO_ERROR_CONNECT, mqtt_ctx->disconnect_userdata);
    }
}

/**
 * @brief 发送成功回调函数
 * @param context MQTT上下文指针
 * @param response 发送成功响应数据
 * 
 * 功能：
 * 1. 异步发送成功时自动调用
 * 2. 更新操作状态为成功
 * 3. 调用用户发送回调函数
 */
void onSendSuccess(void* context, MQTTAsync_successData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Send success but context is NULL\n");
        return;
    }
    
    set_operation_status(mqtt_ctx, OP_SUCCESS);
    // 调用用户回调函数
    if (mqtt_ctx->send_callback) {
        mqtt_ctx->send_callback(&mqtt_ctx->base, PROTO_SUCCESS, mqtt_ctx->send_userdata);
    }
}

/**
 * @brief 发送失败回调函数
 * @param context MQTT上下文指针
 * @param response 发送失败响应数据
 * 
 * 功能：
 * 1. 异步发送失败时自动调用
 * 2. 更新操作状态为失败
 * 3. 记录失败原因和错误码
 * 4. 调用用户发送回调函数
 */
void onSendFailure(void* context, MQTTAsync_failureData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Send failed but context is NULL\n");
        return;
    }
    
    set_operation_status(mqtt_ctx, OP_FAILED);
    
    // 记录详细的失败信息
    if (response != NULL) {
        printf("[MQTT] Send failed. Reason Code: %d, Message: %s\n", 
               response->code, response->message);
    } else {
        printf("[MQTT] Send failed. Unknown reason\n");
    }
    
    // 调用用户回调函数
    if (mqtt_ctx->send_callback) {
        mqtt_ctx->send_callback(&mqtt_ctx->base, PROTO_ERROR_WRITE, mqtt_ctx->send_userdata);
    }
}

/**
 * @brief 订阅成功回调函数
 * @param context MQTT上下文指针
 * @param response 订阅成功响应数据
 * 
 * 功能：
 * 1. 异步订阅成功时自动调用
 * 2. 记录订阅成功信息
 */
void onSubscribeSuccess(void* context, MQTTAsync_successData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Subscribe success but context is NULL\n");
        return;
    }
    
    printf("[MQTT] Subscribe success!\n");
}

/**
 * @brief 订阅失败回调函数
 * @param context MQTT上下文指针
 * @param response 订阅失败响应数据
 * 
 * 功能：
 * 1. 异步订阅失败时自动调用
 * 2. 记录失败原因和错误码
 */
void onSubscribeFailure(void* context, MQTTAsync_failureData* response) {
    mqtt_ctx_t* mqtt_ctx = (mqtt_ctx_t*)context;
    if (!mqtt_ctx) {
        printf("[MQTT] Subscribe failed but context is NULL\n");
        return;
    }
    
    // 记录详细的失败信息
    if (response != NULL) {
        printf("[MQTT] Subscribe failed. Reason Code: %d, Message: %s\n", 
               response->code, response->message);
    } else {
        printf("[MQTT] Subscribe failed. Unknown reason\n");
    }
}