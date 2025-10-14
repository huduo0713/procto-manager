#include "proto_mqtt.h"
#include "common/utils/one_logger.hpp"

/**
 * @brief 写入MQTT消息
 * @param ctx 协议上下文指针
 * @param req 请求结构体指针
 * @return PROTO_SUCCESS 成功，其他值表示错误
 * 
 * 功能：
 * 1. 参数校验和连接状态检查
 * 2. 自动处理连接断开和重连
 * 3. 准备MQTT消息结构（支持二进制数据）
 * 4. 异步发布消息到指定主题
 * 5. 设置发送回调以获取发布结果
 * 6. 立即返回，不等待发布完成
 */
int mqtt_proto_write(proto_ctx_t *ctx, mqtt_write_t *req) {
    // 参数校验
    if (!ctx || !req || !req->payload) {
        return PROTO_ERROR_PARAM;
    }
    
    // 调试信息：显示发送的数据
    log_info("[MQTT] Write - Topic: {}, Payload: {}, QoS: {}, Retained: {}", 
           req->topic, req->payload, req->qos, req->retained);
    
    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return PROTO_ERROR_PARAM;
    }
    
    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    
    // 如果未连接且启用了自动重连，自动尝试连接
    connect_status_t status = get_connect_status(mqtt_ctx);
    if (status != CON_OK) {
        if (cfg->enable_auto_reconnect && status == DCON_OK) {
            // 自动重连
            log_info("[MQTT] Auto-reconnecting for write operation...");
            int connect_result = proto_connect(ctx);
            if (connect_result != PROTO_SUCCESS) {
                log_error("[MQTT] Auto-reconnect failed for write operation");
                return PROTO_ERROR_WRITE;
            }
            // 异步重连已发起。
            set_operation_status(mqtt_ctx, OP_PENDING);
            return PROTO_ERROR_WRITE;
        } else {
            // 未启用自动重连或连接状态不允许重连
            set_operation_status(mqtt_ctx, OP_FAILED);
            return PROTO_ERROR_WRITE;
        }
    }
    
    // 准备异步消息结构
    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = req->payload;                         // 消息内容
    msg.payloadlen = strlen(req->payload);              // 消息长度
    msg.qos = req->qos;                                 // 服务质量等级
    msg.retained = req->retained;                       // 是否保留

    // 设置发送回调选项
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = onSendSuccess;                     // 发送成功回调
    opts.onFailure = onSendFailure;                     // 发送失败回调
    opts.context = mqtt_ctx;                            // 上下文指针

    // 异步发布消息（立即返回，不等待完成）
    int result = MQTTAsync_sendMessage(mqtt_ctx->client, req->topic, &msg, &opts);
    
    if (result != MQTTASYNC_SUCCESS) {
        set_operation_status(mqtt_ctx, OP_FAILED);
        log_error("[MQTT] Send failed: {}", MQTTAsync_strerror(result));
        return PROTO_ERROR_WRITE;
    }
    set_operation_status(mqtt_ctx, OP_PENDING);
    return PROTO_SUCCESS;
}

/**
 * @brief 读取MQTT消息（带自动重连）
 * @param ctx 协议上下文指针
 * @param req 请求结构体指针
 * @return PROTO_SUCCESS 成功，PROTO_NO_DATA 没有数据可读，其他值表示错误
 * 
 * 功能：
 * 1. 参数校验和上下文检查
 * 2. 自动处理连接断开和重连
 * 3. 非阻塞检查消息缓冲区
 * 4. 从环形队列头部获取消息（支持二进制数据）
 * 5. 安全复制消息内容
 * 6. 更新缓冲区指针和计数
 */
