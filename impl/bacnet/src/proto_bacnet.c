#include <stdio.h>
#include <string.h>
#include <unistd.h> // for sleep
#include <time.h>
#include <stdlib.h>

#include "proto_bacnet.h" // 包含我们之前定义的 BACnet 特定结构体
#include "common/api/proto_common.h"
#include "common/api/proto_driver.h" // 包含通用驱动接口定义

/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bactext.h"
#include "bacnet/bacerror.h"
#include "bacnet/iam.h"
#include "bacnet/arf.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/whois.h"
#include "bacnet/version.h"
/* some demo stuff needed */
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/sys/mstimer.h"
#include "bacnet/rp.h"      // ReadProperty
#include "bacnet/wp.h"      // WriteProperty
#include "bacnet/apdu.h"    // APDU handling (invoke id)
#include "bacnet/bacapp.h"  // For BACNET_APPLICATION_DATA_VALUE and decoding
#include "bacnet/datalink/bip.h"
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
/* 调试信息打印 */
static bool BACnet_Debug_Enabled = true;
/* 错误检测标志 */
static bool Error_Detected = false;

/* the invoke id is needed to filter incoming messages */
static uint8_t Request_Invoke_ID = 0;
static BACNET_ADDRESS Target_Address;

/* 读取操作的结果存储 */
static bool Read_Property_Result_Available = false;
static BACNET_APPLICATION_DATA_VALUE Read_Property_Value = {0};
static proto_request_t *Current_Request = NULL;

/* 写操作的结果存储 */
static bool Write_Property_Success = false;

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
    if (BACnet_Debug_Enabled)
    {
        fprintf(stderr, "Received I-Am Request"); /* 打印接收到 I-Am 请求的调试信息 */
    }
    // 检查解码是否成功，并且设备ID是否是我们正在寻找的那个
    if (len != -1 && device_id == g_target_device_id) {
        if (BACnet_Debug_Enabled)
        {
            fprintf(stderr, " from %lu, MAC = ", (unsigned long)device_id); /* 打印设备 ID */
            if ((src->mac_len == 6) && (src->len == 0)) /* 如果是 IP 地址 */
            {
                fprintf(
                    stderr, "%u.%u.%u.%u %02X%02X\n", (unsigned)src->mac[0],
                    (unsigned)src->mac[1], (unsigned)src->mac[2],
                    (unsigned)src->mac[3], (unsigned)src->mac[4],
                    (unsigned)src->mac[5]);
            }
            else /* 如果是其他 MAC 地址 */
            {
                for (unsigned i = 0; i < src->mac_len; i++)
                {
                    fprintf(stderr, "%02X", (unsigned)src->mac[i]);
                    if (i < (src->mac_len - 1))
                    {
                        fprintf(stderr, ":");
                    }
                }
                fprintf(stderr, "\n");
            }
        }
            
        // 关键步骤：调用库函数 address_add 将设备ID和其网络地址(src)的映射关系
        // 添加到 bacnet-stack 内部维护的一个全局地址缓存表中。
        // 之后所有需要与此设备通信的函数，只需提供device_id即可，库会自动查找其IP地址。
        address_add(device_id, max_apdu, src);
        // 设置全局标志位，通知主流程我们已经找到了目标。
        g_target_device_found = true;
        printf("驱动: 发现目标设备 %u\n", device_id);
    }
}

/** Handler for a ReadProperty ACK.
 * @ingroup DSRP
 * Doesn't actually do anything, except, for debugging, to
 * print out the ACK data of a matching request.
 *
 * @param service_request [in] The contents of the service request.
 * @param service_len [in] The length of the service_request.
 * @param src [in] BACNET_ADDRESS of the source of the message
 * @param service_data [in] The BACNET_CONFIRMED_SERVICE_DATA information
 *                          decoded from the APDU header of this message.
 */
