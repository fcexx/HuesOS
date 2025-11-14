#include <stdint.h>
#include <stddef.h>
#include "net_lwip.h"
#include <e1000.h>
#include <pit.h>
#include <heap.h>
#include <string.h>

#ifdef USE_LWIP
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/ip4.h"
#include "netif/ethernet.h"
#include "lwip/etharp.h"

static struct netif g_netif;
static uint8_t g_hwaddr[6];

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
	(void)netif;
	uint8_t frame[1600];
	size_t off = 0;
	for (struct pbuf* q = p; q; q = q->next) {
		if (off + q->len > sizeof(frame)) return ERR_MEM;
		memcpy(frame + off, q->payload, q->len);
		off += q->len;
	}
	if (off < 60) off = 60;
	return e1000_send(frame, off) == 0 ? ERR_OK : ERR_IF;
}

static err_t e1000_netif_init(struct netif* netif) {
	netif->name[0] = 'e'; netif->name[1] = '0';
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
	memcpy(netif->hwaddr, g_hwaddr, 6);
	netif->hwaddr_len = 6;
	return ERR_OK;
}

static void service_input(void) {
	uint8_t buf[2048]; size_t len = 0;
	for (int i = 0; i < 1024; i++) {
		int r = e1000_poll(buf, sizeof(buf), &len);
		if (r != 1) break;
		if (len == 0 || len > sizeof(buf)) continue;
		/* Diagnostic: parse Ethernet/IP/TCP headers to trace large TLS segments */
		if (len >= 14) {
			uint16_t ethertype = (uint16_t)((buf[12] << 8) | buf[13]);
			if (ethertype == 0x0800 && len >= 14 + 20) { /* IPv4 */
				uint8_t ihl = buf[14] & 0x0F;
				uint8_t proto = buf[14 + 9];
				uint8_t sa0 = buf[14 + 12], sa1 = buf[14 + 13], sa2 = buf[14 + 14], sa3 = buf[14 + 15];
				uint8_t da0 = buf[14 + 16], da1 = buf[14 + 17], da2 = buf[14 + 18], da3 = buf[14 + 19];
				if (proto == 6 && len >= 14 + ihl * 4 + 14) { /* TCP */
					size_t tcp_off = 14 + ihl * 4;
					uint16_t srcp = (uint16_t)((buf[tcp_off] << 8) | buf[tcp_off + 1]);
					uint16_t dstp = (uint16_t)((buf[tcp_off + 2] << 8) | buf[tcp_off + 3]);
					uint32_t seq = ((uint32_t)buf[tcp_off + 4] << 24) |
					               ((uint32_t)buf[tcp_off + 5] << 16) |
					               ((uint32_t)buf[tcp_off + 6] << 8) |
					               ((uint32_t)buf[tcp_off + 7]);
				qemu_debug_printf("netif: ethertype=0x%04x ip proto=%u %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u seq=%u totlen=%u\n",
					        ethertype, proto, sa0, sa1, sa2, sa3, (unsigned)srcp, da0, da1, da2, da3, (unsigned)dstp, (unsigned)seq, (unsigned)len);
				} else {
					qemu_debug_printf("netif: ethertype=0x%04x ip proto=%u %u.%u.%u.%u -> %u.%u.%u.%u len=%u\n",
					        ethertype, proto, sa0, sa1, sa2, sa3, da0, da1, da2, da3, (unsigned)len);
				}
			} else {
				qemu_debug_printf("netif: ethertype=0x%04x len=%u\n", ethertype, (unsigned)len);
			}
		}
		struct pbuf* p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
		if (!p) continue;
		if (g_netif.input == NULL) { pbuf_free(p); continue; }
		size_t copied = 0;
		for (struct pbuf* q = p; q; q = q->next) {
			size_t to_copy = q->len;
			memcpy(q->payload, buf + copied, to_copy);
			copied += to_copy;
		}
		g_netif.input(p, &g_netif);
	}
}

