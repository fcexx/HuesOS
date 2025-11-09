#include <stdint.h>
#include <stddef.h>
#include <tls.h>
#include <debug.h>

/* Optional TLS integration with mbedTLS.
 * To enable real HTTPS, compile with -DUSE_MBEDTLS and add mbedTLS library sources to the build.
 * For now, provide stubs that return an error to keep the kernel build working.
 */

#ifndef USE_MBEDTLS
#include <net_lwip.h>
#include <fs.h>
#include <ramfs.h>
/* Fallback: talk to host-side TLS proxy (e.g. socat) over plain TCP.
 * Example proxy on host: socat TCP-LISTEN:8443,fork,reuseaddr OPENSSL:www.google.com:443,verify=0
 */
int https_get(const char* host, const char* path, uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms) {														
	if (!host || !path || !out || cap == 0) return -1;
	/* QEMU usernet gw in our setup */
	uint32_t ip = (10<<24)|(0<<16)|(2<<8)|10;
	uint16_t port = 8443;																																										
	lwip_tcp_handle_t* th = lwip_tcp_connect(ip, port, timeout_ms);
	if (!th) return -2;
	char req[256];
	int req_len = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: AxonOS\r\nConnection: close\r\n\r\n",
		path, host);
	if (req_len <= 0) { lwip_tcp_close(th); return -3; }
	int wr = lwip_tcp_send(th, (const uint8_t*)req, (size_t)req_len, timeout_ms);
	if (wr < 0) { lwip_tcp_close(th); return -4; }
	size_t total = 0;
	for (;;) {
		int n = lwip_tcp_recv(th, out + total, cap - total, timeout_ms);
		if (n < 0) { lwip_tcp_close(th); return -5; }
		if (n == 0) break;
		total += (size_t)n;
		if (total == cap) break;
	}
	if (out_len) *out_len = total;
	lwip_tcp_close(th);
	return 0;
}

int https_get_to_file(const char* host, const char* path, const char* out_path, uint32_t timeout_ms, int soft_wrap_cols) {
	if (!host || !path || !out_path) return -1;
	uint32_t ip = (10<<24)|(0<<16)|(2<<8)|10;
	uint16_t port = 8443;
	lwip_tcp_handle_t* th = lwip_tcp_connect(ip, port, timeout_ms);
	if (!th) return -2;
	char req[256];
	int req_len = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: AxonOS\r\nConnection: close\r\n\r\n",
		path, host);
	if (req_len <= 0) { lwip_tcp_close(th); return -3; }
	if (lwip_tcp_send(th, (const uint8_t*)req, (size_t)req_len, timeout_ms) < 0) { lwip_tcp_close(th); return -4; }
	struct fs_file *f = fs_create_file(out_path);
	if (!f) { lwip_tcp_close(th); return -5; }
	size_t file_off = 0;
	static uint8_t buf[2048];
	int header_done = 0;
	/* rolling window to detect header end across chunks */
	uint8_t b0=0,b1=0,b2=0,b3=0;
	/* Soft wrap state */
	int wrap_cols = soft_wrap_cols > 0 ? soft_wrap_cols : 0;
	int col = 0;
	for (;;) {
		int n = lwip_tcp_recv(th, buf, sizeof(buf), timeout_ms);
		if (n < 0) { fs_file_free(f); lwip_tcp_close(th); return -6; }
		if (n == 0) break;
		for (int i = 0; i < n; i++) {
			uint8_t ch = buf[i];
			if (!header_done) {
				/* shift 4-byte window and detect end-of-headers */
				b0=b1; b1=b2; b2=b3; b3=ch;
				if ((b0=='\r' && b1=='\n' && b2=='\r' && b3=='\n') ||
				    (b2=='\n' && b3=='\n')) {
					header_done = 1; col = 0; continue;
				}
				/* keep consuming headers */
				continue;
			}
			/* body: write with optional wrap */
			if (!wrap_cols) {
				fs_write(f, &ch, 1, file_off++);
				continue;
			}
			if (ch == '\r') { col = 0; continue; }
			if (ch == '\n') { uint8_t c = '\n'; fs_write(f, &c, 1, file_off++); col = 0; continue; }
			fs_write(f, &ch, 1, file_off++); col++;
			if (col >= wrap_cols) { uint8_t nl = '\n'; fs_write(f, &nl, 1, file_off++); col = 0; }
		}
	}
	fs_file_free(f);
	lwip_tcp_close(th);
	return 0;
}
#else
/* mbedTLS-based implementation using lwIP blocking TCP wrappers. */
#include <string.h>
#include <net_lwip.h>
#include <pit.h>
/* avoid hard include dependency; declare directly */
void sys_check_timeouts(void);
#include <net.h>
#include <heap.h>
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy_poll.h"
#include "mbedtls/debug.h"
/* local glue provides this symbol */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