static void My_Read_Property_Ack_Handler(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    int len = 0;
    BACNET_READ_PROPERTY_DATA data;

    if (address_match(&Target_Address, src) &&
        (service_data->invoke_id == Request_Invoke_ID)) {
        len = rp_ack_decode_service_request(service_request, service_len, &data);
        if (len < 0) {
            printf("解码失败!\n");
            Error_Detected = true;
        } else {
            // 打印调试信息（可选）
            if (BACnet_Debug_Enabled) {
                rp_ack_print_data(&data);
            }
            
            // 解码应用数据到值结构体中
            if (data.application_data && data.application_data_len > 0) {
                int dec_len = bacapp_decode_application_data(
                    data.application_data, 
                    (uint8_t)data.application_data_len, 
                    &Read_Property_Value);
                
                if (dec_len > 0) {
                    Read_Property_Result_Available = true;
                    printf("成功读取属性值，类型标签: %d\n", Read_Property_Value.tag);
                    
                    // 如果有当前请求且需要返回值，尝试将数据复制到请求结构中
                    if (Current_Request && Current_Request->value) {
                        switch (Read_Property_Value.tag) {
                            case BACNET_APPLICATION_TAG_REAL:
                                *(float*)Current_Request->value = Read_Property_Value.type.Real;
                                printf("读取到 REAL 值: %f\n", Read_Property_Value.type.Real);
                                break;
                            case BACNET_APPLICATION_TAG_UNSIGNED_INT:
                                *(uint32_t*)Current_Request->value = Read_Property_Value.type.Unsigned_Int;
                                printf("读取到 UNSIGNED_INT 值: %lu\n", Read_Property_Value.type.Unsigned_Int);
                                break;
                            case BACNET_APPLICATION_TAG_SIGNED_INT:
                                *(int32_t*)Current_Request->value = Read_Property_Value.type.Signed_Int;
                                printf("读取到 SIGNED_INT 值: %d\n", Read_Property_Value.type.Signed_Int);
                                break;
                            case BACNET_APPLICATION_TAG_BOOLEAN:
                                *(bool*)Current_Request->value = Read_Property_Value.type.Boolean;
                                printf("读取到 BOOLEAN 值: %s\n", Read_Property_Value.type.Boolean ? "true" : "false");
                                break;
                            case BACNET_APPLICATION_TAG_ENUMERATED:
                                *(uint32_t*)Current_Request->value = Read_Property_Value.type.Enumerated;
                                printf("读取到 ENUMERATED 值: %u\n", Read_Property_Value.type.Enumerated);
                                break;
                            default:
                                printf("警告：不支持的数据类型标签 %d\n", Read_Property_Value.tag);
                                Error_Detected = true;
                                break;
                        }
                    }
                } else {
                    printf("应用数据解码失败!\n");
                    Error_Detected = true;
                }
            } else {
                printf("没有应用数据!\n");
                Error_Detected = true;
            }
        }
    }
}

static void MyErrorHandler(
    BACNET_ADDRESS *src,
    uint8_t invoke_id,
    BACNET_ERROR_CLASS error_class,
    BACNET_ERROR_CODE error_code)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Error: %s: %s\n",
            bactext_error_class_name((int)error_class),
            bactext_error_code_name((int)error_code));
        Error_Detected = true;
    }
}

static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    (void)server;
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Abort: %s\n", bactext_abort_reason_name((int)abort_reason));
        Error_Detected = true;
    }
}

static void
MyRejectHandler(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Reject: %s\n",
            bactext_reject_reason_name((int)reject_reason));
        Error_Detected = true;
    }
}

static void
MyWritePropertySimpleAckHandler(BACNET_ADDRESS *src, uint8_t invoke_id)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf("WriteProperty Acknowledged!\n");
        Write_Property_Success = true;
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
    Device_Init(NULL); /* 初始化设备对象 */
    
    /* 初始化地址绑定和数据链路层环境 */
    address_init();
    dlenv_init();
    
    /* 注意: 此应用程序不需要处理 who-is，这会给用户带来困惑! */
    /* 为我们未实现的所有服务设置处理程序 */
    /* 发送正确的 reject 消息是必需的... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* 我们必须实现 read property - 这是必需的! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
        /* handle the data coming back from confirmed requests */
    apdu_set_confirmed_ack_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, My_Read_Property_Ack_Handler);
    /* 处理返回的回复(请求) */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, my_i_am_handler);
    /* 处理返回的任何错误 */
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
    apdu_set_error_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, MyErrorHandler);
    /* 处理 WriteProperty 的 Simple ACK */
    apdu_set_confirmed_simple_ack_handler(
        SERVICE_CONFIRMED_WRITE_PROPERTY, MyWritePropertySimpleAckHandler);
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
    
    printf("BACnet 驱动初始化成功。\n");
    return PROTO_SUCCESS;
}

