#pragma once
#include "common/api/proto_common.h"  // 已定义 proto_ctx_t、proto_request_t、proto_status_t
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char broker[128];
    char client_id[64];
    char username[64];
    char password[64];
    char pub_topic[64];
    char sub_topic[64];
    int timeout_ms;
} mqtt_config_t;

int proto_driver_init(proto_ctx_t *ctx);
void proto_driver_release(proto_ctx_t *ctx);
int proto_connect(proto_ctx_t *ctx);
void proto_disconnect(proto_ctx_t *ctx);
int proto_read(proto_ctx_t *ctx, proto_request_t *req);
int proto_write(proto_ctx_t *ctx, proto_request_t *req);

#ifdef __cplusplus
}
#endif
