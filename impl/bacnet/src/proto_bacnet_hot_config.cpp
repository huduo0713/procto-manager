#include "proto_bacnet.h"

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

namespace {

struct HotConfigContext {
    std::string config_path;
    std::thread monitor_thread;
    std::atomic<bool> running{false};
    std::mutex mutex;
    time_t last_mtime{0};
    void (*callback)(void){nullptr};
    void *userdata{nullptr};
};

HotConfigContext g_hot_config;

void monitor_loop()
{
    log_info("[BACnet][HotConfig] Monitor thread started, watching: {}", g_hot_config.config_path);

    while (g_hot_config.running.load(std::memory_order_acquire)) {
        struct stat st {};
        if (stat(g_hot_config.config_path.c_str(), &st) != 0) {
            log_warn("[BACnet][HotConfig] Config file not found: {}", std::strerror(errno));
            break;
        }

        if (st.st_mtime != g_hot_config.last_mtime) {
            log_info("[BACnet][HotConfig] Config file changed ({} -> {}), invoking callback",
                     g_hot_config.last_mtime, st.st_mtime);
            g_hot_config.last_mtime = st.st_mtime;

            if (g_hot_config.callback) {
                g_hot_config.callback();
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    log_info("[BACnet][HotConfig] Monitor thread stopped");
}

void stop_locked()
{
    if (g_hot_config.running.exchange(false, std::memory_order_acq_rel)) {
        if (g_hot_config.monitor_thread.joinable()) {
            g_hot_config.monitor_thread.join();
        }
    }
}

} // namespace

extern "C" {

int bacnet_hot_config_init(const char *config_path, void (*on_changed)(void), void *userdata)
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

void bacnet_hot_config_cleanup(void)
{
    std::lock_guard<std::mutex> lock(g_hot_config.mutex);
    stop_locked();
    g_hot_config.monitor_thread = std::thread();
    g_hot_config.config_path.clear();
    g_hot_config.callback = nullptr;
    g_hot_config.userdata = nullptr;
    g_hot_config.last_mtime = 0;
}

int bacnet_hot_config_is_running(void)
{
    return g_hot_config.running.load(std::memory_order_acquire) ? 1 : 0;
}

void bacnet_hot_config_stop(void)
{
    std::lock_guard<std::mutex> lock(g_hot_config.mutex);
    stop_locked();
}

} // extern "C"

namespace {

struct AutoCleanup {
    ~AutoCleanup()
    {
        bacnet_hot_config_cleanup();
    }
};

static AutoCleanup g_auto_cleanup;

} // namespace
