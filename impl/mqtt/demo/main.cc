#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h"
#include "impl/mqtt/src/proto_mqtt.h"
#include "common/utils/one_logger.hpp"

// 全局变量用于信号处理
static int g_running = 1;

// 信号处理函数
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_info("收到中断信号，正在退出...");
        g_running = 0;
    }
}

int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    log_info("=== MQTT协议测试程序（plc封装） ===");
    log_info("开始读写线程...");

    // 创建发送线程
    pthread_t tx_thread;
    pthread_create(&tx_thread, NULL, [](void* arg)->void* {
        (void)arg;
        int message_count = 0;
        while (g_running) {
            mqtt_write_t write_req = {1, 0, "device/echo1111", ""};
            float temp = 10.2f + (message_count % 10);
            int mode = message_count % 4;
            int rc1 = mqtt_data_format("temp", &temp, ENUM_FLOAT, write_req.payload);
            int rc2 = mqtt_data_format("mode", &mode, ENUM_INT32, write_req.payload);
            if (rc1 != PROTO_SUCCESS || rc2 != PROTO_SUCCESS) {
                log_error("拼接mode失败, code={}", rc2);
                sleep(1);
                continue;
            }
            log_info("发送JSON: {}", write_req.payload);
            int write_result = plc_proto_write((void*)&write_req);
            if (write_result != PROTO_SUCCESS) {
                log_error("发送失败, code={}", write_result);
            }

            message_count++;
            sleep(1);
        }
        return NULL;
    }, NULL);

    // 创建接收线程
    pthread_t rx_thread;
    pthread_create(&rx_thread, NULL, [](void* arg)->void* {
        (void)arg;
        while (g_running) {
            mqtt_read_t read_req = {};
            memset(read_req.payload, 0, sizeof(read_req.payload));
            strncpy(read_req.topic, "device/echo", sizeof(read_req.topic) - 1);
            int read_result = plc_proto_read((void*)&read_req);
            if (read_result == PROTO_SUCCESS) {
                std::string received_message(read_req.payload);
                log_info("收到消息: {}", received_message);
            } else {
                // NO_DATA 等非致命情况无需打印过多日志
                usleep(200 * 1000); // 200ms 轮询间隔
            }
        }
        return NULL;
    }, NULL);

    // 等待退出
    pthread_join(tx_thread, NULL);
    pthread_join(rx_thread, NULL);

    // plc 封装内部持有的上下文会在进程结束时回收，如需显式释放可在此处扩展
    log_info("程序结束");
    return 0;
}


