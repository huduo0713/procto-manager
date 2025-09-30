#include "common/api/proto_driver.h"
#include "proto_mqtt.h"
#include <pthread.h>

// 进程内 MQTT 协议上下文单例
static proto_ctx_t g_mqtt_ctx = {
    PROTO_TYPE_MQTT,
    NULL,
    NULL,
    NULL
};

static int g_inited = 0;
static pthread_mutex_t g_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

// 确保已初始化并按需连接
static int ensure_init_and_connect(void) {
    int rc = PROTO_SUCCESS;

    if (!g_inited) {
        rc = proto_driver_init(&g_mqtt_ctx);
        if (rc != PROTO_SUCCESS) {
            return rc;
        }
        g_inited = 1;
    }

    // 如果有 MQTT 上下文，依据状态机检查连接
    mqtt_ctx_t *mqtt_ctx = (mqtt_ctx_t *)g_mqtt_ctx.userdata;
    if (!mqtt_ctx) {
        // 初始化应该已设置 userdata
        return PROTO_ERROR_INIT;
    }

    connect_status_t status = get_connect_status(mqtt_ctx);
    if (status != CON_OK) {
        rc = proto_connect(&g_mqtt_ctx);
        if (rc != PROTO_SUCCESS) {
            return rc;
        }
    }

    return PROTO_SUCCESS;
}

int plc_proto_read(void *req) {
    if (!req) return PROTO_ERROR_PARAM;

    // 仅在确保上下文阶段加锁，避免长时间持锁影响读写
    pthread_mutex_lock(&g_ctx_mutex);
    int rc = ensure_init_and_connect();
    pthread_mutex_unlock(&g_ctx_mutex);

    if (rc != PROTO_SUCCESS) return rc;

    return proto_read(&g_mqtt_ctx, (proto_request_t *)req);
}

int plc_proto_write(void *req) {
    if (!req) return PROTO_ERROR_PARAM;

    pthread_mutex_lock(&g_ctx_mutex);
    int rc = ensure_init_and_connect();
    pthread_mutex_unlock(&g_ctx_mutex);

    if (rc != PROTO_SUCCESS) return rc;

    return proto_write(&g_mqtt_ctx, (proto_request_t *)req);
}



static int ensure_json_object_initialized(char *dst, size_t cap) {
    if (!dst || cap < 2) return -1;
    if (dst[0] == '\0') {
        if (cap < 3) return -1; // 至少需要 "{}" 和终止符
        dst[0] = '{';
        dst[1] = '}';
        dst[2] = '\0';
    }
    return 0;
}

// 在 JSON 对象字符串末尾（闭括号前）插入一段内容
static int json_append_before_closing_brace(char *dst, size_t cap, const char *snippet) {
    size_t len = strlen(dst);
    if (len < 2 || dst[0] != '{' || dst[len - 1] != '}') return -1;
    size_t snip_len = strlen(snippet);
    // 需要空间: 现有内容(去掉结尾的 '}') + 可能的逗号 + 片段 + '}' + '\0'
    // 判断是否需要逗号：如果当前对象内已有键值对（长度>2且不是"{}"），则需加逗号
    int needs_comma = (len > 2);
    size_t need = (len - 1) + (needs_comma ? 1 : 0) + snip_len + 1 + 1;
    if (need > cap) return -1;
    // 回退覆盖 '}'
    char *p = dst + len - 1;
    if (needs_comma) *p++ = ',';
    memcpy(p, snippet, snip_len);
    p += snip_len;
    *p++ = '}';
    *p = '\0';
    return 0;
}

int mqtt_data_format(const char* key, const void* data_ptr, TypeData type, char* dst) {
    if (!key || !dst) return PROTO_ERROR_PARAM;
    if (strlen(key) == 0) return PROTO_ERROR_PARAM;
    const size_t cap = MAX_MESSAGE_SIZE;

    // 确保 dst 初始化为一个空 JSON 对象 "{}"
    if (ensure_json_object_initialized(dst, cap) != 0) {
        return PROTO_ERROR_WRITE;
    }

    // 准备片段，根据类型格式化
    char snippet[256];
    snippet[0] = '\0';

    switch (type) {
        case ENUM_INT32: {
            if (!data_ptr) return PROTO_ERROR_PARAM;
            int v = *(const int*)data_ptr;
            // {"key": 123}
            int n = snprintf(snippet, sizeof(snippet), "\"%s\": %d", key, v);
            if (n < 0 || (size_t)n >= sizeof(snippet)) return PROTO_ERROR_WRITE;
            break;
        }
        case ENUM_FLOAT: {
            if (!data_ptr) return PROTO_ERROR_PARAM;
            float v = *(const float*)data_ptr;
            // 使用合理精度，避免科学计数法
            int n = snprintf(snippet, sizeof(snippet), "\"%s\": %.6f", key, (double)v);
            if (n < 0 || (size_t)n >= sizeof(snippet)) return PROTO_ERROR_WRITE;
            break;
        }
        case ENUM_BOOL: {
            if (!data_ptr) return PROTO_ERROR_PARAM;
            int v = *(const int*)data_ptr;
            int n = snprintf(snippet, sizeof(snippet), "\"%s\": %s", key, v ? "true" : "false");
            if (n < 0 || (size_t)n >= sizeof(snippet)) return PROTO_ERROR_WRITE;
            break;
        }
        case ENUM_STRING: {
            if (!data_ptr) return PROTO_ERROR_PARAM;
            const char *s = (const char*)data_ptr;
            // 简单转义: 将内部双引号替换为\"，避免破坏 JSON（简单实现，按需增强）
            char buf[200];
            size_t bi = 0;
            for (size_t i = 0; s[i] && bi + 2 < sizeof(buf); ++i) {
                if (s[i] == '"') {
                    buf[bi++] = '\\';
                    buf[bi++] = '"';
                } else if ((unsigned char)s[i] < 0x20) {
                    // 控制字符简单跳过，避免生成非法 JSON
                } else {
                    buf[bi++] = s[i];
                }
            }
            buf[bi] = '\0';
            int n = snprintf(snippet, sizeof(snippet), "\"%s\": \"%s\"", key, buf);
            if (n < 0 || (size_t)n >= sizeof(snippet)) return PROTO_ERROR_WRITE;
            break;
        }
        default:
            return PROTO_ERROR_UNSUPPORTED;
    }

    // 追加到 dst 的 JSON 对象中
    if (json_append_before_closing_brace(dst, cap, snippet) != 0) {
        return PROTO_ERROR_WRITE; // 容量不足
    }

    return PROTO_SUCCESS;
}

