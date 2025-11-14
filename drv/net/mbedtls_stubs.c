#ifdef USE_MBEDTLS
#include "mbedtls/ssl.h"

/* Provide missing server-side step symbol to satisfy client build. */
int mbedtls_ssl_handshake_server_step(mbedtls_ssl_context *ssl) {
	(void)ssl;
	return MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE;
}
#endif


