#include <stdio.h>
#include <string.h>
#include <unistd.h> // for sleep
#include <time.h>
#include <stdlib.h>

#include "proto_bacnet.h" // 包含我们之前定义的 BACnet 特定结构体
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h" // 包含通用驱动接口定义


#include "bacnet/basic/object/device.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"

#include "bacnet/whois.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/rp.h"      // ReadProperty
#include "bacnet/wp.h"      // WriteProperty
#include "bacnet/iam.h"     // I-Am decoding
#include "bacnet/apdu.h"    // APDU handling (invoke id)
#include "bacnet/bacapp.h"  // For BACNET_APPLICATION_DATA_VALUE and decoding
#include "bacnet/datalink/bip.h"
#include "bacnet/bactext.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
/*
 * ============================================================================
 * 内部辅助结构体和状态 (Internal Helper Structs and State)
 * ============================================================================
 */

/**
 * @brief 内部 BACnet 请求结构体
 * @details 这是一个在驱动内部使用的私有结构体。它的作用是将上层通用的
 * `proto_request_t` “翻译”成 bacnet-stack 库函数所需的、
 * 具体的、类型化的参数集合。它不应该被暴露在 .h 头文件中。
 */
typedef struct {
    BACNET_OBJECT_TYPE object_type;
    uint32_t object_instance;
    BACNET_PROPERTY_ID property_id;
} internal_bacnet_address_t;

/*
 * ============================================================================
 * 内部全局变量 (Internal Global State)
 * 这些变量用于在驱动的整个生命周期内保存状态。
 * ============================================================================
 */

// 全局标志位，用于标记我们是否已经成功发现了目标设备
static bool g_target_device_found = false;
// 全局变量，存储我们在 connect 时要寻找的目标设备的ID
static uint32_t g_target_device_id = 0;
// 全局接收缓冲区，用于存放 datalink_receive 收到的最新报文
static uint8_t g_RxBuf[MAX_MPDU] = {0};
// 最新接收报文的长度
static uint16_t g_RxBuf_Len = 0;

/*
 * ============================================================================
 * 回调函数和辅助函数 (Callback and Helper Functions)
 * ============================================================================
 */


/*
 * ============================================================================
 * 回调函数 (Callback Functions)
 * 这是BACnet协议栈异步工作的核心。我们预先定义好这些函数，
 * 然后告诉协议栈在接收到特定类型的报文时来调用它们。
 * ============================================================================
 */
/**
 * @brief I-Am 报文回调处理函数
 * @details 这是一个异步回调函数。当网络上任何一个设备响应 I-Am 报文时，
 * bacnet-stack 库都会自动调用此函数。我们的任务是在这里检查这个
 * 响应是不是我们正在寻找的那个设备。
 * @param service_request 指向收到的原始 APDU 报文数据的指针
 * @param service_len 报文数据的长度
 * @param src 指向源地址结构体的指针，包含了响应设备的IP地址和端口等信息
 */
static void my_i_am_handler(
    uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    int len = 0;
    uint32_t device_id = 0;
    unsigned max_apdu = 0;
    int segmentation = 0;
    uint16_t vendor_id = 0;

    // 调用库函数 iam_decode_service_request 来从原始字节流中解析出有意义的数据。
    len = iam_decode_service_request(
        service_request, &device_id, &max_apdu, &segmentation, &vendor_id);
    
    // 检查解码是否成功，并且设备ID是否是我们正在寻找的那个
    if (len > 0 && device_id == g_target_device_id) {
        // 关键步骤：调用库函数 address_add 将设备ID和其网络地址(src)的映射关系
        // 添加到 bacnet-stack 内部维护的一个全局地址缓存表中。
        // 之后所有需要与此设备通信的函数，只需提供device_id即可，库会自动查找其IP地址。
        address_add(device_id, max_apdu, src);
        // 设置全局标志位，通知主流程我们已经找到了目标。
        g_target_device_found = true;
        printf("驱动: 发现目标设备 %u\n", device_id);
    }
}

/*
 * ============================================================================
 * 辅助函数 (Helper Functions)
 * ============================================================================
 */

/**
 * @brief 解析上层应用传入的资源字符串
 * @details 将 "analog-input:0:present-value" 这样的字符串，解析成
 * BACnet 库能理解的对象类型、实例ID和属性ID。
 * @param resource_name [in] 输入的资源字符串
 * @param addr [out] 指向一个 internal_bacnet_address_t 结构体的指针，用于存放解析结果
 * @return true 如果解析成功, false 如果失败
 */
