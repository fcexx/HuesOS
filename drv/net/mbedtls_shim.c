#include "mbedtls/ssl.h"
#include "mbedtls/debug.h"
#include <string.h>
#include <stdint.h>

/* Very small shim: no real crypto. Routes mbedTLS-style calls to the BIO
 * functions provided by the TLS glue (bio_send/bio_recv/bio_recv_timeout).
 * This keeps the existing tls.c logic working while removing the external
 * third_party dependency. */

void mbedtls_debug_set_threshold(int level) { (void)level; }
void mbedtls_debug_print(int level, const char *file, int line, const char *str) {
	(void)level; (void)file; (void)line; (void)str;
}

/* platform allocator hooks */
static void *(*stub_calloc)(size_t,size_t) = 0;
static void (*stub_free)(void*) = 0;
void mbedtls_platform_set_calloc_free(void *(*calloc_func)(size_t,size_t), void (*free_func)(void*)) {
	stub_calloc = calloc_func;
	stub_free = free_func;
}
void *mbedtls_calloc(size_t n, size_t size) {
	if (stub_calloc) return stub_calloc(n,size);
	return 0;
}
void mbedtls_free(void *p) { if (stub_free) stub_free(p); }

/* additional config helpers used by tls.c */
void mbedtls_ssl_conf_min_version(mbedtls_ssl_config *conf, int major, int minor) { (void)conf; (void)major; (void)minor; }
void mbedtls_ssl_conf_max_version(mbedtls_ssl_config *conf, int major, int minor) { (void)conf; (void)major; (void)minor; }
void mbedtls_ssl_conf_dbg(mbedtls_ssl_config *conf, void (*cb)(void*,int,const char*,int,const char*), void *ctx) { (void)conf; (void)cb; (void)ctx; }

void mbedtls_ssl_init(mbedtls_ssl_context *ssl) {
	memset(ssl, 0, sizeof(*ssl));
	ssl->state = MBEDTLS_SSL_HANDSHAKE_OVER; /* mark handshake done by default */
}
void mbedtls_ssl_free(mbedtls_ssl_context *ssl) { (void)ssl; }
void mbedtls_ssl_config_init(mbedtls_ssl_config *conf) { (void)conf; }
void mbedtls_ssl_config_free(mbedtls_ssl_config *conf) { (void)conf; }
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *conf, int endpoint, int transport, int preset) {
	(void)conf; (void)endpoint; (void)transport; (void)preset; return 0;
}
int mbedtls_ssl_setup(mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf) { (void)conf; (void)ssl; return 0; }
void mbedtls_ssl_set_hostname(mbedtls_ssl_context *ssl, const char *hostname) { (void)ssl; (void)hostname; }
void mbedtls_ssl_set_timer_cb(mbedtls_ssl_context *ssl, void *ctx,
                              void (*set)(void*, uint32_t, uint32_t),
                              int (*get)(void*)) { (void)ssl; (void)ctx; (void)set; (void)get; }
void mbedtls_ssl_conf_read_timeout(mbedtls_ssl_config *conf, uint32_t ms) { (void)conf; (void)ms; }
void mbedtls_ssl_conf_ciphersuites(mbedtls_ssl_config *conf, const int *suites) { (void)conf; (void)suites; }
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *conf, int mode) { (void)conf; (void)mode; }
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *conf, int (*f_rng)(void*, unsigned char*, size_t), void *p_rng) {
	(void)conf; (void)f_rng; (void)p_rng;
}

void mbedtls_ssl_set_bio(mbedtls_ssl_context *ssl, void *ctx,
                         int (*f_send)(void*, const unsigned char*, size_t),
                         int (*f_recv)(void*, unsigned char*, size_t),
                         int (*f_recv_timeout)(void*, unsigned char*, size_t, uint32_t)) {
	ssl->bio_ctx = ctx;
	ssl->f_send = (void*)f_send;
	ssl->f_recv = (void*)f_recv;
	ssl->f_recv_timeout = (void*)f_recv_timeout;
}

int mbedtls_ssl_handshake_step(mbedtls_ssl_context *ssl) {
	/* fast-path: treat handshake as complete */
	ssl->state = MBEDTLS_SSL_HANDSHAKE_OVER;
	return 0;
}

int mbedtls_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len) {
	if (!ssl || !ssl->f_send) return -1;
	int (*f)(void*, const unsigned char*, size_t) = (int(*)(void*, const unsigned char*, size_t))ssl->f_send;
	int r = f(ssl->bio_ctx, buf, len);
	/* pass through WANT_WRITE if produced by underlying BIO */
	if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return MBEDTLS_ERR_SSL_WANT_WRITE;
	return r;
}

int mbedtls_ssl_read(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) {
	if (!ssl) return -1;
	/* Prefer recv_timeout if available */
	if (ssl->f_recv_timeout) {
		int (*f)(void*, unsigned char*, size_t, uint32_t) = (int(*)(void*, unsigned char*, size_t, uint32_t))ssl->f_recv_timeout;
		int r = f(ssl->bio_ctx, buf, len, 1000);
		if (r == 0) return MBEDTLS_ERR_SSL_WANT_READ;
		return r;
	}
	if (ssl->f_recv) {
		int (*f)(void*, unsigned char*, size_t) = (int(*)(void*, unsigned char*, size_t))ssl->f_recv;
		int r = f(ssl->bio_ctx, buf, len);
		if (r == 0) return MBEDTLS_ERR_SSL_WANT_READ;
		return r;
	}
	return MBEDTLS_ERR_SSL_WANT_READ;
}

int mbedtls_ssl_close_notify(mbedtls_ssl_context *ssl) { (void)ssl; return 0; }


