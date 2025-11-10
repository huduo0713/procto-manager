#include "proto_bacnet_internal.hpp"

#include "common/utils/one_logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <errno.h>

namespace bacnet {
namespace hot_config {

namespace {

struct HotConfigContext {
    std::string config_path;
    std::thread monitor_thread;
    std::atomic<bool> running{false};
    std::mutex mutex;
    time_t last_mtime{0};
    void (*callback)(void){nullptr};
    void *userdata{nullptr};
    uint32_t polling_interval_ms{1000};  // 默认 1 秒，可配置
};

HotConfigContext g_hot_config;

void monitor_loop()
{
    log_info("[BACnet][HotConfig] Monitor thread started, watching: {} (polling interval: {}ms)", 
             g_hot_config.config_path, g_hot_config.polling_interval_ms);

    int check_count = 0;
    while (g_hot_config.running.load(std::memory_order_acquire)) {
        struct stat st {};
        if (stat(g_hot_config.config_path.c_str(), &st) != 0) {
            log_warn("[BACnet][HotConfig] Config file not found: {}", std::strerror(errno));
            break;
        }

        check_count++;
        // 每10次检查打印一次状态（方便确认线程在运行）
        if (check_count % 10 == 0) {
            log_debug("[BACnet][HotConfig] Checking #{}: mtime={}, last_mtime={}", 
                     check_count, st.st_mtime, g_hot_config.last_mtime);
        }

        if (st.st_mtime != g_hot_config.last_mtime) {
            log_info("[BACnet][HotConfig] Config file changed ({} -> {}), invoking callback",
                     g_hot_config.last_mtime, st.st_mtime);
            g_hot_config.last_mtime = st.st_mtime;

            if (g_hot_config.callback) {
                g_hot_config.callback();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(g_hot_config.polling_interval_ms));
    }

    log_info("[BACnet][HotConfig] Monitor thread stopped");
}

void stop_locked()
{
    if (g_hot_config.running.exchange(false, std::memory_order_acq_rel)) {
        // 检查是否在监控线程自己调用 stop（避免死锁）
        if (g_hot_config.monitor_thread.joinable()) {
            auto current_thread_id = std::this_thread::get_id();
            auto monitor_thread_id = g_hot_config.monitor_thread.get_id();
            
            if (current_thread_id == monitor_thread_id) {
                // 监控线程自己调用 stop（如配置变化回调中），只设置停止标志，不 join
                log_warn("[BACnet][HotConfig] Stop called from monitor thread itself, detaching...");
                g_hot_config.monitor_thread.detach();
            } else {
                // 外部线程调用 stop，可以安全 join
                log_info("[BACnet][HotConfig] Stopping monitor thread...");
                g_hot_config.monitor_thread.join();
            }
        }
    }
}

} // anonymous namespace

/* -------------------------------------------------------------------------- */
/* 公开接口实现                                                               */
/* -------------------------------------------------------------------------- */

int init(const char *config_path, void (*on_changed)(void), void *userdata)
{
    if (!config_path || !on_changed) {
        log_error("[BACnet][HotConfig] Invalid parameters");
        return -1;
    }

    struct stat st {};
    if (stat(config_path, &st) != 0) {
        log_error("[BACnet][HotConfig] Config file not found: {}", std::strerror(errno));
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_hot_config.mutex);

    stop_locked();

    g_hot_config.config_path = config_path;
    g_hot_config.callback = on_changed;
    g_hot_config.userdata = userdata;
    g_hot_config.last_mtime = st.st_mtime;

    g_hot_config.running.store(true, std::memory_order_release);
    g_hot_config.monitor_thread = std::thread(monitor_loop);

    log_info("[BACnet][HotConfig] Monitoring started for {}", config_path);
    return 0;
}

void stop()
{
    std::lock_guard<std::mutex> lock(g_hot_config.mutex);
    stop_locked();
}

void cleanup()
{
    std::lock_guard<std::mutex> lock(g_hot_config.mutex);
    stop_locked();
    g_hot_config.monitor_thread = std::thread();
    g_hot_config.config_path.clear();
    g_hot_config.callback = nullptr;
    g_hot_config.userdata = nullptr;
    g_hot_config.last_mtime = 0;
}

bool is_running()
{
    return g_hot_config.running.load(std::memory_order_acquire);
}

void set_polling_interval(uint32_t interval_ms)
{
    std::lock_guard<std::mutex> lock(g_hot_config.mutex);
    g_hot_config.polling_interval_ms = (interval_ms > 0) ? interval_ms : 1000;
    log_info("[BACnet][HotConfig] Polling interval set to {}ms", g_hot_config.polling_interval_ms);
}

} // namespace hot_config
} // namespace bacnet

namespace {

struct AutoCleanup {
    ~AutoCleanup()
    {
        bacnet::hot_config::cleanup();
    }
};

static AutoCleanup g_auto_cleanup;

} // anonymous namespace
