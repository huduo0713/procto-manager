#include <pthread.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <vector>
#include <random>
#include <signal.h>
#include <atomic>
#include "common/api/proto_common.h"
#include "proto_mqtt.h"

// 简单循环测试配置
#define SEND_CYCLE_TIME_MS 1000   // 发送线程1秒循环周期
#define RECV_CYCLE_TIME_MS 100    // 接收线程100ms循环周期（更频繁）
#define MAX_RECONNECT_ATTEMPTS 10 // 最大重连尝试次数
#define RECONNECT_DELAY_MS 2000   // 重连延迟2秒

// 全局控制变量
std::atomic<bool> g_running{true};
std::atomic<bool> g_interrupted{false};
std::atomic<int> g_messages_sent{0};
std::atomic<int> g_messages_received{0};
std::atomic<int> g_reconnect_count{0};

// 循环调用测试函数

// 时间测量类
class TimeMeasure {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    long duration_us;
    
public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    void end() {
        auto end_time = std::chrono::high_resolution_clock::now();
        duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    }
    
    long getDuration() const { return duration_us; }
};


// 发送线程 - 持续发送消息
void* writer_thread(void* arg) {
    proto_ctx_t* ctx = (proto_ctx_t*)arg;
    proto_request_t req = {};
    strncpy(req.resource_name, "test_data", sizeof(req.resource_name) - 1);

    char msg[64];
    int writer_cycle_count = 0;
    
    std::cout << "[Writer] 发送线程启动，循环周期: " << SEND_CYCLE_TIME_MS << "ms" << std::endl;
    
    while (g_running) {
        auto cycle_begin = std::chrono::high_resolution_clock::now();
        
        // 检查连接状态
        connect_status_t status = proto_get_connect_status(ctx);
        if (status != CON_OK) {
            std::cout << "[Writer] 连接断开，状态: " << status << "，等待重连..." << std::endl;
            usleep(1000000); // 等待1秒
            continue;
        }
        
        // 发送消息
        snprintf(msg, sizeof(msg), "Test_Message_%d", writer_cycle_count);
        req.value = msg;
        req.quantity = strlen(msg);
        
        TimeMeasure timer;
        timer.start();
        int result = proto_write(ctx, &req);
        timer.end();
        
        if (result == PROTO_SUCCESS) {
            g_messages_sent++;
            std::cout << "[Writer] 发送: " << msg 
                     << " (耗时: " << timer.getDuration() << "μs)" << std::endl;
        } else {
            std::cout << "[Writer] 发送失败: " << msg 
                     << " (错误码: " << result << ")" << std::endl;
        }
        
        writer_cycle_count++;
        
        // 控制循环时间
        auto cycle_end = std::chrono::high_resolution_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(cycle_end - cycle_begin).count();
        
        if (cycle_duration < SEND_CYCLE_TIME_MS) {
            usleep((SEND_CYCLE_TIME_MS - cycle_duration) * 1000);
        } else {
            std::cout << "[Writer] 周期 " << writer_cycle_count << " 超时: " << cycle_duration << "ms" << std::endl;
        }
    }
    
    std::cout << "[Writer] 发送线程结束，总发送: " << g_messages_sent.load() << " 条消息" << std::endl;
    return nullptr;
}

// 接收线程 - 持续接收消息
void* reader_thread(void* arg) {
    proto_ctx_t* ctx = (proto_ctx_t*)arg;
    proto_request_t req = {};
    req.value = malloc(256);  // 分配消息缓冲区
    req.quantity = 256;

    int reader_cycle_count = 0;
    
    std::cout << "[Reader] 接收线程启动，循环周期: " << RECV_CYCLE_TIME_MS << "ms" << std::endl;
    
    while (g_running) {
        auto cycle_begin = std::chrono::high_resolution_clock::now();
        
        // 检查连接状态
        connect_status_t status = proto_get_connect_status(ctx);
        if (status != CON_OK) {
            std::cout << "[Reader] 连接断开，状态: " << status << "，等待重连..." << std::endl;
            usleep(500000); // 等待500ms
            continue;
        }
        
        // 尝试读取消息（非阻塞方式）
        memset(req.value, 0, 256);
        
        TimeMeasure timer;
        timer.start();
        int result = proto_read(ctx, &req);
        timer.end();
        
        if (result == PROTO_SUCCESS) {
            g_messages_received++;
            std::cout << "[Reader] 接收: " << (char*)req.value 
                     << " (耗时: " << timer.getDuration() << "μs)" << std::endl;
        } else if (result == NO_DATA) {
            // 没有消息可读，这是正常情况
            // std::cout << "[Reader] 无消息可读" << std::endl;
        } else {
            std::cout << "[Reader] 读取失败 (错误码: " << result << ")" << std::endl;
        }
        
        reader_cycle_count++;
        
        // 控制循环时间
        auto cycle_end = std::chrono::high_resolution_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(cycle_end - cycle_begin).count();
        
        if (cycle_duration < RECV_CYCLE_TIME_MS) {
            usleep((RECV_CYCLE_TIME_MS - cycle_duration) * 1000);
        } else {
            std::cout << "[Reader] 周期 " << reader_cycle_count << " 超时: " << cycle_duration << "ms" << std::endl;
        }
    }
    
    free(req.value);
    std::cout << "[Reader] 接收线程结束，总接收: " << g_messages_received.load() << " 条消息" << std::endl;
    return nullptr;
}

