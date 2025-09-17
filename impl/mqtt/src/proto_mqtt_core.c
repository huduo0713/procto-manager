#include "proto_mqtt.h"


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

/**
 * @brief 获取操作状态（用于循环调用判断）
 * @param ctx 协议上下文指针
 * @return 当前操作状态
 */
 operation_status_t proto_get_operation_status(proto_ctx_t *ctx) {
    // 参数校验
    if (!ctx) {
        return OP_IDLE;
    }

    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return OP_IDLE;
    }

    return get_operation_status(mqtt_ctx);
}

/**
 * @brief 获取连接状态
 * @param ctx 协议上下文指针
 * @return 当前连接状态
 */
connect_status_t proto_get_connect_status(proto_ctx_t *ctx) {
    // 参数校验
    if (!ctx) {
        return CON_IDLE;
    }

    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return CON_IDLE;
    }

    return get_connect_status(mqtt_ctx);
}

/**
 * @brief 等待连接完成（支持超时）
 * @param ctx MQTT上下文指针
 * @param timeout_ms 超时时间（毫秒）
 * @return 0成功，-1超时
 */
int wait_for_connection(mqtt_ctx_t* ctx, int timeout_ms) {
    if (!ctx) return -1;
    
    pthread_mutex_lock(&ctx->state_mutex);
    
    // 如果已经连接或断开，直接返回
    if (ctx->connect_status == CON_OK || ctx->connect_status == DCON_OK) {
        pthread_mutex_unlock(&ctx->state_mutex);
        return 0;
    }
    
    // 等待连接状态变化
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec++;
        timeout.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(&ctx->conn_cond, &ctx->state_mutex, &timeout);
    pthread_mutex_unlock(&ctx->state_mutex);
    
    return (result == 0) ? 0 : -1;
}


/**
 * @brief 异步写入MQTT消息
 * @param ctx 协议上下文指针
 * @param req 请求结构体指针
 * @return PROTO_SUCCESS 成功，其他值表示错误
 * 
 * 功能：
 * 1. 参数校验和连接状态检查
 * 2. 准备MQTT消息结构（支持二进制数据）
 * 3. 异步发布消息到指定主题
 * 4. 设置发送回调以获取发布结果
 * 5. 立即返回，不等待发布完成
 */
int proto_write(proto_ctx_t *ctx, proto_request_t *req) {
    // 参数校验
    if (!ctx || !req || !req->value || req->quantity == 0) {
        return PROTO_ERROR_PARAM;
    }
    
    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return PROTO_ERROR_PARAM;
    }
    
    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    
    // 检查连接状态
    if (get_connect_status(mqtt_ctx) != CON_OK) {
        set_operation_status(mqtt_ctx, OP_FAILED);
        return PROTO_ERROR_WRITE;
    }
    
    // 准备异步消息结构
    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = req->value;                           // 消息内容
    msg.payloadlen = req->quantity;                     // 消息长度（支持二进制）
    msg.qos = cfg->qos;                                 // 服务质量等级
    msg.retained = 0;                                   // 不保留消息

    // 设置发送回调选项
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = onSendSuccess;                     // 发送成功回调
    opts.onFailure = onSendFailure;                     // 发送失败回调
    opts.context = mqtt_ctx;                            // 上下文指针

    // 异步发布消息（立即返回，不等待完成）
    int result = MQTTAsync_sendMessage(mqtt_ctx->client, cfg->pub_topic, &msg, &opts);
    
    if (result != MQTTASYNC_SUCCESS) {
        set_operation_status(mqtt_ctx, OP_FAILED);
        printf("[MQTT] Send failed: %s\n", MQTTAsync_strerror(result));
        return PROTO_ERROR_WRITE;
    }

    set_operation_status(mqtt_ctx, OP_PENDING);
    return PROTO_SUCCESS;
}