/* DNS candidates for QEMU slirp and public resolvers */
static const uint32_t g_dns_candidates[] = {
	(10u<<24)|(0u<<16)|(2u<<8)|10u, /* gw */
	(10u<<24)|(0u<<16)|(2u<<8)|3u,  /* slirp */
	(1u<<24)|(1u<<16)|(1u<<8)|1u,   /* 1.1.1.1 */
	(8u<<24)|(8u<<16)|(8u<<8)|8u    /* 8.8.8.8 */
};

static void tls_log(const char* where, int ret) {
	/* negative mbedTLS codes are informative; print hex */
	kprintf("tls: %s ret=%d (0x%x)\n", where, ret, (unsigned)(ret < 0 ? (unsigned)(-ret) : (unsigned)ret));
}

/* mbedTLS timer callbacks (avoid MBEDTLS_TIMING_C) */
typedef struct {
	uint32_t intermediate_ms;
	uint32_t final_ms;
	uint64_t start_ms;
} ax_tls_timer_t;

static void ax_tls_set_timer(void *ctx, uint32_t int_ms, uint32_t fin_ms) {
	ax_tls_timer_t *t = (ax_tls_timer_t*)ctx;
	t->intermediate_ms = int_ms;
	t->final_ms = fin_ms;
	t->start_ms = pit_get_time_ms();
}

/* Return: -1 = cancel, 0 = none, 1 = intermediate, 2 = final */
static int ax_tls_get_timer(void *ctx) {
	ax_tls_timer_t *t = (ax_tls_timer_t*)ctx;
	if (t->final_ms == 0) return -1;
	uint64_t now = pit_get_time_ms();
	uint64_t elapsed = now - t->start_ms;
	if (t->intermediate_ms != 0 && elapsed >= t->intermediate_ms && elapsed < t->final_ms) return 1;
	if (elapsed >= t->final_ms) return 2;
	return 0;
}

static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
	lwip_tcp_handle_t* h = (lwip_tcp_handle_t*)ctx;
	size_t off = 0;
	uint64_t deadline = pit_get_time_ms() + 10000;
	while (off < len && pit_get_time_ms() < deadline) {
		int n = lwip_tcp_send(h, buf + off, len - off, 200);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		sys_check_timeouts();
		lwip_pump_io();
		pit_sleep_ms(2);
	}
	if (off == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
	qemu_debug_printf("bio: sent=%u\n", (unsigned)off);
	return (int)off;
}

static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
	lwip_tcp_handle_t* h = (lwip_tcp_handle_t*)ctx;
	/* Неблокирующая семантика: перед попыткой чтения один раз прокачаем стек,
	 * чтобы mbedTLS, вызывая нас в тесном цикле, всё же получал новые данные,
	 * не дожидаясь выхода во внешний цикл. */
	sys_check_timeouts();
	lwip_pump_io();
	size_t pending = lwip_tcp_pending(h);
	if (pending == 0) {
		kprintf("bio: want=%u pending=0 closed=%d err=%d\n",
		        (unsigned)len, lwip_tcp_is_closed(h), lwip_tcp_errflag(h));
	}
	/* Одно мгновенное чтение; если данных нет — WANT_READ. */
	int n = lwip_tcp_recv(h, buf, len, 0);
	if (n > 0) {
		if ((size_t)n >= 5) {
			uint8_t ct = buf[0];
			uint16_t ver = ((uint16_t)buf[1] << 8) | buf[2];
			uint16_t rl = ((uint16_t)buf[3] << 8) | buf[4];
			qemu_debug_printf("bio: recv=%u ct=%u ver=0x%04x len=%u\n", (unsigned)n, (unsigned)ct, (unsigned)ver, (unsigned)rl);
		} else {
			qemu_debug_printf("bio: recv=%u ct=%u\n", (unsigned)n, (unsigned)buf[0]);
		}
		return n;
	}
	if (n == 0) return 0; /* EOF */
	return MBEDTLS_ERR_SSL_WANT_READ;
}

