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



#ifdef __cplusplus
}
#endif
