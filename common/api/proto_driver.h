#pragma once
#include "proto_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int proto_driver_init(proto_ctx_t *ctx);
void proto_driver_release(proto_ctx_t *ctx);
proto_ctx_t *proto_create_context(proto_type_t type, void *config);
void proto_free_context(proto_ctx_t *ctx);
int proto_connect(proto_ctx_t *ctx);
void proto_disconnect(proto_ctx_t *ctx);
int proto_read(proto_ctx_t *ctx, proto_request_t *req);
int proto_write(proto_ctx_t *ctx, proto_request_t *req);

#ifdef __cplusplus
}
#endif