// 连接管理线程 - 负责自动重连
void* connection_manager_thread(void* arg) {
    proto_ctx_t* ctx = (proto_ctx_t*)arg;
    
    std::cout << "[ConnMgr] 连接管理线程启动" << std::endl;
    
    while (g_running) {
        connect_status_t status = proto_get_connect_status(ctx);
        
        if (status != CON_OK) {
            std::cout << "[ConnMgr] 检测到连接断开，状态: " << status << "，尝试重连..." << std::endl;
            g_reconnect_count++;
            
            int connect_attempts = 0;
            while (connect_attempts < MAX_RECONNECT_ATTEMPTS && g_running) {
                int result = proto_connect(ctx);
                
                if (result == PROTO_SUCCESS) {
                    // 等待连接完成
                    usleep(500000); // 等待500ms
                    
                    // 检查连接状态
                    connect_status_t new_status = proto_get_connect_status(ctx);
                    if (new_status == CON_OK) {
                        std::cout << "[ConnMgr] 重连成功! 尝试次数: " << (connect_attempts + 1) << std::endl;
                        break;
                    } else {
                        std::cout << "[ConnMgr] 重连中... 状态: " << new_status << std::endl;
                    }
                } else {
                    std::cout << "[ConnMgr] 重连失败，错误码: " << result << std::endl;
                }
                
                connect_attempts++;
                if (connect_attempts < MAX_RECONNECT_ATTEMPTS) {
                    std::cout << "[ConnMgr] 等待 " << RECONNECT_DELAY_MS << "ms 后重试..." << std::endl;
                    usleep(RECONNECT_DELAY_MS * 1000);
                }
            }
            
            if (connect_attempts >= MAX_RECONNECT_ATTEMPTS) {
                std::cout << "[ConnMgr] 重连失败，已达到最大重试次数" << std::endl;
            }
        }
        
        // 每5秒检查一次连接状态
        usleep(5000000);
    }
    
    std::cout << "[ConnMgr] 连接管理线程结束" << std::endl;
    return nullptr;
}

// 统计显示线程 - 定期显示统计信息
void* stats_thread(void* arg) {
    std::cout << "[Stats] 统计线程启动" << std::endl;
    
    while (g_running) {
        usleep(10000000); // 每10秒显示一次统计
        
        if (g_running) {
            std::cout << "[Stats] 统计信息 - 发送: " << g_messages_sent.load() 
                     << ", 接收: " << g_messages_received.load() 
                     << ", 重连: " << g_reconnect_count.load() << std::endl;
        }
    }
    
    std::cout << "[Stats] 统计线程结束" << std::endl;
    return nullptr;
}

// 信号处理函数
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n\n[Signal] 收到Ctrl+C信号" << std::endl;
        g_interrupted = true;
        g_running = false;
    }
}