/* mbedTLS expects this exact signature: return bytes, or 0 on timeout */
static int bio_recv_timeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout_ms) {
	lwip_tcp_handle_t* h = (lwip_tcp_handle_t*)ctx;
	size_t off = 0;
	uint64_t deadline = pit_get_time_ms() + (timeout_ms ? timeout_ms : 10000);

	/* Step 1: read TLS header (5 bytes) first so we know record length.
	 * Do short cycles: pump IO and try to read small chunks until header or timeout. */
	while (off < 5 && pit_get_time_ms() < deadline) {
		sys_check_timeouts();
		lwip_pump_io();
		int n = lwip_tcp_recv(h, buf + off, (size_t)(5 - off), 50);
		if (n > 0) { off += (size_t)n; continue; }
		if (n == 0) { /* EOF */ return 0; }
		/* no data yet — let other timers progress briefly */
		pit_sleep_ms(1);
	}
	/* If we didn't get anything by deadline, return 0 (timeout/EOF) */
	if (off == 0 && pit_get_time_ms() >= deadline) return 0;
	/* If we got partial header, return what we have (mbedTLS will handle WANT_READ) */
	if (off > 0 && off < 5) {
		qemu_debug_printf("bio: partial-hdr=%u\n", (unsigned)off);
		return (int)off;
	}

	/* We have at least 5 bytes: parse record length */
	uint16_t rec_len = ((uint16_t)buf[3] << 8) | buf[4];
	size_t total_needed = (size_t)5 + (size_t)rec_len;
	if (total_needed > len) total_needed = len;

	/* Step 2: prefer to wait until lwIP has accumulated a decent amount of data
	 * (use lwip_tcp_pending) to avoid repeated tiny reads. Then pull what's available. */
	while (off < total_needed && pit_get_time_ms() < deadline) {
		/* Compute remaining deadline in ms and pass it to consume to wait longer
		 * when a large record is expected. Use at least 100 ms per attempt. */
		uint64_t now = pit_get_time_ms();
		uint32_t rem_ms = (deadline > now) ? (uint32_t)(deadline - now) : 0;
		uint32_t pass_ms = rem_ms > 100 ? rem_ms : 100;
		/* Diagnostic: how much we're asking consume to provide this pass */
		qemu_debug_printf("tls: consume-request need=%u off=%u pass_ms=%u\n",
		        (unsigned)(total_needed - off), (unsigned)off, (unsigned)pass_ms);
		int n = lwip_tcp_consume(h, buf + off, total_needed - off, pass_ms);
		if (n > 0) {
			off += (size_t)n;
			/* If consume returned a small fragment but lwIP still has pending
			 * data, try to drain immediately with non-blocking reads to avoid
			 * leaving bytes sitting in RX buffer while mbedTLS loops. */
			if (off < total_needed) {
				size_t pending = lwip_tcp_pending(h);
				if (pending > 0) {
					qemu_debug_printf("tls: consume-drain pending=%u off=%u need=%u\n",
					        (unsigned)pending, (unsigned)off, (unsigned)total_needed);
					while (off < total_needed) {
						int m = lwip_tcp_recv(h, buf + off, total_needed - off, 0);
						if (m > 0) { off += (size_t)m; continue; }
						break;
					}
					qemu_debug_printf("tls: consume-drain done off=%u remaining_pending=%u\n",
					        (unsigned)off, (unsigned)lwip_tcp_pending(h));
				}
			}
			continue;
		}
		if (n == 0) break; /* EOF */
		/* n < 0 = timeout/error; pump IO and retry */
		sys_check_timeouts();
		lwip_pump_io();
		pit_sleep_ms(1);
	}

	/* Logging for debug */
	if (off >= 5) {
		uint8_t ct = buf[0];
		uint16_t ver = ((uint16_t)buf[1] << 8) | buf[2];
		uint16_t rl = ((uint16_t)buf[3] << 8) | buf[4];
		uint8_t hstype = (off >= 6) ? buf[5] : 0xFF;
		qemu_debug_printf("bio: recv=%u ct=%u ver=0x%04x len=%u hs=%u (need=%u)\n",
		        (unsigned)off, (unsigned)ct, (unsigned)ver, (unsigned)rl, (unsigned)hstype, (unsigned)rec_len);
	} else if (off > 0) {
		qemu_debug_printf("bio: recv=%u ct=%u\n", (unsigned)off, (unsigned)buf[0]);
	}
	return (int)off;
}
/* Fast entropy shim for CTR-DRBG: wrap our hardware poll to the expected signature */
static int ax_fast_entropy(void *p, unsigned char *out, size_t len) {
	size_t got = 0; (void)p;
	mbedtls_hardware_poll(NULL, out, len, &got);
	return got == len ? 0 : -1;
}

