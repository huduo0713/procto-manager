#include "proto_modbus.h"
#include <stdlib.h>
#include <string.h>
#include <modbus/modbus.h>


int proto_driver_init(proto_ctx_t *ctx) {
    if (!ctx || !ctx->config) return PROTO_ERROR_PARAM;
    modbus_config_t *cfg = (modbus_config_t *)ctx->config;
    modbus_t *mb = modbus_new_tcp(cfg->ip, cfg->port);
    if (!mb) return PROTO_ERROR_INIT;
    modbus_set_slave(mb, cfg->slave_id);
    ctx->client = mb;
    return PROTO_SUCCESS;
}

void proto_driver_release(proto_ctx_t *ctx) {
    if (ctx && ctx->client) {
        modbus_close((modbus_t *)ctx->client);
        modbus_free((modbus_t *)ctx->client);
        ctx->client = NULL;
    }
}

int proto_connect(proto_ctx_t *ctx) {
    if (!ctx || !ctx->client) return PROTO_ERROR_PARAM;
    if (modbus_connect((modbus_t *)ctx->client) == -1)
        return PROTO_ERROR_CONNECT;
    return PROTO_SUCCESS;
}

void proto_disconnect(proto_ctx_t *ctx) {
    if (ctx && ctx->client)
        modbus_close((modbus_t *)ctx->client);
}

int proto_read(proto_ctx_t *ctx, proto_request_t *req) {
    if (!ctx || !ctx->client || !req) return PROTO_ERROR_PARAM;
    uint16_t *dest = (uint16_t *)req->value;
    int rc = modbus_read_registers((modbus_t *)ctx->client, req->address, req->quantity, dest);
    return rc == req->quantity ? PROTO_SUCCESS : PROTO_ERROR_READ;
}

int proto_write(proto_ctx_t *ctx, proto_request_t *req) {
    if (!ctx || !ctx->client || !req) return PROTO_ERROR_PARAM;
    uint16_t *data = (uint16_t *)req->value;
    int rc = modbus_write_registers((modbus_t *)ctx->client, req->address, req->quantity, data);
    return rc == req->quantity ? PROTO_SUCCESS : PROTO_ERROR_WRITE;
}
