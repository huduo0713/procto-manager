#include <pthread.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h"
#include "proto_mqtt.h"

void* writer_thread(void* arg) {
    proto_ctx_t* ctx = (proto_ctx_t*)arg;
    proto_request_t req = {};
    strncpy(req.resource_name, "HoldingReg", sizeof(req.resource_name) - 1);

    char msg[64];
    for (int i = 0; i < 5; ++i) {
        snprintf(msg, sizeof(msg), "Hello %d", i);
        req.value = msg;

        if (proto_write(ctx, &req) == PROTO_SUCCESS) {
            std::cout << "[Writer] Message sent." << std::endl;
        } else {
            std::cerr << "[Writer] Failed to send." << std::endl;
        }
        sleep(1);
    }
    return nullptr;
}

void* reader_thread(void* arg) {
    proto_ctx_t* ctx = (proto_ctx_t*)arg;
    proto_request_t req = {};
    req.value = malloc(128);  // 简单分配一块缓冲
    req.quantity = 128;

    for (int i = 0; i < 10; ++i) {
        memset(req.value, 0, 128);
        if (proto_read(ctx, &req) == PROTO_SUCCESS) {
            std::cout << "[Reader] Received: " << (char*)req.value << std::endl;
        } else {
            std::cerr << "[Reader] Failed to read." << std::endl;
        }
        sleep(1);
    }
    free(req.value);
    return nullptr;
}

int main() {
    mqtt_config_t config = {};
    strncpy(config.broker, "tcp://1.92.111.153:1883", sizeof(config.broker) - 1);
    strncpy(config.client_id, "client_test_mqtt", sizeof(config.client_id) - 1);
    strncpy(config.username, "Admin", sizeof(config.username) - 1);
    strncpy(config.password, "123456", sizeof(config.password) - 1);
    strncpy(config.pub_topic, "device/echo", sizeof(config.pub_topic) - 1);
    strncpy(config.sub_topic, "device/echo", sizeof(config.sub_topic) - 1);  // 订阅同一 topic
    config.timeout_ms = 3000;

    proto_ctx_t ctx = {};
    ctx.type = PROTO_TYPE_MQTT;
    ctx.client = nullptr;
    ctx.config = &config;
    ctx.userdata = nullptr;

    if (proto_driver_init(&ctx) != PROTO_SUCCESS) {
        std::cerr << "MQTT init failed" << std::endl;
        return -1;
    }

    if (proto_connect(&ctx) != PROTO_SUCCESS) {
        std::cerr << "MQTT connect failed" << std::endl;
        proto_driver_release(&ctx);
        return -1;
    }

    std::cout << "MQTT connected successfully" << std::endl;
    sleep(1);

    pthread_t writer, reader;
    pthread_create(&writer, nullptr, writer_thread, &ctx);
    pthread_create(&reader, nullptr, reader_thread, &ctx);

    pthread_join(writer, nullptr);
    pthread_join(reader, nullptr);

    proto_disconnect(&ctx);
    proto_driver_release(&ctx);

    std::cout << "Program completed" << std::endl;
    return 0;
}
