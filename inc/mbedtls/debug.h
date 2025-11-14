#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
void mbedtls_debug_set_threshold(int level);
void mbedtls_debug_print(int level, const char *file, int line, const char *str);
#ifdef __cplusplus
}
#endif


