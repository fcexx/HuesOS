#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int dummy; } mbedtls_ctr_drbg_context;
void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *ctx);
void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *ctx);
int mbedtls_ctr_drbg_random(void *p_rng, unsigned char *output, size_t len);
#ifdef __cplusplus
}
#endif


