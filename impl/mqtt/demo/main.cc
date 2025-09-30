#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include "proto_mqtt.h"

// 全局变量用于信号处理
static int g_running = 1;

// 信号处理函数
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n收到中断信号，正在退出..." << std::endl;
        g_running = 0;
    }
}

int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== MQTT协议测试程序 ===" << std::endl;
    
    // 初始化协议上下文
    proto_ctx_t ctx = {};
    ctx.type = PROTO_TYPE_MQTT;
    ctx.config = NULL;  
    // 初始化MQTT客户端
    int init_result = proto_driver_init(&ctx);
    if (init_result != PROTO_SUCCESS) {
        std::cerr << "MQTT客户端初始化失败: " << init_result << std::endl;
        return -1;
    }
    std::cout << "MQTT客户端初始化成功" << std::endl;
    
    // // 显示配置信息（从上下文获取）
    // mqtt_config_t *config = (mqtt_config_t *)ctx.config;
    // if (config) {
    //     std::cout << "配置 => broker=" << config->broker 
    //               << ", client_id=" << config->client_id 
    //               << ", pub_topic=" << config->pub_topic 
    //               << ", sub_topic=" << config->sub_topic << std::endl;
        
    //     // 打印详细的连接参数
    //     std::cout << "连接参数详情:" << std::endl;
    //     std::cout << "  - Broker URI: " << config->broker << std::endl;
    //     std::cout << "  - Client ID: " << config->client_id << std::endl;
    //     std::cout << "  - Username: " << config->username << std::endl;
    //     std::cout << "  - Password: " << config->password << std::endl;
    //     std::cout << "  - Keepalive: " << config->keepalive_interval << " seconds" << std::endl;
    //     std::cout << "  - QoS: " << config->qos << std::endl;
    //     std::cout << "  - Retained: " << (config->retained ? "true" : "false") << std::endl;
    //     std::cout << "  - Timeout: " << config->timeout_ms << " ms" << std::endl;
    //     std::cout << "  - Auto Reconnect: " << (config->enable_auto_reconnect ? "enabled" : "disabled") << std::endl;
    //     if (config->enable_auto_reconnect) {
    //         std::cout << "  - Max Reconnect Attempts: " << (config->max_reconnect_attempts == 0 ? "unlimited" : std::to_string(config->max_reconnect_attempts)) << std::endl;
    //         std::cout << "  - Reconnect Interval: " << config->reconnect_interval << " seconds" << std::endl;
    //     }
    // }
    
    // 连接到MQTT服务器（异步）
    std::cout << "正在连接到MQTT服务器..." << std::endl;
    int connect_result = proto_connect(&ctx);
    if (connect_result != PROTO_SUCCESS) {
        std::cerr << "连接失败: " << connect_result << std::endl;
        proto_driver_release(&ctx);
        return -1;
    }
    
    std::cout << "开始读写线程..." << std::endl;
    
    // 创建发送线程
    pthread_t tx_thread;
    pthread_create(&tx_thread, NULL, [](void* arg)->void* {
        proto_ctx_t* ctxp = (proto_ctx_t*)arg;
        int message_count = 0;
        while (g_running) {
            std::string message = "Hello MQTT! Message #" + std::to_string(message_count);
            proto_request_t write_req = {};
            write_req.value = (void*)message.c_str();
            write_req.quantity = message.length();
            int write_result = proto_write(ctxp, &write_req);
            if (write_result == PROTO_SUCCESS) {
                std::cout << "发送消息: " << message << std::endl;
            }
            message_count++;
            sleep(1);
        }
        return NULL;
    }, &ctx);

    // 创建接收线程
    pthread_t rx_thread;
    pthread_create(&rx_thread, NULL, [](void* arg)->void* {
        proto_ctx_t* ctxp = (proto_ctx_t*)arg;
        while (g_running) {
            char read_buffer[1024];
            proto_request_t read_req = {};
            read_req.value = read_buffer;
            read_req.quantity = sizeof(read_buffer);
            int read_result = proto_read(ctxp, &read_req);
            if (read_result == PROTO_SUCCESS) {
                std::string received_message(read_buffer, read_req.quantity);
                std::cout << "收到消息: " << received_message << std::endl;
            }
            usleep(200 * 1000); // 200ms 轮询
        }
        return NULL;
    }, &ctx);

    // 等待退出
    pthread_join(tx_thread, NULL);
    pthread_join(rx_thread, NULL);
    
    // 断开连接
    std::cout << "正在断开连接..." << std::endl;
    proto_disconnect(&ctx);
    
    // 释放资源
    proto_driver_release(&ctx);
    std::cout << "程序结束" << std::endl;
    
    return 0;
}