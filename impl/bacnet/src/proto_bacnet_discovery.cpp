#include "proto_bacnet_internal.hpp"

namespace bacnet {

/* -------------------------------------------------------------------------- */
/* 设备发现实现                                                               */
/* -------------------------------------------------------------------------- */

proto_status_t discover_target_device(BacnetContext *context)
{
    if (!context) {
        return PROTO_ERROR_PARAM;
    }

    log_info("[BACnet] Starting device discovery for range {}-{}", 
             context->target_device_start, context->target_device_end);

    // 重置发现状态
    context->target_found.store(false, std::memory_order_release);

    // 设置连接状态
    set_connection_state(context, BACNET_CONN_CONNECTING);

    // 初始化数据链路层
    Device_Set_Object_Instance_Number(context->config.bacnet.local_device.instance_id);
    
    // 设置定时器
    mstimer apdu_timer{};
    mstimer datalink_timer{};
    uint32_t timeout_ms = context->config.bacnet.discovery.response_timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = bacnet::defaults::kDiscoveryTimeoutMs;
    }

    uint32_t datalink_timer_ms = context->config.bacnet.services.datalink_maintenance_ms;
    if (datalink_timer_ms == 0) {
        datalink_timer_ms = bacnet::defaults::kDatalinkMaintenanceMs;
    }

    mstimer_set(&apdu_timer, timeout_ms);
    mstimer_set(&datalink_timer, datalink_timer_ms);

    // 获取广播地址
    BACNET_ADDRESS dest{};
    datalink_get_broadcast_address(&dest);

    // 发送Who-Is请求（范围查询）
    uint32_t start_dev = context->target_device_start;
    uint32_t end_dev = context->target_device_end;
    log_info("[BACnet] Sending Who-Is for device range {}-{}", start_dev, end_dev);
    Send_WhoIs(start_dev, end_dev);

    // 等待I-Am响应
    while (!context->target_found.load(std::memory_order_acquire)) {
        // 检查超时
        if (mstimer_expired(&apdu_timer)) {
            log_error("[BACnet] Device discovery timeout for range {}-{}", start_dev, end_dev);
            set_connection_state(context, BACNET_CONN_DISCONNECTED);
            return PROTO_ERROR_CONNECT;
        }

        // 检查停止信号
        if (context->worker_stop.load(std::memory_order_acquire)) {
            log_info("[BACnet] Device discovery interrupted by stop signal");
            set_connection_state(context, BACNET_CONN_DISCONNECTED);
            return PROTO_ERROR_CONNECT;
        }

        // 接收并处理数据包
        BACNET_ADDRESS src{};
        uint16_t pdu_len = datalink_receive(&src, context->rx_buffer, MAX_MPDU, 100);
        if (pdu_len > 0) {
            npdu_handler(&src, context->rx_buffer, pdu_len);
        }

        // 数据链路层维护
        if (mstimer_expired(&datalink_timer)) {
            datalink_maintenance_timer(mstimer_interval(&datalink_timer) / 1000);
            mstimer_reset(&datalink_timer);
        }
    }

    // 发现成功
    set_connection_state(context, BACNET_CONN_CONNECTED);
    context->reconnect_attempts.store(0, std::memory_order_release);
    
