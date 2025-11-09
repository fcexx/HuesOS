#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HTTPS GET client.
 * On success returns 0 and writes up to 'cap' bytes of the HTTP response body into 'out',
 * setting *out_len to the number of bytes written.
 * Returns <0 on error or if TLS is not available.
 */
int https_get(const char* host, const char* path, uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms);

/* Stream variant: write HTTP body directly to a file (RAMFS), optionally soft-wrapping.
 * soft_wrap_cols: 0 to disable, otherwise insert '\n' when this column is reached.
 * Returns 0 on success.
 */
int https_get_to_file(const char* host, const char* path, const char* out_path, uint32_t timeout_ms, int soft_wrap_cols);

#ifdef __cplusplus
}
#endif


