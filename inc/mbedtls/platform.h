#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Minimal platform shim */
void *mbedtls_calloc(size_t n, size_t size);
void mbedtls_free(void *p);
#define MBEDTLS_PLATFORM_C
#include <stdint.h>

typedef struct { int dummy; } mbedtls_platform_context;
/* allow setting allocators */
void mbedtls_platform_set_calloc_free(void *(*calloc_func)(size_t,size_t), void (*free_func)(void*));
int mbedtls_platform_setup(mbedtls_platform_context *ctx);
void mbedtls_platform_teardown(mbedtls_platform_context *ctx);
#ifdef __cplusplus
}
#endif