    log_info("[BACnet] Device discovery successful for range {}-{}", start_dev, end_dev);
    return PROTO_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 工作线程循环（处理协议栈事件和超时检测）                                    */
/* -------------------------------------------------------------------------- */

void worker_loop_function(BacnetContext *context)
{
    if (!context) {
        return;
    }

    log_info("[BACnet] Worker thread started");

    time_t last_seconds = time(nullptr);

    while (!context->worker_stop.load(std::memory_order_acquire)) {
        // 处理写队列（已移除，写操作现在直接执行）
        // 写操作不需要队列，因为它们是同步的，回调函数直接处理ACK

        // 更新定时器 - 使用毫秒级精度
        static auto last_timer_update = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_timer_update).count();
        
        if (elapsed >= 100) {  // 每100ms更新一次定时器
            tsm_timer_milliseconds(static_cast<uint16_t>(elapsed));
            datalink_maintenance_timer(elapsed / 1000);
            last_timer_update = now;
        }

        // 定期清理过期请求（每5秒一次）
        static auto last_cleanup = std::chrono::steady_clock::now();
        auto cleanup_now = std::chrono::steady_clock::now();
        auto elapsed_since_cleanup = std::chrono::duration_cast<std::chrono::seconds>(
            cleanup_now - last_cleanup).count();
        
        if (elapsed_since_cleanup >= 5) {
            cleanup_stale_requests(context);
            last_cleanup = cleanup_now;
        }

        // 定期打印请求哈希表统计信息（每30秒一次）
        static auto last_stats = std::chrono::steady_clock::now();
        auto stats_now = std::chrono::steady_clock::now();
        auto elapsed_since_stats = std::chrono::duration_cast<std::chrono::seconds>(
            stats_now - last_stats).count();
        
        if (elapsed_since_stats >= 30) {
            size_t object_states_count = 0;
            size_t cached_objects = 0;
            size_t active_requests = 0;
            size_t invoke_id_mappings = 0;
            size_t write_count = 0;
            size_t write_completed = 0;
            
            {
                std::lock_guard<std::mutex> lock(context->object_states_mutex);
                object_states_count = context->object_states.size();
                for (const auto &pair : context->object_states) {
                    if (pair.second.has_valid_cache) cached_objects++;
                    if (pair.second.active_invoke_id != 0) active_requests++;
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(context->invoke_id_to_key_mutex);
                invoke_id_mappings = context->invoke_id_to_key.size();
            }
            
            {
                std::lock_guard<std::mutex> lock(context->write_queue_mutex);
                write_count = context->write_queue.size();
                for (const auto &pair : context->write_queue) {
                    if (pair.second.is_completed) write_completed++;
                }
            }
            
            if (object_states_count > 0 || write_count > 0) {
                log_info("[BACnet] Cache stats - Objects: {} (cached: {}, active: {}), InvokeID mappings: {}, Write queue: {}/{}",
                         object_states_count, cached_objects, active_requests, 
                         invoke_id_mappings, write_completed, write_count);
            }
            
            last_stats = stats_now;
        }

        // 接收并处理数据包
        BACNET_ADDRESS src{};
        uint16_t pdu_len = datalink_receive(&src, context->rx_buffer, MAX_MPDU, 100);
        if (pdu_len > 0) {
            npdu_handler(&src, context->rx_buffer, pdu_len);
        }

        // 自动重连检查（每5秒检查一次）
        static time_t last_reconnect_check = 0;
        time_t current_time = time(nullptr);
        if (current_time - last_reconnect_check >= 5) {
            check_and_reconnect_if_needed(context);
            last_reconnect_check = current_time;
        }

        // 短暂休眠，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    log_info("[BACnet] Worker thread stopped");
}

/* -------------------------------------------------------------------------- */
/* 检查连接状态并在需要时自动重连                                             */
/* -------------------------------------------------------------------------- */

void check_and_reconnect_if_needed(BacnetContext *context)
{
    if (!context) {
        return;
    }

    bacnet_connection_state_t current_state = get_connection_state(context);
    
    // 只在断开连接状态下尝试重连
    if (current_state != BACNET_CONN_DISCONNECTED) {
        return;
    }

    int attempts = context->reconnect_attempts.load(std::memory_order_acquire);
    uint8_t max_attempts = context->config.bacnet.connection.max_reconnect_attempts;
    uint32_t base_interval_ms = context->config.bacnet.connection.reconnect_interval_ms;
    
    // 检查是否达到最大重连次数
    if (attempts >= max_attempts) {
        return;
    }

    // 指数退避策略：基于配置的重连间隔计算
    static time_t last_reconnect_attempt = 0;
    time_t current_time = time(nullptr);
    
    // 退避时间 = base_interval * 2^attempts (毫秒转秒)，最大60秒
    int backoff_seconds = (std::min)(60, static_cast<int>((base_interval_ms / 1000) * (1 << attempts)));
    if (current_time - last_reconnect_attempt < backoff_seconds) {
        return; // 还未到重连时间
    }

    log_info("[BACnet] Attempting automatic reconnection (attempt {}/{}, backoff: {}s)",
             attempts + 1, max_attempts, backoff_seconds);
    
    last_reconnect_attempt = current_time;
    
    // 尝试重新发现目标设备
    proto_status_t result = discover_target_device(context);
    
    if (result == PROTO_SUCCESS) {
        log_info("[BACnet] Automatic reconnection successful");
        context->reconnect_attempts.store(0, std::memory_order_release);
        set_connection_state(context, BACNET_CONN_CONNECTED);
        
        // 触发重连成功回调
        trigger_callback(context, "reconnect", PROTO_SUCCESS);
    } else {
        context->reconnect_attempts.fetch_add(1, std::memory_order_acq_rel);
        
        if (context->reconnect_attempts.load(std::memory_order_acquire) >= 
            context->config.bacnet.connection.max_reconnect_attempts) {
            log_error("[BACnet] Maximum reconnection attempts reached, giving up");
            trigger_callback(context, "reconnect", PROTO_ERROR_CONNECT);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 清理过期请求（防止内存泄漏）                                               */
/* -------------------------------------------------------------------------- */

void cleanup_stale_requests(BacnetContext *context) {
    auto now = std::chrono::steady_clock::now();
    
    // 清理反向映射表中超时的invoke_id
    std::vector<uint8_t> stale_invoke_ids;
    {
        std::lock_guard<std::mutex> lock(context->invoke_id_to_key_mutex);
        std::lock_guard<std::mutex> lock_states(context->object_states_mutex);
        
        for (auto it = context->invoke_id_to_key.begin(); it != context->invoke_id_to_key.end(); ) {
            uint8_t invoke_id = it->first;
            const ObjectKey &key = it->second;
            
            // 检查对象状态
            auto state_it = context->object_states.find(key);
            if (state_it != context->object_states.end()) {
                auto &state = state_it->second;
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - state.timestamp).count();
                
                // 超时阈值：2倍请求超时时间（兜底保护）
                uint32_t timeout_threshold = context->config.bacnet.services.read_timeout_ms * 2;
                if (timeout_threshold == 0) timeout_threshold = 12000;
                
                if (elapsed > timeout_threshold && state.active_invoke_id == invoke_id) {
                    log_warn("[BACnet] Cleaning stale read request (invoke_id: {}, elapsed: {}ms, threshold: {}ms)",
                             invoke_id, elapsed, timeout_threshold);
                    
                    // 清除活跃请求标记
                    state.active_invoke_id = 0;
                    state.status = PROTO_TIMEOUT;
                    
                    // 释放TSM资源
                    if (invoke_id != 0) {
                        tsm_free_invoke_id(invoke_id);
                    }
                    
                    // 删除反向映射
                    it = context->invoke_id_to_key.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
    
    // 清理写队列
    {
        std::lock_guard<std::mutex> lock(context->write_queue_mutex);
        for (auto it = context->write_queue.begin(); it != context->write_queue.end(); ) {
            auto &item = it->second;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - item.timestamp).count();
            
            uint32_t timeout_threshold = 12000;  // 写操作默认12秒
            
            if (elapsed > timeout_threshold) {
                log_warn("[BACnet] Cleaning stale write request (invoke_id: {}, elapsed: {}ms)",
                         item.invoke_id, elapsed);
                
                if (!item.is_completed) {
                    item.status = PROTO_TIMEOUT;
                    item.is_completed = true;
                    
                    if (item.invoke_id != 0) {
                        tsm_free_invoke_id(item.invoke_id);
                    }
                }
                
                it = context->write_queue.erase(it);
            } else {
                ++it;
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 工作线程主循环                                                             */
/* -------------------------------------------------------------------------- */

void start_worker_thread(BacnetContext *context)
{
    if (!context) {
        return;
    }

    bool expected = false;
    if (context->worker_running.compare_exchange_strong(expected, true, 
                                                         std::memory_order_acq_rel)) {
        context->worker_stop.store(false, std::memory_order_release);
        
        // 使用智能指针管理线程
        context->worker_thread = std::make_unique<std::thread>(worker_loop_function, context);
        
        log_info("[BACnet] Worker thread started successfully");
    } else {
        log_warn("[BACnet] Worker thread already running");
    }
}

/* -------------------------------------------------------------------------- */
/* 停止工作线程                                                               */
/* -------------------------------------------------------------------------- */

void stop_worker_thread(BacnetContext *context)
{
    if (!context) {
        return;
    }

    if (!context->worker_running.load(std::memory_order_acquire)) {
        return;
    }

    log_info("[BACnet] Stopping worker thread...");

    // 设置停止标志
    context->worker_stop.store(true, std::memory_order_release);

    // 等待线程结束
    if (context->worker_thread && context->worker_thread->joinable()) {
        context->worker_thread->join();
    }

    // 重置线程指针
    context->worker_thread.reset();
    context->worker_running.store(false, std::memory_order_release);

    log_info("[BACnet] Worker thread stopped successfully");
}

} // namespace bacnet