static bool parse_bacnet_resource(const char *resource_name, internal_bacnet_address_t *addr) {
    char buffer[64];
    strncpy(buffer, resource_name, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token;
    token = strtok(buffer, ":");
    if (!token || !bactext_object_type_strtol(token, &addr->object_type)) return false;

    token = strtok(NULL, ":");
    if (!token) return false;
    addr->object_instance = strtoul(token, NULL, 10);

    token = strtok(NULL, ":");
    if (!token || !bactext_property_strtol(token, &addr->property_id)) return false;

    return true;
}

/*
 * ============================================================================
 * 通用驱动接口实现 (Public Driver Interface Implementation)
 * ============================================================================
 */

/**
 * @brief 初始化BACnet协议驱动
 * @param ctx [in] 指向通用协议上下文的指针
 * @return PROTO_SUCCESS 或错误码
 */
int proto_driver_init(proto_ctx_t *ctx) {
    if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;
    
    // 关键步骤：注册我们的 I-Am 回调函数。
    // 这告诉协议栈，当收到一个 Unconfirmed-Request 类型的、服务是 I_AM 的报文时，
    // 就去调用 my_i_am_handler 函数。
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, my_i_am_handler);
    
    // bip_init 是 BACnet/IP 数据链路层的初始化函数。
    // 传入 NULL 表示让它自动绑定到系统默认的网络接口上。
    if (!bip_init(NULL)) return PROTO_ERROR_INIT;
    
    printf("BACnet 驱动初始化成功。\n");
    return PROTO_SUCCESS;
}

/**
 * @brief 释放BACnet协议驱动占用的资源
 * @param ctx [in] 指向通用协议上下文的指针
 */
void proto_driver_release(proto_ctx_t *ctx) {
    if (ctx && ctx->type == PROTO_TYPE_BACNET) {
        // bip_cleanup 是 bip_init 的配对函数，用于关闭socket，释放资源。
        bip_cleanup();
        printf("BACnet 驱动已释放。\n");
    }
    else {
        printf("驱动释放失败：上下文无效或不是BACnet类型。\n");
    }
}

/**
 * @brief “连接”到目标设备
 * @details BACnet/IP 本质上是无连接的。这里的“连接”操作，实际上是通过发送 Who-Is
 * 广播来发现目标设备，并将其网络地址缓存起来，为后续的读写操作做准备。
 * @param ctx [in] 指向通用协议上下文的指针
 * @return PROTO_SUCCESS 或错误码
 */
int proto_connect(proto_ctx_t *ctx) {
    if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;
    
    bacnet_config_t *config = (bacnet_config_t *)ctx->config;
    g_target_device_id = config->target_device_id;
    g_target_device_found = false;

    printf("正在通过 Who-Is 发现设备 %u...\n", g_target_device_id);
    // Send_WhoIs 是库提供的函数，用于构建并发送一个 Who-Is 报文。
    // 两个参数分别代表要寻找的设备ID的最小和最大范围。这里我们只找一个特定设备。
    Send_WhoIs(g_target_device_id, g_target_device_id);

    // 这是一个简化的同步等待循环，用于接收网络报文。
    // 在一个真正的多线程应用中，这里应该是一个独立的、持续运行的接收线程。
    for (int i = 0; i < 10; ++i) {
        if (g_target_device_found) {
            printf("设备连接成功 (地址已缓存)。\n");
            return PROTO_SUCCESS;
        }
        usleep(200 * 1000); // 暂停 200ms，避免CPU空转
        BACNET_ADDRESS src;
        uint8_t buffer[MAX_MPDU];
        // datalink_receive 是数据链路层的核心函数，用于从网卡接收一个BACnet包。
        uint16_t pdu_len = datalink_receive(&src, buffer, MAX_MPDU, 0);
        if (pdu_len) {
            // npdu_handler 是协议栈的入口。收到任何包都“喂”给它，
            // 它会负责解析并触发相应的回调函数。
            npdu_handler(&src, buffer, pdu_len);
        }
    }

    printf("连接失败：未发现目标设备。\n");
    return PROTO_ERROR_CONNECT;
}

/**
 * @brief 断开与设备的“连接”
 * @details 主要是从本地的地址缓存中移除设备信息。
 * @param ctx [in] 指向通用协议上下文的指针
 */
