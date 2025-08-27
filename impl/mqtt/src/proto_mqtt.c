#include "proto_mqtt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <MQTTClient.h>

#define MAX_MESSAGE_SIZE 1024

static pthread_mutex_t g_msg_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_msg_cond = PTHREAD_COND_INITIALIZER;
static char g_last_message[MAX_MESSAGE_SIZE] = {0};
static int g_has_new_message = 0;

int messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    pthread_mutex_lock(&g_msg_mutex);
    if (message->payloadlen > 0 && message->payloadlen < MAX_MESSAGE_SIZE) {
        memcpy(g_last_message, message->payload, message->payloadlen);
        g_last_message[message->payloadlen] = '\0';
        g_has_new_message = 1;
        pthread_cond_signal(&g_msg_cond);
    }
    pthread_mutex_unlock(&g_msg_mutex);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int proto_driver_init(proto_ctx_t *ctx) {
    if (!ctx || !ctx->config) return PROTO_ERROR_PARAM;
    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    MQTTClient client;

    if (MQTTClient_create(&client, cfg->broker, cfg->client_id,
                          MQTTCLIENT_PERSISTENCE_NONE, NULL) != MQTTCLIENT_SUCCESS)
        return PROTO_ERROR_INIT;

    MQTTClient_setCallbacks(client, NULL, NULL, messageArrived, NULL);
    ctx->client = client;
    return PROTO_SUCCESS;
}

void proto_driver_release(proto_ctx_t *ctx) {
    if (ctx && ctx->client) {
        MQTTClient_destroy((MQTTClient *)&ctx->client);
        ctx->client = NULL;
    }
}

int proto_connect(proto_ctx_t *ctx) {
    if (!ctx || !ctx->client) return PROTO_ERROR_PARAM;
    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    MQTTClient client = (MQTTClient)ctx->client;

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = cfg->username;
    conn_opts.password = cfg->password;

    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS)
        return PROTO_ERROR_CONNECT;

    if (MQTTClient_subscribe(client, cfg->sub_topic, 1) != MQTTCLIENT_SUCCESS)
        return PROTO_ERROR_CONNECT;

    return PROTO_SUCCESS;
}

void proto_disconnect(proto_ctx_t *ctx) {
    if (ctx && ctx->client)
        MQTTClient_disconnect((MQTTClient)ctx->client, 1000);
}

int proto_write(proto_ctx_t *ctx, proto_request_t *req) {
    if (!ctx || !req || !req->value) return PROTO_ERROR_PARAM;
    mqtt_config_t *cfg = (mqtt_config_t *)ctx->config;
    MQTTClient client = (MQTTClient)ctx->client;

    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = req->value;
    msg.payloadlen = strlen((char *)req->value);
    msg.qos = 1;
    msg.retained = 0;

    MQTTClient_deliveryToken token;
    if (MQTTClient_publishMessage(client, cfg->pub_topic, &msg, &token) != MQTTCLIENT_SUCCESS)
        return PROTO_ERROR_WRITE;

    MQTTClient_waitForCompletion(client, token, cfg->timeout_ms);
    return PROTO_SUCCESS;
}

int proto_read(proto_ctx_t *ctx, proto_request_t *req) {
    if (!ctx || !req || !req->value || req->quantity == 0)
        return PROTO_ERROR_PARAM;

    pthread_mutex_lock(&g_msg_mutex);
    while (!g_has_new_message) {
        pthread_cond_wait(&g_msg_cond, &g_msg_mutex);
    }

    size_t copy_len = req->quantity;
    if (copy_len > MAX_MESSAGE_SIZE)
        copy_len = MAX_MESSAGE_SIZE;

    strncpy((char *)req->value, g_last_message, copy_len);
    g_has_new_message = 0;
    pthread_mutex_unlock(&g_msg_mutex);

    return PROTO_SUCCESS;
}