/**
 * @brief 释放BACnet协议驱动占用的资源
 * @param ctx [in] 指向通用协议上下文的指针
 */
void proto_driver_release(proto_ctx_t *ctx) {
    if (ctx && ctx->type == PROTO_TYPE_BACNET) {
        // 清理事务状态机
        if (Request_Invoke_ID != 0) {
            tsm_free_invoke_id(Request_Invoke_ID);
            Request_Invoke_ID = 0;
        }
        
        // 清理地址绑定 - 移除所有设备
        if (g_target_device_id != 0) {
            address_remove_device(g_target_device_id);
        }
        
        // bip_cleanup 是 bip_init 的配对函数，用于关闭socket，释放资源。
        bip_cleanup();
        
        // 调用数据链路层清理
        datalink_cleanup();
        
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
    unsigned timeout_milliseconds = 0; /* 超时时间 (毫秒) */
    unsigned delay_milliseconds = 100; /* 接收响应的延迟时间 (毫秒) */
    struct mstimer apdu_timer = { 0 }; /* APDU 定时器 */
    struct mstimer datalink_timer = { 0 }; /* 数据链路层定时器 */
    BACNET_ADDRESS dest = { 0 }; /* 目标 BACnet 地址 */
    bool global_broadcast = true; /* 是否为全局广播 */
    bool repeat_forever = false; /* 是否永远重复发送 */
    long retry_count = 100; /* 重试次数 */
    
    if (global_broadcast) /* 如果是全局广播 */
    {
        datalink_get_broadcast_address(&dest); /* 获取数据链路层广播地址 */
    }

    /* 设置自身信息 */
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE); /* 设置本设备的对象实例号 (通常为最大值以避免冲突) */
    
    /* 注册程序退出时调用的数据链路层清理函数 */
    atexit(datalink_cleanup);
    
    if (timeout_milliseconds == 0) /* 如果未指定超时时间 */
    {
        timeout_milliseconds = apdu_timeout() * apdu_retries(); /* 使用默认的 APDU 超时时间和重试次数计算 */
    }
    mstimer_set(&apdu_timer, timeout_milliseconds); /* 设置 APDU 定时器 */
    mstimer_set(&datalink_timer, 1000); /* 设置数据链路层维护定时器 (1秒) */

    printf("正在通过 Who-Is 发现设备 %u...\n", g_target_device_id);
    // Send_WhoIs 是库提供的函数，用于构建并发送一个 Who-Is 报文。
    // 两个参数分别代表要寻找的设备ID的最小和最大范围。这里我们只找一个特定设备。
    Send_WhoIs(g_target_device_id, g_target_device_id);

    // 参考 whois.cc 的主循环实现，使用无限循环直到找到设备或超时
    for (;;) {
        if (g_target_device_found) {
            printf("设备连接成功 (地址已缓存).\n");
            return PROTO_SUCCESS;
        }
        
        BACNET_ADDRESS src;
        // datalink_receive 是数据链路层的核心函数，用于从网卡接收一个BACnet包。
        uint16_t pdu_len = datalink_receive(&src, &g_RxBuf[0], MAX_MPDU, delay_milliseconds);
        if (pdu_len) {
            // npdu_handler 是协议栈的入口。收到任何包都"喂"给它，
            // 它会负责解析并触发相应的回调函数。
            npdu_handler(&src, &g_RxBuf[0], pdu_len);
        }
        
        if (mstimer_expired(&datalink_timer)) /* 如果数据链路层定时器超时 */
        {
            datalink_maintenance_timer(
                mstimer_interval(&datalink_timer) / 1000); /* 执行数据链路层维护 */
            mstimer_reset(&datalink_timer); /* 重置数据链路层定时器 */
        }
        
        if (mstimer_expired(&apdu_timer)) /* 如果 APDU 定时器超时 */
        {
            printf("连接失败：未发现目标设备.\n");
            return PROTO_ERROR_CONNECT;
        }
    }
}

/**
 * @brief 断开与设备的“连接”
 * @details 主要是从本地的地址缓存中移除设备信息。
 * @param ctx [in] 指向通用协议上下文的指针
 */