/**
 * @brief 异步读取MQTT消息
 * @param ctx 协议上下文指针
 * @param req 请求结构体指针
 * @return PROTO_SUCCESS 成功，PROTO_NO_DATA 没有数据可读，其他值表示错误
 * 
 * 功能：
 * 1. 参数校验和上下文检查
 * 2. 非阻塞检查消息缓冲区
 * 3. 从环形队列头部获取消息（支持二进制数据）
 * 4. 安全复制消息内容
 * 5. 更新缓冲区指针和计数
 */
int proto_read(proto_ctx_t *ctx, proto_request_t *req) {
    // 参数校验
    if (!ctx || !req || !req->value || req->quantity == 0) {
        return PROTO_ERROR_PARAM;
    }

    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return PROTO_ERROR_PARAM;
    }

    // 检查连接状态
    if (get_connect_status(mqtt_ctx) != CON_OK) {
        set_operation_status(mqtt_ctx, OP_FAILED);
        return PROTO_ERROR_READ;
    }

    // 同步读取：直接从消息缓冲区读取
    pthread_mutex_lock(&mqtt_ctx->msg_mutex);
    
    // 检查是否有消息
    if (mqtt_ctx->msg_buffer_count == 0) {
        pthread_mutex_unlock(&mqtt_ctx->msg_mutex);
        set_operation_status(mqtt_ctx, OP_IDLE);
        return NO_DATA; // 没有消息时返回专门的无数据码
    }
    
    // 从缓冲区读取消息
    MQTT_Message *msg = &mqtt_ctx->msg_buffer[mqtt_ctx->msg_buffer_head];
    
    // 使用payloadlen而不是strlen，支持二进制数据
    size_t copy_len = (msg->payloadlen < req->quantity) ? msg->payloadlen : req->quantity;
    memcpy(req->value, msg->payload, copy_len);
    req->quantity = copy_len;
    
    // 更新缓冲区指针
    mqtt_ctx->msg_buffer_head = (mqtt_ctx->msg_buffer_head + 1) % MAX_MSG_BUFFER_SIZE;
    mqtt_ctx->msg_buffer_count--;
    
    pthread_mutex_unlock(&mqtt_ctx->msg_mutex);
    
    set_operation_status(mqtt_ctx, OP_SUCCESS);
    return PROTO_SUCCESS;
}


/**
 * @brief 初始化MQTT客户端（同步接口）
 * @param ctx 协议上下文指针
 * @return PROTO_SUCCESS 成功，其他值表示错误
 */