void lwip_pump_io(void) {
	sys_check_timeouts();
	service_input();
}
int lwip_stack_init(uint32_t ip, uint32_t netmask, uint32_t gateway) {
	e1000_get_mac(g_hwaddr);
	lwip_init();
	ip4_addr_t ip4, nm, gw;
	IP4_ADDR(&ip4, (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF);
	IP4_ADDR(&nm, (netmask>>24)&0xFF, (netmask>>16)&0xFF, (netmask>>8)&0xFF, netmask&0xFF);
	IP4_ADDR(&gw, (gateway>>24)&0xFF, (gateway>>16)&0xFF, (gateway>>8)&0xFF, gateway&0xFF);
	netif_add(&g_netif, &ip4, &nm, &gw, NULL, e1000_netif_init, ethernet_input);
	netif_set_default(&g_netif);
	netif_set_up(&g_netif);
	netif_set_link_up(&g_netif);
	return 0;
}

struct http_ctx {
	struct tcp_pcb* pcb;
	const char* path;
	uint8_t* out;
	size_t cap;
	size_t written;
	int done;
	err_t last_err;
	uint64_t deadline_ms;
};

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
	struct http_ctx* ctx = (struct http_ctx*)arg;
	if (!p) { ctx->done = 1; return ERR_OK; }
	if (err != ERR_OK) { pbuf_free(p); ctx->last_err = err; ctx->done = 1; return err; }
	for (struct pbuf* q = p; q; q = q->next) {
		size_t to_copy = q->len;
		if (ctx->written + to_copy > ctx->cap) to_copy = ctx->cap - ctx->written;
		memcpy(ctx->out + ctx->written, q->payload, to_copy);
		ctx->written += to_copy;
	}
	tcp_recved(tpcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static err_t http_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
	(void)err;
	struct http_ctx* ctx = (struct http_ctx*)arg;
	char req[256];
	const char* path = ctx->path ? ctx->path : "/";
	size_t pos = 0;
	const char* a = "GET ";
	const char* b = " HTTP/1.1\r\nHost: www.google.com\r\nUser-Agent: AxonOS\r\nConnection: close\r\n\r\n";
	size_t al = 4, bl = 74;
	if (pos + al < sizeof(req)) { memcpy(req + pos, a, al); pos += al; }
	size_t pl = 0; while (path[pl] && pos + 1 < sizeof(req)) { req[pos++] = path[pl++]; if (pos >= sizeof(req)) break; }
	if (pos + bl < sizeof(req)) { memcpy(req + pos, b, bl); pos += bl; }
	tcp_write(tpcb, req, (u16_t)pos, TCP_WRITE_FLAG_COPY);
	tcp_output(tpcb);
	return ERR_OK;
}

static void http_err(void *arg, err_t err) {
	struct http_ctx* ctx = (struct http_ctx*)arg;
	ctx->last_err = err;
	ctx->done = 1;
}

int lwip_http_get_ip(uint32_t dst_ip, uint16_t dst_port, const char* path,
                     uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms) {
	struct http_ctx ctx = {0};
	ctx.path = path;
	ctx.out = out;
	ctx.cap = cap;
	ctx.written = 0;
	ctx.done = 0;
	ctx.last_err = ERR_OK;

	ip4_addr_t dip; IP4_ADDR(&dip, (dst_ip>>24)&0xFF,(dst_ip>>16)&0xFF,(dst_ip>>8)&0xFF,dst_ip&0xFF);
	struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
	if (!pcb) return -1;
	ctx.pcb = pcb;
	tcp_arg(pcb, &ctx);
	tcp_err(pcb, http_err);
	tcp_recv(pcb, http_recv);
	err_t er = tcp_connect(pcb, &dip, dst_port, http_connected);
	if (er != ERR_OK) { tcp_abort(pcb); return -2; }

	uint64_t start = pit_get_time_ms();
	while (!ctx.done && (pit_get_time_ms() - start < timeout_ms)) {
		sys_check_timeouts();
		service_input();
	}
	if (!ctx.done) { tcp_abort(pcb); return -3; }
	if (out_len) *out_len = ctx.written;
	return ctx.last_err == ERR_OK ? 0 : -4;
}

/* ------------- Blocking TCP wrapper for upper layers (e.g., TLS) ------------- */
struct lwip_tcp_handle {
	struct tcp_pcb* pcb;
	int connected;
	int closed;
	int err_seen;
	uint8_t* rxbuf;
	size_t rxlen;
	size_t rxcap;
};

static err_t tcpblk_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
	struct lwip_tcp_handle* h = (struct lwip_tcp_handle*)arg;
	if (!p) { h->closed = 1; return ERR_OK; }
	if (err != ERR_OK) { pbuf_free(p); h->err_seen = 1; return err; }
	/* append to rx buffer */
	/* quick visibility that RX is flowing */
	kprintf("tcp: rx tot=%u\n", (unsigned)p->tot_len);
	size_t added = 0;
	for (struct pbuf* q = p; q; q = q->next) {
		size_t to_copy = q->len;
		/* grow buffer if needed to avoid truncation */
		if (h->rxlen + to_copy > h->rxcap) {
			size_t need = h->rxlen + to_copy;
			size_t newcap = h->rxcap ? h->rxcap : 1024;
			while (newcap < need) newcap *= 2;
			uint8_t* nb = (uint8_t*)kmalloc(newcap);
			if (!nb) { pbuf_free(p); h->err_seen = 1; return ERR_MEM; }
			memcpy(nb, h->rxbuf, h->rxlen);
			if (h->rxbuf) kfree(h->rxbuf);
			h->rxbuf = nb;
			h->rxcap = newcap;
		}
		memcpy(h->rxbuf + h->rxlen, q->payload, to_copy);
		h->rxlen += to_copy;
		added += to_copy;
	}
	qemu_debug_printf("tcpblk_recv: appended=%u new_rxlen=%u rxcap=%u\n", (unsigned)added, (unsigned)h->rxlen, (unsigned)h->rxcap);
	tcp_recved(tpcb, p->tot_len);
	/* ensure immediate ACK/window update */
	tcp_output(tpcb);
	pbuf_free(p);
	return ERR_OK;
}