/* Simple RNG wrapper over hardware_poll to avoid CTR_DRBG seed hangs in this environment */
static int ax_rng(void *p, unsigned char *out, size_t len) {
	(void)p;
	size_t off = 0;
	while (off < len) {
		size_t got = 0; size_t want = (len - off) > 32 ? 32 : (len - off);
		if (mbedtls_hardware_poll(NULL, out + off, want, &got) != 0 || got == 0) {
			return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
		}
		off += got;
	}
	return 0;
}

static void ax_mbedtls_dbg(void *c, int level, const char *file, int line, const char *str) {
	(void)c;
	/* strip path */
	const char* f = file; const char* s = file;
	while (*s) { if (*s=='/' || *s=='\\') f = s + 1; s++; }
	kprintf("mbedtls[%d] %s:%d: %s\n", level, f, line, str);
}

int https_get(const char* host, const char* path, uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms) {
	if (!host || !path || !out || cap == 0) return -1;
	qemu_debug_printf("tls: enter https_get\n");
	kprintf("tls: enter\n");

	uint32_t ip = 0;
	/* Resolve host via our minimal DNS over UDP using static candidates */
	qemu_debug_printf("tls: pre-dns\n");
	kprintf("tls: pre-dns\n");
	kprintf("tls: dns %s ...\n", host);
	int dns_rc = -1;
	for (unsigned i = 0; i < sizeof(g_dns_candidates)/sizeof(g_dns_candidates[0]); i++) {
		uint32_t dns_ip = g_dns_candidates[i];
		qemu_debug_printf("tls: try dns=%d.%d.%d.%d\n",
			(dns_ip>>24)&0xFF,(dns_ip>>16)&0xFF,(dns_ip>>8)&0xFF,dns_ip&0xFF);
		kprintf("tls: try dns %d.%d.%d.%d\n",
			(dns_ip>>24)&0xFF,(dns_ip>>16)&0xFF,(dns_ip>>8)&0xFF,dns_ip&0xFF);
		if (net_dns_query(host, dns_ip, &ip, 3000) == 0) { dns_rc = 0; break; }
	}
	if (dns_rc != 0) { kprintf("tls: dns failed\n"); return -2; }
	kprintf("tls: ip %d.%d.%d.%d\n", (ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF);

	lwip_tcp_handle_t* th = lwip_tcp_connect(ip, 443, timeout_ms);
	if (!th) { kprintf("tls: tcp connect failed\n"); return -2; }
	kprintf("tls: tcp connect ok\n");

	kprintf("tls: alloc ctx...\n");
	/* Важно: НЕ размещать большие контексты mbedTLS на стеке — переполнение ядрового стека даст GPF. */
	typedef struct {
		mbedtls_ssl_context ssl;
		mbedtls_ssl_config conf;
		ax_tls_timer_t timer_ctx;
	} tls_ctx_t;
	tls_ctx_t *ctx = (tls_ctx_t*)kcalloc(1, sizeof(tls_ctx_t));
	if (!ctx) { lwip_tcp_close(th); return -12; }
	kprintf("tls: ctx=%p size=%u\n", ctx, (unsigned)sizeof(*ctx));

	kprintf("tls: ssl_init\n");
	mbedtls_ssl_init(&ctx->ssl);
	kprintf("tls: conf_init\n");
	mbedtls_ssl_config_init(&ctx->conf);

	int ret = 0;

	kprintf("tls: cfg...\n");
	if ((ret = mbedtls_ssl_config_defaults(&ctx->conf,
		MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
		tls_log("cfg", ret);
		lwip_tcp_close(th); kfree(ctx); return -4;
	}
#if defined(MBEDTLS_DEBUG_C)
	mbedtls_debug_set_threshold(2);
	mbedtls_ssl_conf_dbg(&ctx->conf, ax_mbedtls_dbg, NULL);
#endif
	/* Force TLS1.2 (GitHub requires modern ciphers) */
	mbedtls_ssl_conf_min_version(&ctx->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
	mbedtls_ssl_conf_max_version(&ctx->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
	/* Prefer AES-GCM/CHACHA20-POLY1305 */
	static const int ciphersuites[] = {
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
		0
	};
	mbedtls_ssl_conf_ciphersuites(&ctx->conf, ciphersuites);
	mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE); /* temporarily skip verification */
	mbedtls_ssl_conf_rng(&ctx->conf, ax_rng, NULL);

	kprintf("tls: setup...\n");
	if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf)) != 0) { tls_log("setup", ret); lwip_tcp_close(th); kfree(ctx); return -5; }
	kprintf("tls: sni=%s\n", host);
	mbedtls_ssl_set_hostname(&ctx->ssl, host);
	/* Provide timers to handshake */
	mbedtls_ssl_set_timer_cb(&ctx->ssl, &ctx->timer_ctx, ax_tls_set_timer, ax_tls_get_timer);
	/* handshake/read timeouts for blocking I/O */
	mbedtls_ssl_conf_read_timeout(&ctx->conf, 10000);

	/* BIO callbacks using lwIP blocking wrappers */
	kprintf("tls: bio\n");
	/* Некоторым серверам (и некоторым версиям mbedTLS) удобнее, чтобы
	 * recv callback блокировал и читал максимально возможное до таймаута.
	 * Установим оба callback'а (f_recv и f_recv_timeout) в блокирующую
	 * реализацию bio_recv_timeout — это даёт mbedTLS стабильный поток данных
	 * во время крупного Certificate. */
	mbedtls_ssl_set_bio(&ctx->ssl, th, bio_send, bio_recv_timeout, bio_recv_timeout);
	kprintf("tls: bio-set f_send=%p f_recv=%p f_timeout=%p\n",
	        (void*)ctx->ssl.f_send, (void*)ctx->ssl.f_recv, (void*)ctx->ssl.f_recv_timeout);

	/* Handshake (step-by-step to avoid internal blocking) */
	uint64_t start = pit_get_time_ms();
	kprintf("tls: handshake...\n");
	uint64_t last_dot = start;
	while (ctx->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
		int prev = ctx->ssl.state;
		ret = mbedtls_ssl_handshake_step(&ctx->ssl);
		if (ret == 0) {
			if (ctx->ssl.state != prev) {
				kprintf("tls: state=%d\n", ctx->ssl.state);
			}
		} else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
			kprintf("tls: want %s (state=%d)\n", ret == MBEDTLS_ERR_SSL_WANT_READ ? "read" : "write", ctx->ssl.state);
			/* drive network */
			sys_check_timeouts();
			lwip_pump_io();
			pit_sleep_ms(5);
		} else {
			tls_log("handshake", ret);
			lwip_tcp_close(th);
			mbedtls_ssl_free(&ctx->ssl);
			mbedtls_ssl_config_free(&ctx->conf);
			kfree(ctx);
			return -6;
		}
		/* always pump a bit to progress lwIP timers and RX */
		sys_check_timeouts();
		lwip_pump_io();
		pit_sleep_ms(1);
		uint64_t now = pit_get_time_ms();
		if (now - last_dot >= 500) { kprintf("."); last_dot = now; }
		if (now - start > timeout_ms) {
			kprintf("\n tls: handshake timeout\n");
			lwip_tcp_close(th);
			mbedtls_ssl_free(&ctx->ssl);
			mbedtls_ssl_config_free(&ctx->conf);
			kfree(ctx);
			return -7;
		}
	}
	kprintf("\n tls: handshake ok\n");

	/* Send HTTP request over TLS */
	char req[256];
	int req_len = 0;
	req_len = snprintf(req, sizeof(req),
	                   "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: AxonOS\r\nConnection: close\r\n\r\n",
	                   path, host);
	if (req_len <= 0) { lwip_tcp_close(th); return -8; }
	int written = 0;
	while (written < req_len) {
		ret = mbedtls_ssl_write(&ctx->ssl, (const unsigned char*)req + written, req_len - written);
		if (ret > 0) { written += ret; continue; }
		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) { sys_check_timeouts(); lwip_pump_io(); continue; }
		tls_log("write", ret);
		lwip_tcp_close(th); return -9;
	}

	/* Read response */
	size_t total = 0;
	while (1) {
		ret = mbedtls_ssl_read(&ctx->ssl, out + total, (int)(cap - total));
		if (ret > 0) {
			total += ret;
			if (total == cap) break;
			continue;
		}
		if (ret == 0) break; /* EOF */
		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) { sys_check_timeouts(); lwip_pump_io(); continue; }
		tls_log("read", ret);
		lwip_tcp_close(th); return -10;
	}
	if (out_len) *out_len = total;

	mbedtls_ssl_close_notify(&ctx->ssl);
	lwip_tcp_close(th);
	mbedtls_ssl_free(&ctx->ssl);
	mbedtls_ssl_config_free(&ctx->conf);
	kfree(ctx);
	return 0;
}