void proto_disconnect(proto_ctx_t *ctx) {
    if (ctx && ctx->type == PROTO_TYPE_BACNET) {
        // address_remove_device 从地址缓存中移除指定设备，释放内存。
        address_remove_device(g_target_device_id);
        g_target_device_found = false;
        printf("BACnet 断开连接 (清理本地状态)。\n");
    } else {
        printf("驱动断开失败：上下文无效或不是BACnet类型。\n");
    }
}

/**
 * @brief 读取一个BACnet属性
 * @param ctx [in] 指向通用协议上下文的指针
 * @param req [in, out] 指向通用请求结构体的指针。resource_name 作为输入，
 * 读取到的值将被写入 req->value 指向的内存。
 * @return PROTO_SUCCESS 或错误码
 */

// int proto_read(proto_ctx_t *ctx, proto_request_t *req) {
//     if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;
    
//     internal_bacnet_request_t bac_req = {0};
//     bac_req.array_index = BACNET_ARRAY_ALL;

//     if (!parse_bacnet_resource(req->resource_name, &bac_req)) {
//         return PROTO_ERROR_PARAM;
//     }
    
//     BACNET_ADDRESS dest;
//     unsigned max_apdu;
//     uint8_t invoke_id;
//     bool status;

//     // 从地址缓存中获取目标设备的网络地址
//     if (!address_get_by_device(g_target_device_id, &max_apdu, &dest)) {
//         return PROTO_ERROR_CONNECT; // 目标设备地址未知
//     }

//     // 关键修复：使用正确的库函数 Send_Read_Property_Request 来发送请求
//     invoke_id = Send_Read_Property_Request(g_target_device_id,
//         bac_req.object_type, bac_req.object_instance,
//         bac_req.property_id, bac_req.array_index);

//     if (invoke_id == 0) return PROTO_ERROR_READ;

//     // 实现一个更健壮的同步等待响应机制
//     time_t start_time = time(NULL);
//     while ((time(NULL) - start_time) < 2) { // 等待最多2秒
//         BACNET_ADDRESS src;
//         g_RxBuf_Len = datalink_receive(&src, g_RxBuf, MAX_MPDU, 100); // 阻塞等待100ms
//         if (!g_RxBuf_Len) continue;

//         // 检查收到的包是不是我们想要的 Complex-ACK
//         if ((g_RxBuf[0] == PDU_TYPE_COMPLEX_ACK) && (apdu_decode_invoke_id(g_RxBuf, g_RxBuf_Len) == invoke_id)) {
//             // 关键修复：使用正确的库函数 rp_ack_decode_service_request 来解码响应
//             status = rp_ack_decode_service_request(g_RxBuf, g_RxBuf_Len, &bac_req);
//             if (status) {
//                 if (bac_req.value.tag == BACNET_APPLICATION_TAG_REAL && req->value != NULL) {
//                     *(float*)req->value = bac_req.value.type.Real;
//                     return PROTO_SUCCESS;
//                 }
//             }
//             return PROTO_ERROR_READ; // 解码失败或类型不匹配
//         }
//     }
    
//     return PROTO_ERROR_READ; // 超时
// }

int proto_read(proto_ctx_t *ctx, proto_request_t *req) {
    if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;
    
    internal_bacnet_address_t bac_addr;
    if (!parse_bacnet_resource(req->resource_name, &bac_addr)) {
        return PROTO_ERROR_PARAM;
    }
    
    uint8_t invoke_id = Send_Read_Property_Request(g_target_device_id,
        bac_addr.object_type, bac_addr.object_instance,
        bac_addr.property_id, BACNET_ARRAY_ALL);

    if (invoke_id == 0) return PROTO_ERROR_READ;

    time_t start_time = time(NULL);
    while ((time(NULL) - start_time) < 2) {
        BACNET_ADDRESS src;
        g_RxBuf_Len = datalink_receive(&src, g_RxBuf, MAX_MPDU, 100);
        if (!g_RxBuf_Len) continue;

        if ((g_RxBuf[0] == PDU_TYPE_COMPLEX_ACK) && (g_RxBuf[1] == invoke_id)) {
            // 关键修复 1：使用库定义的 BACNET_READ_PROPERTY_DATA 结构体来接收解码结果
            BACNET_READ_PROPERTY_DATA rp_data;
            int len = rp_ack_decode_service_request(g_RxBuf, g_RxBuf_Len, &rp_data);
            
            if (len > 0) {
                // 关键修复 2：rp_data.application_data 是一个原始字节指针，
                // 我们需要调用 bacapp_decode_application_data 将其解码到 value 结构体中。
                BACNET_APPLICATION_DATA_VALUE value;
                int dec_len = bacapp_decode_application_data(rp_data.application_data, (uint8_t)rp_data.application_data_len, &value);

                if (dec_len > 0 && value.tag == BACNET_APPLICATION_TAG_REAL && req->value != NULL) {
                    *(float*)req->value = value.type.Real;
                    return PROTO_SUCCESS;
                }
            }
            return PROTO_ERROR_READ; // 解码失败或类型不匹配
        }
    }
    
    return PROTO_ERROR_READ; // 超时
}

