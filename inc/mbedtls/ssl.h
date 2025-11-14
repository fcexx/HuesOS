/* Minimal mbedTLS shim for build without upstream third_party/mbedtls.
 * This provides just enough types and functions used by the project to
 * compile and to route TLS I/O through existing BIO wrappers. It does NOT
 * implement real TLS and should NOT be used in production.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (small set used by the project) */
#define MBEDTLS_ERR_SSL_WANT_READ  (-0x6900)
#define MBEDTLS_ERR_SSL_WANT_WRITE (-0x6901)
#define MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE (-0x7080)
/* Entropy/source error */
#define MBEDTLS_ERR_ENTROPY_SOURCE_FAILED (-0x7010)

/* Handshake completion sentinel used by tls.c loop */
#define MBEDTLS_SSL_HANDSHAKE_OVER 0

typedef struct {
	void *f_send;
	void *f_recv;
	void *f_recv_timeout;
	void *bio_ctx;
	int state;
} mbedtls_ssl_context;

typedef struct {
	int dummy;
} mbedtls_ssl_config;

/* configuration enums/macros used by tls.c */
#define MBEDTLS_SSL_IS_CLIENT 1
#define MBEDTLS_SSL_TRANSPORT_STREAM 1
#define MBEDTLS_SSL_PRESET_DEFAULT 0
#define MBEDTLS_SSL_MAJOR_VERSION_3 3
#define MBEDTLS_SSL_MINOR_VERSION_3 3

/* ciphersuite placeholders */
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 0x01
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384   0x02
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 0x03
#define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256   0x04
#define MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 0x05
#define MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256   0x06

/* verification mode */
#define MBEDTLS_SSL_VERIFY_NONE 0
/* prototypes */
void mbedtls_ssl_init(mbedtls_ssl_context *ssl);
void mbedtls_ssl_free(mbedtls_ssl_context *ssl);
void mbedtls_ssl_config_init(mbedtls_ssl_config *conf);
void mbedtls_ssl_config_free(mbedtls_ssl_config *conf);
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *conf, int endpoint, int transport, int preset);
int mbedtls_ssl_setup(mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf);

void mbedtls_ssl_set_hostname(mbedtls_ssl_context *ssl, const char *hostname);
void mbedtls_ssl_set_timer_cb(mbedtls_ssl_context *ssl, void *ctx,
                              void (*set)(void*, uint32_t, uint32_t),
                              int (*get)(void*));
void mbedtls_ssl_conf_read_timeout(mbedtls_ssl_config *conf, uint32_t ms);
void mbedtls_ssl_conf_ciphersuites(mbedtls_ssl_config *conf, const int *suites);
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *conf, int mode);
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *conf, int (*f_rng)(void*, unsigned char*, size_t), void *p_rng);

/* BIO glue */
void mbedtls_ssl_set_bio(mbedtls_ssl_context *ssl, void *ctx,
                         int (*f_send)(void*, const unsigned char*, size_t),
                         int (*f_recv)(void*, unsigned char*, size_t),
                         int (*f_recv_timeout)(void*, unsigned char*, size_t, uint32_t));

/* Handshake step (nonblocking) */
int mbedtls_ssl_handshake_step(mbedtls_ssl_context *ssl);

/* I/O */
int mbedtls_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len);
int mbedtls_ssl_read(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len);
int mbedtls_ssl_close_notify(mbedtls_ssl_context *ssl);

#ifdef __cplusplus
}
#endif