int main() {
    std::cout << "🚀 MQTT多线程循环测试程序" << std::endl;
    std::cout << "配置: 发送线程" << SEND_CYCLE_TIME_MS << "ms, 接收线程" << RECV_CYCLE_TIME_MS << "ms, 自动重连" << std::endl;
    std::cout << "================================================" << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    std::cout << "按Ctrl+C可以中断测试程序" << std::endl;
    
    // 配置MQTT连接参数
    mqtt_config_t config = {};
    strncpy(config.broker, "tcp://1.92.111.153:1883", sizeof(config.broker) - 1);
    strncpy(config.client_id, "mqtt_tester", sizeof(config.client_id) - 1);
    strncpy(config.username, "Admin", sizeof(config.username) - 1);
    strncpy(config.password, "123456", sizeof(config.password) - 1);
    strncpy(config.pub_topic, "device/echo", sizeof(config.pub_topic) - 1);
    strncpy(config.sub_topic, "device/echo", sizeof(config.sub_topic) - 1);
    config.timeout_ms = 5000;
    config.qos = 1;
    config.keepalive_interval = 30;        // 保活间隔30秒
    config.reconnect_interval = 5;         // 重连间隔5秒
    config.max_reconnect_attempts = 10;    // 最大重连10次
    config.enable_auto_reconnect = 1;      // 启用自动重连

    // 初始化协议上下文
    proto_ctx_t ctx = {};
    ctx.type = PROTO_TYPE_MQTT;
    ctx.client = nullptr;
    ctx.config = &config;
    ctx.userdata = nullptr;

    // 初始化MQTT客户端
    std::cout << "初始化MQTT客户端..." << std::endl;
    TimeMeasure timer;
    timer.start();
    if (proto_driver_init(&ctx) != PROTO_SUCCESS) {
        std::cerr << "MQTT初始化失败" << std::endl;
        return -1;
    }
    timer.end();
    std::cout << "MQTT客户端初始化成功，耗时: " << timer.getDuration() << "μs" << std::endl;

    // 初始连接
    std::cout << "正在连接到MQTT服务器..." << std::endl;
    timer.start();
    
    int connect_attempts = 0;
    while (connect_attempts < MAX_RECONNECT_ATTEMPTS) {
        int result = proto_connect(&ctx);
        
        if (result == PROTO_SUCCESS) {
            usleep(500000); // 等待500ms
            connect_status_t status = proto_get_connect_status(&ctx);
            if (status == CON_OK) {
                std::cout << "初始连接成功! 尝试次数: " << (connect_attempts + 1) << std::endl;
                break;
            }
        }
        
        connect_attempts++;
        if (connect_attempts < MAX_RECONNECT_ATTEMPTS) {
            std::cout << "连接中... 尝试次数: " << connect_attempts << std::endl;
            usleep(RECONNECT_DELAY_MS * 1000);
        }
    }
    
    if (connect_attempts >= MAX_RECONNECT_ATTEMPTS) {
        std::cerr << "初始连接失败" << std::endl;
        proto_driver_release(&ctx);
        return -1;
    }
    
    timer.end();
    std::cout << "连接耗时: " << timer.getDuration() << "μs" << std::endl;
    std::cout << "================================================" << std::endl;

    // 创建线程
    pthread_t writer_tid, reader_tid, conn_mgr_tid, stats_tid;
    
    std::cout << "启动多线程测试..." << std::endl;
    
    // 启动发送线程
    if (pthread_create(&writer_tid, NULL, writer_thread, &ctx) != 0) {
        std::cerr << "创建发送线程失败" << std::endl;
        proto_driver_release(&ctx);
        return -1;
    }
    
    // 启动接收线程
    if (pthread_create(&reader_tid, NULL, reader_thread, &ctx) != 0) {
        std::cerr << "创建接收线程失败" << std::endl;
        g_running = false;
        pthread_join(writer_tid, NULL);
        proto_driver_release(&ctx);
        return -1;
    }
    
    // 启动连接管理线程
    if (pthread_create(&conn_mgr_tid, NULL, connection_manager_thread, &ctx) != 0) {
        std::cerr << "创建连接管理线程失败" << std::endl;
        g_running = false;
        pthread_join(writer_tid, NULL);
        pthread_join(reader_tid, NULL);
        proto_driver_release(&ctx);
        return -1;
    }
    
    // 启动统计线程
    if (pthread_create(&stats_tid, NULL, stats_thread, NULL) != 0) {
        std::cerr << "创建统计线程失败" << std::endl;
        g_running = false;
        pthread_join(writer_tid, NULL);
        pthread_join(reader_tid, NULL);
        pthread_join(conn_mgr_tid, NULL);
        proto_driver_release(&ctx);
        return -1;
    }
    
    std::cout << "所有线程启动成功，开始测试..." << std::endl;
    std::cout << "按Ctrl+C停止测试" << std::endl;
    
    // 主线程等待信号
    while (g_running) {
        usleep(1000000); // 每秒检查一次
    }
    
    std::cout << "\n================================================" << std::endl;
    std::cout << "正在停止所有线程..." << std::endl;
    
    // 等待所有线程结束
    pthread_join(writer_tid, NULL);
    pthread_join(reader_tid, NULL);
    pthread_join(conn_mgr_tid, NULL);
    pthread_join(stats_tid, NULL);
    
    std::cout << "断开连接并清理资源..." << std::endl;
    timer.start();
    proto_disconnect(&ctx);
    proto_driver_release(&ctx);
    timer.end();
    std::cout << "断开耗时: " << timer.getDuration() << "μs" << std::endl;

    std::cout << "================================================" << std::endl;
    if (g_interrupted) {
        std::cout << "测试程序被用户中断!" << std::endl;
    } else {
        std::cout << "测试程序执行完成!" << std::endl;
    }
    
    std::cout << "最终统计:" << std::endl;
    std::cout << "  发送消息: " << g_messages_sent.load() << std::endl;
    std::cout << "  接收消息: " << g_messages_received.load() << std::endl;
    std::cout << "  重连次数: " << g_reconnect_count.load() << std::endl;

    return g_interrupted ? 1 : 0;
}
