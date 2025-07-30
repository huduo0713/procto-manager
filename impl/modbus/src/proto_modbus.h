#pragma once
#include "common/api/proto_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ip[32];
    int port;
    int slave_id;
} modbus_config_t;


int proto_modbus_driver_init(proto_ctx_t *ctx);
void proto_modbus_driver_release(proto_ctx_t *ctx);
int proto_modbus_connect(proto_ctx_t *ctx);
void proto_modbus_disconnect(proto_ctx_t *ctx);
int proto_modbus_read(proto_ctx_t *ctx, proto_request_t *req);
int proto_modbus_write(proto_ctx_t *ctx, proto_request_t *req);

#ifdef __cplusplus
}
#endif