int mqtt_proto_read(proto_ctx_t *ctx, mqtt_read_t *req) {
    // 参数校验
    if (!ctx || !req || !req->payload) {
        return PROTO_ERROR_PARAM;
    }

    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)ctx->userdata;
    if (!mqtt_ctx) {
        return PROTO_ERROR_PARAM;
    }

    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    
    // 连接管理：如果未连接且启用了自动重连，自动尝试连接（异步，不等待）
    connect_status_t status = get_connect_status(mqtt_ctx);
    if (status != CON_OK) {
        if (cfg->enable_auto_reconnect && status == DCON_OK) {
            // 自动重连：发起连接请求
            log_info("[MQTT] Auto-reconnecting for read operation...");
            int connect_result = proto_connect(ctx);
            if (connect_result != PROTO_SUCCESS) {
                log_error("[MQTT] Auto-reconnect failed for read operation");
                return PROTO_ERROR_READ;
            }
            // 异步重连已发起，不阻塞等待。读取返回无数据。
            set_operation_status(mqtt_ctx, OP_PENDING);
            return NO_DATA;
        } else {
            // 未启用自动重连或连接状态不允许重连
            set_operation_status(mqtt_ctx, OP_FAILED);
            return PROTO_ERROR_READ;
        }
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
    
    // 复制消息内容到 mqtt_read_t 结构体
    size_t copy_len = (msg->payloadlen < sizeof(req->payload)) ? msg->payloadlen : sizeof(req->payload) - 1;
    memcpy(req->payload, msg->payload, copy_len);
    req->payload[copy_len] = '\0';  // 确保字符串终止
    
    // 复制主题
    strncpy(req->topic, msg->topic, sizeof(req->topic) - 1);
    req->topic[sizeof(req->topic) - 1] = '\0';
    
    // 调试信息：显示接收的数据
    log_info("[MQTT] Read - Topic: {}, Payload: {}, Length: {}", 
           req->topic, req->payload, copy_len);
    
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
    if (!ctx) return PROTO_ERROR_PARAM;
    
    // 从 YAML 文件加载配置（使用堆内存，避免返回后悬垂指针）
    mqtt_config_t *cfg = (mqtt_config_t *)malloc(sizeof(mqtt_config_t));
    if (!cfg) {
        return PROTO_ERROR_PARAM;
    }
    memset(cfg, 0, sizeof(mqtt_config_t));
    const char* yaml_path = "/usr/runtime/protocol/mqtt/config.yaml";
    int yaml_ret = load_mqtt_config_from_yaml(yaml_path, cfg);
    if (yaml_ret != 0) {
        log_error("[MQTT] Failed to load config from YAML: {}", yaml_ret);
        free(cfg);
        return PROTO_ERROR_PARAM;
    }
    
    // 将加载的配置设置到上下文中
    ctx->config = cfg;
    
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
    
    // 初始化重连机制（简化版，无需线程）
    mqtt_ctx->reconnect_attempts = 0;
    
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
        free(mqtt_ctx);
        log_error("[MQTT] Create failed: {}", MQTTAsync_strerror(rc));
        return PROTO_ERROR_PARAM;
    }
    
    // 设置回调函数
    MQTTAsync_setCallbacks(mqtt_ctx->client, mqtt_ctx, onConnectionLost, messageArrived, NULL);
    
    // 使用MQTT协议原生的保活机制，无需额外线程
    // 使用直接重连机制，无需重连线程
    
    // 更新原始上下文
    *ctx = mqtt_ctx->base;
    
    log_info("[MQTT] Client initialized successfully");
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
    log_info("[MQTT] Releasing client resources...");

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
    
    // 释放内存
    free(mqtt_ctx);
    ctx->userdata = NULL;
    
    // 释放配置内存
    if (ctx->config) {
        free(ctx->config);
        ctx->config = NULL;
    }

    log_info("[MQTT] Client resources released successfully");
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
                    log_error("[MQTT] Connect failed: {}", MQTTAsync_strerror(rc));
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
                    log_error("[MQTT] Disconnect failed: {}", MQTTAsync_strerror(rc));
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