static err_t tcpblk_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
	(void)tpcb;
	struct lwip_tcp_handle* h = (struct lwip_tcp_handle*)arg;
	h->connected = (err == ERR_OK);
	h->err_seen = (err != ERR_OK);
	return ERR_OK;
}

static void tcpblk_err(void *arg, err_t err) {
	struct lwip_tcp_handle* h = (struct lwip_tcp_handle*)arg;
	h->err_seen = 1; (void)err;
}

lwip_tcp_handle_t* lwip_tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms) {
	struct lwip_tcp_handle* h = (struct lwip_tcp_handle*)kmalloc(sizeof(*h));
	if (!h) return NULL;
	memset(h, 0, sizeof(*h));
	h->pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
	if (!h->pcb) { kfree(h); return NULL; }
	/* держим большой приёмный буфер, чтобы вместить длинные TLS‑фрагменты
	 * (например, Certificate ~15 КБ) без излишнего дробления */
	h->rxcap = 65536;
	h->rxbuf = (uint8_t*)kmalloc(h->rxcap);
	if (!h->rxbuf) { tcp_abort(h->pcb); kfree(h); return NULL; }
	ip4_addr_t dip; IP4_ADDR(&dip, (dst_ip>>24)&0xFF,(dst_ip>>16)&0xFF,(dst_ip>>8)&0xFF,dst_ip&0xFF);
	tcp_arg(h->pcb, h);
	tcp_err(h->pcb, tcpblk_err);
	tcp_recv(h->pcb, tcpblk_recv);
	err_t er = tcp_connect(h->pcb, &dip, dst_port, tcpblk_connected);
	if (er != ERR_OK) { tcp_abort(h->pcb); kfree(h->rxbuf); kfree(h); return NULL; }
	/* Disable Nagle to reduce handshake latency */
#ifdef LWIP_TCP
	tcp_nagle_disable(h->pcb);
#endif
	uint64_t start = pit_get_time_ms();
	while (!h->connected && !h->err_seen && (pit_get_time_ms() - start < timeout_ms)) {
		sys_check_timeouts();
		service_input();
	}
	if (!h->connected || h->err_seen) { tcp_abort(h->pcb); kfree(h->rxbuf); kfree(h); return NULL; }
	return h;
}

