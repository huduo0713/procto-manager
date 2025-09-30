#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h"
#include "impl/mqtt/src/proto_mqtt.h"

// 全局变量用于信号处理
static int g_running = 1;

// 信号处理函数
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n收到中断信号，正在退出..." << std::endl;
        g_running = 0;
    }
}

int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "=== MQTT协议测试程序（plc封装） ===" << std::endl;
    std::cout << "开始读写线程..." << std::endl;

    // 创建发送线程
    pthread_t tx_thread;
    pthread_create(&tx_thread, NULL, [](void* arg)->void* {
        (void)arg;
        int message_count = 0;
        char payload[1024] = {0};
        while (g_running) {
            payload[0] = '\0';
            float temp = 10.2f + (message_count % 10);
            int mode = message_count % 4;
            int rc1 = mqtt_data_format("temp", &temp, ENUM_FLOAT, payload);
            int rc2 = mqtt_data_format("mode", &mode, ENUM_INT32, payload);
            if (rc1 != PROTO_SUCCESS || rc2 != PROTO_SUCCESS) {
                std::cout << "拼接mode失败, code=" << rc2 << std::endl;
                sleep(1);
                continue;
            }
            proto_request_t write_req = {};
            write_req.value = (void*)payload;
            write_req.quantity = (uint16_t)strlen(payload);
            int write_result = plc_proto_write(&write_req);
            if (write_result == PROTO_SUCCESS) {
                std::cout << "发送JSON: " << payload << std::endl;
            } else {
                std::cout << "发送失败, code=" << write_result << std::endl;
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
            char read_buffer[1024];
            memset(read_buffer, 0, sizeof(read_buffer));
            proto_request_t read_req = {};
            read_req.value = read_buffer;
            read_req.quantity = (uint16_t)sizeof(read_buffer);
            int read_result = plc_proto_read(&read_req);
            if (read_result == PROTO_SUCCESS) {
                std::string received_message(read_buffer, read_req.quantity);
                std::cout << "收到消息: " << received_message << std::endl;
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
    std::cout << "程序结束" << std::endl;
    return 0;
}


