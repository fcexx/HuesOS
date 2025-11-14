#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
#ifdef __cplusplus
}
#endif


