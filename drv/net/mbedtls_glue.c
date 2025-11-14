#ifdef USE_MBEDTLS
#include <stdint.h>
#include <stddef.h>
#include <heap.h>
#include <pit.h>
#include <e1000.h>
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"

void* axon_calloc(size_t n, size_t sz) {
	size_t total = n * sz;
	void* p = kmalloc(total);
	if (!p) return NULL;
	memset(p, 0, total);
	return p;
}
void axon_free(void* p) {
	if (p) kfree(p);
}

int mbedtls_platform_setup(mbedtls_platform_context *ctx) {
	(void)ctx;
	mbedtls_platform_set_calloc_free(axon_calloc, axon_free);
	return 0;
}

void mbedtls_platform_teardown(mbedtls_platform_context *ctx) {
	(void)ctx;
}

/* Weak entropy source: combine PIT time and MAC address.
 * WARNING: Not cryptographically strong; for demo only. */
int mbedtls_hardware_poll( void *data, unsigned char *output, size_t len, size_t *olen ) {
	(void)data;
	uint8_t mac[6] = {0};
	e1000_get_mac(mac);
	uint64_t t = pit_get_time_ms();
	for (size_t i = 0; i < len; i++) {
		uint8_t b = (uint8_t)((t >> ((i % 8) * 8)) & 0xFF);
		output[i] = b ^ mac[i % 6] ^ (uint8_t)i;
	}
	if (olen) *olen = len;
	return 0;
}

#endif