int lwip_tcp_send(lwip_tcp_handle_t* h, const uint8_t* data, size_t len, uint32_t timeout_ms) {
	size_t sent = 0;
	uint64_t deadline = pit_get_time_ms() + timeout_ms;
	while (sent < len) {
		u16_t chunk = (u16_t)((len - sent) > 1024 ? 1024 : (len - sent));
		err_t er = tcp_write(h->pcb, data + sent, chunk, TCP_WRITE_FLAG_COPY);
		if (er == ERR_MEM) { sys_check_timeouts(); service_input(); continue; }
		if (er != ERR_OK) return -1;
		tcp_output(h->pcb);
		sent += chunk;
		if (pit_get_time_ms() > deadline) break;
		sys_check_timeouts(); service_input();
	}
	return (int)sent;
}

int lwip_tcp_recv(lwip_tcp_handle_t* h, uint8_t* out, size_t len, uint32_t timeout_ms) {
	uint64_t start = pit_get_time_ms();
	while (h->rxlen == 0 && !h->closed && !h->err_seen && (pit_get_time_ms() - start < timeout_ms)) {
		sys_check_timeouts();
		service_input();
	}
	if (h->rxlen == 0 && h->closed) return 0;
	if (h->rxlen == 0 && (h->err_seen || pit_get_time_ms() - start >= timeout_ms)) return -1;
	size_t to_copy = h->rxlen < len ? h->rxlen : len;
	memcpy(out, h->rxbuf, to_copy);
	/* compact remaining */
	memmove(h->rxbuf, h->rxbuf + to_copy, h->rxlen - to_copy);
	h->rxlen -= to_copy;
	/* После выдачи данных приложению стимулируем немедленную отправку ACK/окна */
	tcp_output(h->pcb);
	qemu_debug_printf("lwip_tcp_recv: returned=%u remaining_rxlen=%u\n", (unsigned)to_copy, (unsigned)h->rxlen);
	return (int)to_copy;
}

int lwip_tcp_consume(lwip_tcp_handle_t* h, uint8_t* out, size_t want, uint32_t timeout_ms) {
	if (!h || !out || want == 0) return -1;
	uint64_t start = pit_get_time_ms();
	size_t got = 0;
	qemu_debug_printf("lwip_tcp_consume: enter want=%u rxlen=%u rxcap=%u closed=%d err=%d timeout=%u\n",
	        (unsigned)want, (unsigned)h->rxlen, (unsigned)h->rxcap, (int)h->closed, (int)h->err_seen, (unsigned)timeout_ms);
	while (got < want && pit_get_time_ms() - start < timeout_ms) {
		/* if nothing available, try to pump input to fill buffer */
		if (h->rxlen == 0 && !h->closed && !h->err_seen) {
			sys_check_timeouts();
			service_input();
			/* small sleep to avoid busy spin */
			pit_sleep_ms(1);
			continue;
		}
		if (h->rxlen == 0) break;

		size_t need = want - got;

		/* If we have some data but less than requested, give the stack
		 * a short opportunity to accumulate more before returning a tiny
		 * fragment. This helps mbedTLS fetch large TLS records in fewer
		 * syscalls and avoids long handshakes made of many tiny reads. */
		if (h->rxlen > 0 && h->rxlen < need) {
			uint64_t inner_start = pit_get_time_ms();
			/* Adaptive wait: be more patient for large wants (server certificate),
			 * but keep short latency for small reads. */
			uint32_t inner_deadline_ms = (want > 4096) ? 200 : 80;
			/* target threshold: try to get either full need or a reasonable chunk.
			 * For very large needs prefer larger aggregation to reduce syscalls. */
			size_t target = need;
			if (target > 4096) target = 4096;
			else if (target > 1024) target = 2048;
			else if (target > 512) target = 1024;
			/* if already at or above target, skip waiting */
			if (h->rxlen < target && !h->closed && !h->err_seen) {
				while (pit_get_time_ms() - inner_start < inner_deadline_ms) {
					sys_check_timeouts();
					service_input();
					if (h->rxlen >= target) break;
					/* tiny backoff */
					pit_sleep_ms(1);
				}
			}
		}

		size_t avail = h->rxlen < need ? h->rxlen : need;
		qemu_debug_printf("lwip_tcp_consume: need=%u have=%u -> avail=%u\n", (unsigned)need, (unsigned)h->rxlen, (unsigned)avail);
		memcpy(out + got, h->rxbuf, avail);
		/* compact remaining */
		memmove(h->rxbuf, h->rxbuf + avail, h->rxlen - avail);
		h->rxlen -= avail;
		got += avail;
		qemu_debug_printf("lwip_tcp_consume: copied=%u got_total=%u remaining_rxlen=%u\n", (unsigned)avail, (unsigned)got, (unsigned)h->rxlen);
		/* ensure ACK/window */
		tcp_output(h->pcb);
	}
	if (got == 0 && h->closed) return 0;
	if (got == 0 && (h->err_seen || pit_get_time_ms() - start >= timeout_ms)) return -1;
	qemu_debug_printf("lwip_tcp_consume: got=%u remaining_rxlen=%u\n", (unsigned)got, (unsigned)h->rxlen);
	return (int)got;
}