int proto_driver_init(proto_ctx_t *ctx) {
    // 参数校验
    if (!ctx || !ctx->config) return PROTO_ERROR_PARAM;
    
    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    
    // 配置参数校验
    if (!cfg->broker || !cfg->client_id) {
        return PROTO_ERROR_PARAM;
    }
    
    // 创建MQTT扩展上下文
    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)malloc(sizeof(mqtt_ctx_t));
    if (!mqtt_ctx) {
        return PROTO_ERROR_PARAM;
    }
    
    // 初始化基础上下文
    mqtt_ctx->base = *ctx;
    mqtt_ctx->base.userdata = mqtt_ctx;
    
    // 初始化消息缓冲区
    mqtt_ctx->msg_buffer_head = 0;
    mqtt_ctx->msg_buffer_tail = 0;
    mqtt_ctx->msg_buffer_count = 0;
    pthread_mutex_init(&mqtt_ctx->msg_mutex, NULL);
    pthread_cond_init(&mqtt_ctx->msg_cond, NULL);
    
    // 初始化实例状态管理
    mqtt_ctx->client = NULL;
    mqtt_ctx->connect_status = CON_IDLE;
    mqtt_ctx->operation_status = OP_IDLE;
    mqtt_ctx->timeout_ms = cfg->timeout_ms;
    pthread_mutex_init(&mqtt_ctx->state_mutex, NULL);
    pthread_cond_init(&mqtt_ctx->conn_cond, NULL);
    
    // 初始化保活和重连机制
    mqtt_ctx->reconnect_attempts = 0;
    mqtt_ctx->last_heartbeat_time = 0;
    mqtt_ctx->connection_lost_time = 0;
    mqtt_ctx->keepalive_running = 0;
    mqtt_ctx->reconnect_running = 0;
    pthread_mutex_init(&mqtt_ctx->keepalive_mutex, NULL);
    
    // 初始化连接质量监控
    mqtt_ctx->heartbeat_sent_count = 0;
    mqtt_ctx->heartbeat_ack_count = 0;
    mqtt_ctx->last_network_delay = 0;
    mqtt_ctx->connection_quality = 100;
    
    // 初始化回调函数
    mqtt_ctx->connect_callback = NULL;
    mqtt_ctx->disconnect_callback = NULL;
    mqtt_ctx->send_callback = NULL;
    mqtt_ctx->connect_userdata = NULL;
    mqtt_ctx->disconnect_userdata = NULL;
    mqtt_ctx->send_userdata = NULL;
    
    // 创建MQTT异步客户端
    MQTTAsync_createOptions create_opts = MQTTAsync_createOptions_initializer;
    create_opts.sendWhileDisconnected = 1;
    create_opts.maxBufferedMessages = 100;
    
    int rc = MQTTAsync_create(&mqtt_ctx->client, cfg->broker, cfg->client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTASYNC_SUCCESS) {
        // 清理已初始化的资源
        pthread_mutex_destroy(&mqtt_ctx->msg_mutex);
        pthread_cond_destroy(&mqtt_ctx->msg_cond);
        pthread_mutex_destroy(&mqtt_ctx->state_mutex);
        pthread_cond_destroy(&mqtt_ctx->conn_cond);
        pthread_mutex_destroy(&mqtt_ctx->keepalive_mutex);
        free(mqtt_ctx);
        printf("[MQTT] Create failed: %s\n", MQTTAsync_strerror(rc));
        return PROTO_ERROR_PARAM;
    }
    
    // 设置回调函数
    MQTTAsync_setCallbacks(mqtt_ctx->client, mqtt_ctx, onConnectionLost, messageArrived, NULL);
    
    // 更新原始上下文
    *ctx = mqtt_ctx->base;
    
    printf("[MQTT] Client initialized successfully\n");
    return PROTO_SUCCESS;
}

/**
 * @brief 释放MQTT客户端资源（同步接口）
 * @param ctx 协议上下文指针
 */
void proto_driver_release(proto_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return;
    }
    
    printf("[MQTT] Releasing client resources...\n");
    
    // 停止保活和重连线程
    mqtt_ctx->keepalive_running = 0;
    mqtt_ctx->reconnect_running = 0;
    
    // 等待线程结束
    if (mqtt_ctx->keepalive_thread) {
        pthread_join(mqtt_ctx->keepalive_thread, NULL);
    }
    if (mqtt_ctx->reconnect_thread) {
        pthread_join(mqtt_ctx->reconnect_thread, NULL);
    }
    
    // 先断开连接，再销毁客户端
    if (mqtt_ctx->client) {
        // 如果还连接着，先断开
        if (get_connect_status(mqtt_ctx) == CON_OK) {
            MQTTAsync_disconnectOptions disc_opts;
            create_disconnect_options(&disc_opts, mqtt_ctx->timeout_ms, mqtt_ctx);
            MQTTAsync_disconnect(mqtt_ctx->client, &disc_opts);
        }
        
        // 销毁MQTT客户端
        MQTTAsync_destroy(&mqtt_ctx->client);
        mqtt_ctx->client = NULL;
    }
    
    // 清理同步对象
    pthread_mutex_destroy(&mqtt_ctx->msg_mutex);
    pthread_cond_destroy(&mqtt_ctx->msg_cond);
    pthread_mutex_destroy(&mqtt_ctx->state_mutex);
    pthread_cond_destroy(&mqtt_ctx->conn_cond);
    pthread_mutex_destroy(&mqtt_ctx->keepalive_mutex);
    
    // 释放内存
    free(mqtt_ctx);
    ctx->userdata = NULL;
    
    printf("[MQTT] Client resources released successfully\n");
}