int https_get_to_file(const char* host, const char* path, const char* out_path, uint32_t timeout_ms, int soft_wrap_cols) {
	if (!host || !path || !out_path) return -1;
	kprintf("tls:file enter host=%s path=%s -> %s\n", host, path, out_path);
	size_t cap = 32768;
	uint8_t *buf = (uint8_t*)kmalloc(cap);
	kprintf("tls:file buf=%p cap=%u\n", buf, (unsigned)cap);
	if (!buf) return -2;
	size_t len = 0;
	kprintf("tls:file call https_get=%p\n", https_get);
	int rc = https_get(host, path, buf, cap, &len, timeout_ms);
	if (rc != 0) { kfree(buf); return rc; }
	/* strip headers */
	size_t off = 0;
	for (size_t i=0;i+3<len;i++) { if (buf[i]=='\r'&&buf[i+1]=='\n'&&buf[i+2]=='\r'&&buf[i+3]=='\n'){ off=i+4; break; } }
	if (off==0) { for (size_t i=0;i+1<len;i++){ if (buf[i]=='\n'&&buf[i+1]=='\n'){ off=i+2; break; } } }
	struct fs_file *f = fs_create_file(out_path);
	if (!f) { kfree(buf); return -3; }
	size_t file_off = 0;
	if (soft_wrap_cols <= 0) {
		ssize_t wr = fs_write(f, buf+off, len-off, file_off);
		if (wr > 0) file_off += (size_t)wr;
	} else {
		int col = 0; uint8_t nl = '\n';
		for (size_t i=off;i<len;i++) {
			uint8_t ch = buf[i];
			if (ch=='\r') { col=0; continue; }
			ssize_t wr = fs_write(f, &ch, 1, file_off); (void)wr; file_off++;
			if (ch=='\n') { col=0; continue; }
			if (++col >= soft_wrap_cols) { fs_write(f, &nl, 1, file_off); file_off++; col=0; }
		}
	}
	fs_file_free(f);
	kfree(buf);
	return 0;
}
#endif