void lwip_tcp_close(lwip_tcp_handle_t* h) {
	if (!h) return;
	if (h->pcb) {
		tcp_arg(h->pcb, NULL);
		tcp_recv(h->pcb, NULL);
		tcp_err(h->pcb, NULL);
		tcp_close(h->pcb);
	}
	if (h->rxbuf) kfree(h->rxbuf);
	kfree(h);
}

size_t lwip_tcp_pending(const lwip_tcp_handle_t* h) {
	return h ? h->rxlen : 0;
}

int lwip_tcp_is_closed(const lwip_tcp_handle_t* h) {
	return h ? h->closed : 0;
}

int lwip_tcp_errflag(const lwip_tcp_handle_t* h) {
	return h ? h->err_seen : 0;
}

/* lwIP requires sys_now() for timers; bridge to PIT */
u32_t sys_now(void) {
	return (u32_t)pit_get_time_ms();
}

/* ---------------- DNS over lwIP UDP (avoid competing with lwIP input path) ---------------- */
struct __attribute__((packed)) ax_dns_header {
	u16_t id, flags, qdcount, ancount, nscount, arcount;
};

static int ax_build_dns_query(const char* host, uint8_t* out, size_t* out_len, u16_t txid) {
	uint8_t* p = out;
	struct ax_dns_header* h = (struct ax_dns_header*)p;
	h->id = lwip_htons(txid);
	h->flags = lwip_htons(0x0100); /* RD=1 */
	h->qdcount = lwip_htons(1);
	h->ancount = 0; h->nscount = 0; h->arcount = 0;
	p += sizeof(struct ax_dns_header);
	const char* s = host;
	while (*s) {
		const char* label = s;
		while (*s && *s != '.') s++;
		size_t lab_len = (size_t)(s - label);
		if (lab_len > 63) return -1;
		*p++ = (uint8_t)lab_len;
		for (size_t i = 0; i < lab_len; i++) *p++ = (uint8_t)label[i];
		if (*s == '.') s++;
	}
	*p++ = 0;
	/* QTYPE=A, QCLASS=IN */
	*p++ = 0; *p++ = 1;
	*p++ = 0; *p++ = 1;
	*out_len = (size_t)(p - out);
	return 0;
}

typedef struct {
	u16_t sport;
	u16_t txid;
	uint32_t out_ip_be;
	int done;
} ax_dns_ctx_t;