/**
 * @brief 连接到MQTT服务器（同步接口）
 * @param ctx 协议上下文指针
 * @return PROTO_SUCCESS 成功，其他值表示错误
 */
int proto_connect(proto_ctx_t *ctx) {
    // 参数校验
    if (!ctx || !ctx->config) return PROTO_ERROR_PARAM;

    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx || !mqtt_ctx->client) {
        return PROTO_ERROR_CONNECT;
    }

    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;

    // 状态机处理连接逻辑
    switch (get_connect_status(mqtt_ctx)) {
        case CON_OK:
            // 已经连接，直接返回成功
            set_operation_status(mqtt_ctx, OP_SUCCESS);
            return PROTO_SUCCESS;

        case CON_PENDING:
            // 正在连接中，返回成功（连接操作已发起）
            set_operation_status(mqtt_ctx, OP_PENDING);
            return PROTO_SUCCESS;

        case DCON_PENDING:
            // 正在断开中，不能连接
            set_operation_status(mqtt_ctx, OP_FAILED);
            return PROTO_ERROR_CONNECT;

        case CON_IDLE:
        case DCON_OK:
            // 需要发起连接
            {
                MQTTAsync_connectOptions conn_opts;
                create_connect_options(&conn_opts, cfg);
                conn_opts.context = mqtt_ctx;  // 设置上下文

                // 发起异步连接
                int rc = MQTTAsync_connect(mqtt_ctx->client, &conn_opts);
                if (rc != MQTTASYNC_SUCCESS) {
                    set_operation_status(mqtt_ctx, OP_FAILED);
                    printf("[MQTT] Connect failed: %s\n", MQTTAsync_strerror(rc));
                    return PROTO_ERROR_CONNECT;
                }

                set_connect_status(mqtt_ctx, CON_PENDING);  // 更新为连接中状态
                set_operation_status(mqtt_ctx, OP_PENDING); // 更新为操作进行中
            }
            break;
    }

    // 异步连接已发起，立即返回
    // 连接结果将通过MQTT回调函数处理，然后调用用户回调
    return PROTO_SUCCESS;
}

/**
 * @brief 断开MQTT连接（同步接口）
 * @param ctx 协议上下文指针
 */
void proto_disconnect(proto_ctx_t *ctx) {
    // 参数校验
    if (!ctx) {
        return;
    }

    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx || !mqtt_ctx->client) {
        return;
    }

    // 状态机处理断开逻辑
    switch (get_connect_status(mqtt_ctx)) {
        case CON_OK:
            // 已连接，需要断开
            {
                MQTTAsync_disconnectOptions disc_opts;
                create_disconnect_options(&disc_opts, mqtt_ctx->timeout_ms, mqtt_ctx);

                // 发起异步断开
                int rc = MQTTAsync_disconnect(mqtt_ctx->client, &disc_opts);
                if (rc != MQTTASYNC_SUCCESS) {
                    set_operation_status(mqtt_ctx, OP_FAILED);
                    printf("[MQTT] Disconnect failed: %s\n", MQTTAsync_strerror(rc));
                    return;
                }

                set_connect_status(mqtt_ctx, DCON_PENDING);  // 更新为断开中状态
                set_operation_status(mqtt_ctx, OP_PENDING);  // 更新为操作进行中
            }
            break;

        case CON_IDLE:
        case DCON_OK:
            // 已经断开，直接返回成功
            set_operation_status(mqtt_ctx, OP_SUCCESS);
            return;

        case CON_PENDING:
        case DCON_PENDING:
            // 正在连接或断开中，返回错误
            set_operation_status(mqtt_ctx, OP_FAILED);
            return;
    }

    // 异步断开已发起，立即返回
    // 断开结果将通过MQTT回调函数处理，然后调用用户回调
}
