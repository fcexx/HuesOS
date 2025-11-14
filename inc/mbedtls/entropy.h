#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int dummy; } mbedtls_entropy_context;
void mbedtls_entropy_init(mbedtls_entropy_context *ctx);
void mbedtls_entropy_free(mbedtls_entropy_context *ctx);
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
#ifdef __cplusplus
}
#endif


