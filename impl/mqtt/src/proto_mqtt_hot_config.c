#include "proto_mqtt.h"
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include "common/utils/one_logger.hpp"
// 热配置上下文
typedef struct {
    char config_path[256];
    pthread_t monitor_thread;
    volatile int running;
    pthread_mutex_t mutex;
    time_t last_mtime;  // 上次文件修改时间
    
    // 回调函数
    void (*on_config_changed)(void);
    void *userdata;
} hot_config_ctx_t;

static hot_config_ctx_t g_hot_config = {0};

// 信号处理函数
static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        log_info("[HotConfig] Received signal %d, stopping hot config monitoring...\n", sig);
        g_hot_config.running = 0;
    }
}

/**
 * @brief 热配置监控线程函数
 * @param arg 线程参数
 * @return NULL
 */
static void* hot_config_monitor_thread(void* arg) {
    hot_config_ctx_t *ctx = (hot_config_ctx_t *)arg;
    
    log_info("[HotConfig] Monitor thread started, watching: {}", ctx->config_path);

    // 获取初始文件修改时间
    struct stat st;
    if (stat(ctx->config_path, &st) != 0) {
        log_info("[HotConfig] Config file not found, stopping monitoring: {}", strerror(errno));
        return NULL;
    }
    ctx->last_mtime = st.st_mtime;

    while (ctx->running) {
        // 检查配置文件是否还存在
        if (stat(ctx->config_path, &st) != 0) {
            log_info("[HotConfig] Config file not found, stopping monitoring: {}", strerror(errno));
            break;
        }
        
        // 检查文件修改时间是否发生变化
        if (st.st_mtime != ctx->last_mtime) {
            log_info("[HotConfig] Config file changed (mtime: {} -> {}), triggering reload...", 
                   ctx->last_mtime, st.st_mtime);
            
            // 更新修改时间
            ctx->last_mtime = st.st_mtime;
            
            // 再次检查文件是否存在
            if (stat(ctx->config_path, &st) == 0) {
                // 调用配置变化回调
                if (ctx->on_config_changed) {
                    ctx->on_config_changed();
                }
            } else {
                log_info("[HotConfig] Config file disappeared after change, stopping monitoring\n");
                break;
            }
        }
        
        // 休眠一段时间再检查
        usleep(1000000); // 1秒检查一次
    }
    
    log_info("[HotConfig] Monitor thread stopped\n");
    return NULL;
}

/**
 * @brief 初始化热配置监控
 * @param config_path 配置文件路径
 * @param on_changed 配置变化回调函数
 * @param userdata 用户数据
 * @return 0成功，-1失败
 */
int hot_config_init(const char *config_path, 
                   void (*on_changed)(void),
                   void *userdata) {
    if (!config_path || !on_changed) {
        log_info("[HotConfig] Invalid parameters\n");
        return -1;
    }
    
    // 初始化上下文
    memset(&g_hot_config, 0, sizeof(hot_config_ctx_t));
    strncpy(g_hot_config.config_path, config_path, sizeof(g_hot_config.config_path) - 1);
    g_hot_config.config_path[sizeof(g_hot_config.config_path) - 1] = '\0';
    g_hot_config.on_config_changed = on_changed;
    g_hot_config.userdata = userdata;
    g_hot_config.running = 1;
    
    // 初始化互斥锁
    if (pthread_mutex_init(&g_hot_config.mutex, NULL) != 0) {
        log_info("[HotConfig] Failed to initialize mutex\n");
        return -1;
    }
    
    // 检查配置文件是否存在
    struct stat st;
    if (stat(config_path, &st) != 0) {
        log_info("[HotConfig] Config file not found: {}", strerror(errno));
        pthread_mutex_destroy(&g_hot_config.mutex);
        return -1;
    }
    
    // 注意：信号处理由主程序统一管理，这里不设置信号处理
    
    // 创建监控线程
    if (pthread_create(&g_hot_config.monitor_thread, NULL, hot_config_monitor_thread, &g_hot_config) != 0) {
        log_info("[HotConfig] Failed to create monitor thread: {}", strerror(errno));
        pthread_mutex_destroy(&g_hot_config.mutex);
        return -1;
    }
    
    log_info("[HotConfig] Hot configuration monitoring initialized for: {}", config_path);
    return 0;
}

/**
 * @brief 停止热配置监控
 */
void hot_config_cleanup(void) {
    if (g_hot_config.running) {
        g_hot_config.running = 0;
        
        // 等待监控线程结束
        if (g_hot_config.monitor_thread) {
            pthread_join(g_hot_config.monitor_thread, NULL);
        }
        
        // 清理资源（文件修改时间检查不需要特殊清理）
        
        pthread_mutex_destroy(&g_hot_config.mutex);
        
        // 注意：信号处理由主程序统一管理
        
        log_info("[HotConfig] Hot configuration monitoring stopped\n");
    }
}

/**
 * @brief 自动清理函数（在程序退出时自动调用）
 */
static void auto_cleanup(void) {
    hot_config_cleanup();
}

// 注册退出时自动清理
__attribute__((destructor))
static void cleanup_on_exit(void) {
    auto_cleanup();
}

/**
 * @brief 检查热配置是否正在运行
 * @return 1正在运行，0未运行
 */
int hot_config_is_running(void) {
    return g_hot_config.running;
}

/**
 * @brief 停止热配置监控（供主程序调用）
 */
void hot_config_stop(void) {
    g_hot_config.running = 0;
}