void proto_disconnect(proto_ctx_t *ctx) {
    if (ctx && ctx->type == PROTO_TYPE_BACNET) {
        // 清理当前事务状态
        if (Request_Invoke_ID != 0) {
            tsm_free_invoke_id(Request_Invoke_ID);
            Request_Invoke_ID = 0;
        }
        
        // 重置全局状态变量
        Error_Detected = false;
        Read_Property_Result_Available = false;
        Write_Property_Success = false;
        Current_Request = NULL;
        
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
int proto_read(proto_ctx_t *ctx, proto_request_t *req) {
    if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;
    
    internal_bacnet_address_t bac_addr;
    if (!parse_bacnet_resource(req->resource_name, &bac_addr)) {
        return PROTO_ERROR_PARAM;
    }
    
    // 重置全局状态
    Error_Detected = false;
    Request_Invoke_ID = 0;
    Read_Property_Result_Available = false;
    Current_Request = req;  // 设置当前请求，以便回调函数可以访问
    
    // 获取目标设备地址
    unsigned max_apdu = 0;
    bool found = address_bind_request(g_target_device_id, &max_apdu, &Target_Address);
    if (!found) {
        printf("错误：无法找到设备 %u 的地址\n", g_target_device_id);
        Current_Request = NULL;
        return PROTO_ERROR_CONNECT;
    }
    
    // 发送读属性请求
    Request_Invoke_ID = Send_Read_Property_Request(g_target_device_id,
        bac_addr.object_type, bac_addr.object_instance,
        bac_addr.property_id, BACNET_ARRAY_ALL);

    if (Request_Invoke_ID == 0) {
        printf("错误：无法发送读属性请求\n");
        Current_Request = NULL;
        return PROTO_ERROR_READ;
    }

    // 使用类似 readprop.cc 的主循环来等待响应
    time_t last_seconds = time(NULL);
    time_t current_seconds = 0;
    time_t timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();
    time_t elapsed_seconds = 0;
    unsigned timeout = 100; // milliseconds
    BACNET_ADDRESS src = {0};
    uint16_t pdu_len = 0;
    
    printf("正在读取属性 %s...\n", req->resource_name);
    
    // 主循环 - 参考 readprop.cc 的实现
    for (;;) {
        /* increment timer - exit if timed out */
        current_seconds = time(NULL);

        /* at least one second has passed */
        if (current_seconds != last_seconds) {
            tsm_timer_milliseconds(
                (uint16_t)((current_seconds - last_seconds) * 1000));
            datalink_maintenance_timer(current_seconds - last_seconds);
        }
        
        if (Error_Detected) {
            printf("错误：BACnet协议错误\n");
            break;
        }
        
        // 检查事务状态
        if (tsm_invoke_id_free(Request_Invoke_ID)) {
            // 事务已完成
            printf("读取操作完成\n");
            break;
        } else if (tsm_invoke_id_failed(Request_Invoke_ID)) {
            printf("错误：TSM 超时!\n");
            tsm_free_invoke_id(Request_Invoke_ID);
            Error_Detected = true;
            break;
        }
        
        // 检查总体超时
        elapsed_seconds += (current_seconds - last_seconds);
        if (elapsed_seconds > timeout_seconds) {
            printf("错误：APDU 超时!\n");
            Error_Detected = true;
            break;
        }

        /* returns 0 bytes on timeout */
        pdu_len = datalink_receive(&src, &g_RxBuf[0], MAX_MPDU, timeout);

        /* process - 这是关键：必须调用 npdu_handler 来触发回调 */
        if (pdu_len) {
            npdu_handler(&src, &g_RxBuf[0], pdu_len);
        }

        /* keep track of time for next check */
        last_seconds = current_seconds;
    }
    
    // 清理当前请求指针
    Current_Request = NULL;
    
    if (Error_Detected) {
        return PROTO_ERROR_READ;
    }
    
    // 检查是否成功获取到结果
    if (Read_Property_Result_Available) {
        printf("成功读取属性值\n");
        return PROTO_SUCCESS;
    } else {
        printf("未能获取到有效的属性值\n");
        return PROTO_ERROR_READ;
    }
}

/**
 * @brief 写入一个BACnet属性
 * @param ctx [in] 指向通用协议上下文的指针
 * @param req [in] 指向通用请求结构体的指针。resource_name 和 req->value 作为输入。
 * @return PROTO_SUCCESS 或错误码
 */

int proto_write(proto_ctx_t *ctx, proto_request_t *req) {
    if (ctx->type != PROTO_TYPE_BACNET) return PROTO_ERROR_UNSUPPORTED;

    internal_bacnet_address_t bac_addr;
    if (!parse_bacnet_resource(req->resource_name, &bac_addr)) {
        return PROTO_ERROR_PARAM;
    }

    if (req->value == NULL) return PROTO_ERROR_PARAM;
    
    // 重置全局状态
    Error_Detected = false;
    Request_Invoke_ID = 0;
    Write_Property_Success = false;
    
    // 获取目标设备地址
    unsigned max_apdu = 0;
    bool found = address_bind_request(g_target_device_id, &max_apdu, &Target_Address);
    if (!found) {
        printf("错误：无法找到设备 %u 的地址\n", g_target_device_id);
        return PROTO_ERROR_CONNECT;
    }
    
    // 准备要写入的值 - 根据BACnet协议，大多数present-value都是REAL类型
    BACNET_APPLICATION_DATA_VALUE value;
    value.tag = BACNET_APPLICATION_TAG_REAL;
    value.type.Real = *(float*)req->value;
    value.context_specific = false;
    value.context_tag = 0;
    value.next = NULL;
    
    printf("准备写入值: %f，数据类型: REAL\n", value.type.Real);
    
    // 发送写属性请求
    Request_Invoke_ID = Send_Write_Property_Request(g_target_device_id,
        bac_addr.object_type, bac_addr.object_instance,
        bac_addr.property_id, &value, BACNET_NO_PRIORITY, BACNET_ARRAY_ALL);

    if (Request_Invoke_ID == 0) {
        printf("错误：无法发送写属性请求\n");
        return PROTO_ERROR_WRITE;
    }

    // 使用类似 writeprop.cc 的主循环来等待响应
    time_t last_seconds = time(NULL);
    time_t current_seconds = 0;
    time_t timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();
    time_t elapsed_seconds = 0;
    unsigned timeout = 100; // milliseconds
    BACNET_ADDRESS src = {0};
    uint16_t pdu_len = 0;
    
    printf("正在写入属性 %s，值: %f...\n", req->resource_name, *(float*)req->value);
    
    // 主循环 - 参考 writeprop.cc 的实现
    for (;;) {
        /* increment timer - exit if timed out */
        current_seconds = time(NULL);

        /* at least one second has passed */
        if (current_seconds != last_seconds) {
            tsm_timer_milliseconds(
                (uint16_t)((current_seconds - last_seconds) * 1000));
            datalink_maintenance_timer(current_seconds - last_seconds);
        }
        
        if (Error_Detected) {
            printf("错误：BACnet协议错误\n");
            break;
        }
        
        // 检查事务状态
        if (tsm_invoke_id_free(Request_Invoke_ID)) {
            // 事务已完成
            printf("写入操作完成\n");
            break;
        } else if (tsm_invoke_id_failed(Request_Invoke_ID)) {
            printf("错误：TSM 超时!\n");
            tsm_free_invoke_id(Request_Invoke_ID);
            Error_Detected = true;
            break;
        }
        
        // 检查总体超时
        elapsed_seconds += (current_seconds - last_seconds);
        if (elapsed_seconds > timeout_seconds) {
            printf("错误：APDU 超时!\n");
            Error_Detected = true;
            break;
        }

        /* returns 0 bytes on timeout */
        pdu_len = datalink_receive(&src, &g_RxBuf[0], MAX_MPDU, timeout);

        /* process - 这是关键：必须调用 npdu_handler 来触发回调 */
        if (pdu_len) {
            npdu_handler(&src, &g_RxBuf[0], pdu_len);
        }

        /* keep track of time for next check */
        last_seconds = current_seconds;
    }
    
    if (Error_Detected) {
        return PROTO_ERROR_WRITE;
    }
    
    // 检查是否成功写入
    if (Write_Property_Success) {
        printf("成功写入属性值\n");
        return PROTO_SUCCESS;
    } else {
        printf("未能确认写入操作成功\n");
        return PROTO_ERROR_WRITE;
    }
}