/**
 * @brief 写入一个BACnet属性
 * @param ctx [in] 指向通用协议上下文的指针
 * @param req [in] 指向通用请求结构体的指针。resource_name 和 req->value 作为输入。
 * @return PROTO_SUCCESS 或错误码
 */

// int proto_write(proto_ctx_t *ctx, proto_request_t *req) {
//     if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;

//     internal_bacnet_request_t bac_req = {0};
//     bac_req.array_index = BACNET_ARRAY_ALL;
//     bac_req.priority = 16;

//     if (!parse_bacnet_resource(req->resource_name, &bac_req)) {
//         return PROTO_ERROR_PARAM;
//     }

//     if (req->value == NULL) return PROTO_ERROR_PARAM;
//     bac_req.value.tag = BACNET_APPLICATION_TAG_REAL;
//     bac_req.value.type.Real = *(float*)req->value;
    
//     BACNET_ADDRESS dest;
//     unsigned max_apdu;
//     uint8_t invoke_id;

//     if (!address_get_by_device(g_target_device_id, &max_apdu, &dest)) {
//         return PROTO_ERROR_CONNECT;
//     }

//     // 关键修复：使用正确的库函数 Send_Write_Property_Request
//     invoke_id = Send_Write_Property_Request(g_target_device_id,
//         bac_req.object_type, bac_req.object_instance,
//         bac_req.property_id, &bac_req.value, bac_req.priority, bac_req.array_index);

//     if (invoke_id == 0) return PROTO_ERROR_WRITE;
    
//     // 简化的同步等待 Simple-ACK 响应
//     time_t start_time = time(NULL);
//     while ((time(NULL) - start_time) < 2) {
//         BACNET_ADDRESS src;
//         g_RxBuf_Len = datalink_receive(&src, g_RxBuf, MAX_MPDU, 100);
//         if (!g_RxBuf_Len) continue;

//         if ((g_RxBuf[0] == PDU_TYPE_SIMPLE_ACK) && (apdu_decode_invoke_id(g_RxBuf, g_RxBuf_Len) == invoke_id)) {
//             return PROTO_SUCCESS; // 收到了正确的 Simple-ACK
//         }
//     }
    
//     return PROTO_ERROR_WRITE; // 超时
// }

int proto_write(proto_ctx_t *ctx, proto_request_t *req) {
    if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;

    internal_bacnet_address_t bac_addr;
    if (!parse_bacnet_resource(req->resource_name, &bac_addr)) {
        return PROTO_ERROR_PARAM;
    }

    if (req->value == NULL) return PROTO_ERROR_PARAM;
    
    BACNET_APPLICATION_DATA_VALUE value;
    value.tag = BACNET_APPLICATION_TAG_REAL;
    value.type.Real = *(float*)req->value;
    
    // 关键修复：使用正确的库函数 Send_Write_Property_Request
    uint8_t invoke_id = Send_Write_Property_Request(g_target_device_id,
        bac_addr.object_type, bac_addr.object_instance,
        bac_addr.property_id, &value, 16, BACNET_ARRAY_ALL);

    if (invoke_id == 0) return PROTO_ERROR_WRITE;
    
    time_t start_time = time(NULL);
    while ((time(NULL) - start_time) < 2) {
        BACNET_ADDRESS src;
        g_RxBuf_Len = datalink_receive(&src, g_RxBuf, MAX_MPDU, 100);
        if (!g_RxBuf_Len) continue;

        if ((g_RxBuf[0] == PDU_TYPE_SIMPLE_ACK) && (g_RxBuf[1] == invoke_id)) {
            return PROTO_SUCCESS;
        }
    }
    
    return PROTO_ERROR_WRITE; // 超时
}