static void ax_dns_recv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
	(void)pcb; (void)addr;
	ax_dns_ctx_t* ctx = (ax_dns_ctx_t*)arg;
	if (!p) return;
	/* 'port' from lwIP UDP callback is in host byte order */
	if (port != 53) { pbuf_free(p); return; }
	/* Expect at least header */
	if (p->tot_len < (u16_t)sizeof(struct ax_dns_header)) { pbuf_free(p); return; }
	uint8_t buf[1024];
	u16_t copy = p->tot_len < sizeof(buf) ? p->tot_len : (u16_t)sizeof(buf);
	pbuf_copy_partial(p, buf, copy, 0);
	pbuf_free(p);
	const uint8_t* dns = buf;
	const struct ax_dns_header* dh = (const struct ax_dns_header*)dns;
	if (dh->id != lwip_htons(ctx->txid)) return;
	const uint8_t* q = dns + sizeof(struct ax_dns_header);
	/* skip QNAME */
	while (*q && (q - dns) < copy) q += (*q) + 1;
	if ((q - dns) >= copy) return;
	q++; /* zero label */
	if ((q - dns) + 4 > copy) return; /* QTYPE/QCLASS */
	q += 4;
	u16_t an = lwip_ntohs(dh->ancount);
	for (u16_t i = 0; i < an; i++) {
		if ((q - dns) + 12 > copy) break;
		/* NAME (maybe pointer) */
		if ((*q & 0xC0) == 0xC0) {
			q += 2;
		} else {
			while (*q && (q - dns) < copy) q += (*q) + 1;
			if ((q - dns) >= copy) break;
			q++;
		}
		if ((q - dns) + 10 > copy) break;
		u16_t type = (u16_t)((q[0] << 8) | q[1]);
		u16_t rdlen = (u16_t)((q[8] << 8) | q[9]);
		q += 10;
		if ((q - dns) + rdlen > copy) break;
		if (type == 1 && rdlen == 4) { /* A */
			ctx->out_ip_be = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | (uint32_t)q[3];
			ctx->done = 1;
			return;
		}
		q += rdlen;
	}
}

/* Public resolver API used by TLS and shell */
int net_dns_query(const char* host, uint32_t dns_ip_be, uint32_t* out_ip_be, uint32_t timeout_ms) {
	if (!host || !out_ip_be) return -1;
	kprintf("dns: start host=%s\n", host);
	struct udp_pcb* pcb = udp_new_ip_type(IPADDR_TYPE_V4);
	if (!pcb) { kprintf("dns: pcb alloc fail\n"); return -1; }
	ax_dns_ctx_t ctx; memset(&ctx, 0, sizeof(ctx));
	ctx.sport = (u16_t)(40000 + ((pit_get_time_ms() >> 4) & 0xFF));
	ctx.txid = (u16_t)(pit_get_time_ms() & 0xFFFF);
	kprintf("dns: bind sport=%u\n", (unsigned)ctx.sport);
	udp_recv(pcb, ax_dns_recv, &ctx);
	if (udp_bind(pcb, IP4_ADDR_ANY4, ctx.sport) != ERR_OK) { kprintf("dns: bind fail\n"); udp_remove(pcb); return -1; }
	uint8_t qbuf[512]; size_t qlen = 0;
	if (ax_build_dns_query(host, qbuf, &qlen, ctx.txid) != 0) { kprintf("dns: build fail\n"); udp_remove(pcb); return -3; }
	ip4_addr_t dst; IP4_ADDR(&dst, (dns_ip_be>>24)&0xFF, (dns_ip_be>>16)&0xFF, (dns_ip_be>>8)&0xFF, dns_ip_be&0xFF);
	/* initial send */
	{
		struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)qlen, PBUF_POOL);
		if (!p) { kprintf("dns: pbuf alloc fail\n"); udp_remove(pcb); return -3; }
		pbuf_take(p, qbuf, (u16_t)qlen);
		udp_sendto(pcb, p, (ip_addr_t*)&dst, 53);
		pbuf_free(p);
	}
	uint64_t deadline = pit_get_time_ms() + timeout_ms;
	uint64_t last_send = pit_get_time_ms();
	while (!ctx.done && pit_get_time_ms() < deadline) {
		lwip_pump_io();
		uint64_t now = pit_get_time_ms();
		if (now - last_send >= 500) {
			struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)qlen, PBUF_POOL);
			if (p) {
				pbuf_take(p, qbuf, (u16_t)qlen);
				udp_sendto(pcb, p, (ip_addr_t*)&dst, 53);
				pbuf_free(p);
			}
			last_send = now;
		}
	}
	udp_remove(pcb);
	if (!ctx.done) return -4;
	*out_ip_be = ctx.out_ip_be;
	return 0;
}
#else
int lwip_stack_init(uint32_t ip, uint32_t netmask, uint32_t gateway) {
	(void)ip; (void)netmask; (void)gateway;
	return -1;
}
int lwip_http_get_ip(uint32_t dst_ip, uint16_t dst_port, const char* path,
                     uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms) {
	(void)dst_ip; (void)dst_port; (void)path; (void)out; (void)cap; (void)out_len; (void)timeout_ms;
	return -1;
}
#endif


