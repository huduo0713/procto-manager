#pragma once
#include "common/api/proto_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- BACnet 配置结构体 ---
// 用于定义我们要通信的目标设备
typedef struct {
    uint32_t target_device_id; // 目标设备的实例ID
    // 注意：BACnet/IP 通常是无连接的，IP地址可以通过 Who-Is 动态发现。
    // 如果需要直接点对点通信，可以额外增加一个 char* target_ip_address;
} bacnet_config_t;


#ifdef __cplusplus
}
#endif
