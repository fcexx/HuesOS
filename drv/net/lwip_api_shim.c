#include "net_lwip.h"
#include <stdint.h>
#include <stddef.h>
#include <net.h>
#include <heap.h>
#include <string.h>
#include <pit.h>
#include <e1000.h>

/* Stateful TCP shim:
 * - Implements a minimal TCP client sufficient for TLS: three-way handshake,
 *   PSH/ACK sends, receive data and ACKs.
 * - No retransmissions, no congestion control, fixed window.
 * - Parses incoming frames from e1000_poll and sends frames with e1000_send.
 *
 * This shim is intended as a stop-gap until a full TCP stack is implemented.
 */

/* Byte-swap helpers */
static uint16_t shim_bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static uint32_t shim_bswap32(uint32_t v) {
	return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

/* IP checksum (ones-complement) */
static uint16_t shim_ip_checksum(const void* data, size_t len) {
	const uint16_t* w = (const uint16_t*)data;
	uint32_t sum = 0;
	while (len > 1) { sum += *w++; len -= 2; }
	if (len) sum += *(const uint8_t*)w;
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	return (uint16_t)(~sum);
}

/* TCP checksum over pseudo-header + tcp header + payload */
static uint16_t shim_tcp_checksum(const uint8_t* src_addr_be, const uint8_t* dst_addr_be,
                                  const uint8_t* tcp_hdr, size_t tcp_hdr_len, const uint8_t* payload, size_t pay_len) {
	uint32_t sum = 0;
	/* pseudo header: src(4) + dst(4) + zero + proto + tcp length */
	const uint16_t* ws = (const uint16_t*)src_addr_be;
	sum += ws[0] + ws[1];
	const uint16_t* wd = (const uint16_t*)dst_addr_be;
	sum += wd[0] + wd[1];
	sum += (uint16_t)6; /* protocol TCP (0x06) as 16-bit */
	uint16_t tlen = (uint16_t)(tcp_hdr_len + pay_len);
	sum += shim_bswap16(tlen);
	/* TCP header and payload */
	const uint16_t* w = (const uint16_t*)tcp_hdr;
	size_t l = tcp_hdr_len;
	while (l > 1) { sum += *w++; l -= 2; }
	if (l) sum += ((uint16_t)(*((const uint8_t*)w))) << 8;
	/* payload */
	w = (const uint16_t*)payload;
	l = pay_len;
	while (l > 1) { sum += *w++; l -= 2; }
	if (l) sum += ((uint16_t)(*((const uint8_t*)w))) << 8;
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	return (uint16_t)(~sum) ? (uint16_t)(~sum) : 0xFFFF;
}

/* Minimal shim implementations for lwIP helper API so project builds
 * without third_party/lwip. These provide a tiny blocking "request-on-send"
 * behavior: when upper layer writes an HTTP GET request, we invoke the
 * existing raw `net_http_get()` helper to perform the full request and
 * buffer the response for subsequent reads.
 *
 * This is a pragmatic bridge to allow HTTPS/HTTP user flows to work
 * while you implement a full TCP stack. It does NOT implement TCP.
 */

struct lwip_tcp_handle {
	uint32_t dst_ip;
	uint16_t dst_port;
	uint8_t *rxbuf;
	size_t rxcap;
	size_t rxlen;
	int closed;
	int connected;
	uint16_t sport;
	uint32_t iss;    /* initial send seq */
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint16_t rcv_window;
	uint8_t dst_mac[6];
	uint8_t src_mac[6];
	int established;
};
/* end struct */
lwip_tcp_handle_t* lwip_tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms) {
	/* Allocate handle and perform three-way handshake */
	lwip_tcp_handle_t* h = (lwip_tcp_handle_t*)kmalloc(sizeof(*h));
	if (!h) return NULL;
	memset(h, 0, sizeof(*h));
	h->dst_ip = dst_ip;
	h->dst_port = dst_port;
	h->rxcap = 131072;
	h->rxbuf = (uint8_t*)kmalloc(h->rxcap);
	if (!h->rxbuf) { kfree(h); return NULL; }
	h->rxlen = 0;
	h->closed = 0;
	h->connected = 0;
	h->established = 0;
	/* pick ephemeral source port and initial sequence */
	h->sport = (uint16_t)(40000 + ((pit_get_time_ms() >> 4) & 0xFF));
	h->iss = (uint32_t)(pit_get_time_ms() & 0xFFFFFFFF);
	h->snd_nxt = h->iss + 1;
	h->rcv_nxt = 0;
	h->rcv_window = 0x4000;
	/* read NIC MAC */
	e1000_get_mac(h->src_mac);

	/* Resolve ARP for next-hop: use gateway MAC (common case for remote IPs) */
	uint32_t gw = net_get_gateway_ip();
	if (gw == 0 || net_arp_resolve(gw, h->dst_mac, timeout_ms) != 0) {
		kfree(h->rxbuf); kfree(h); return NULL;
	}

	/* Send SYN */
	/* Build frame inline (Ethernet + IPv4 + TCP) */
	{
		uint8_t frame[1514];
		memset(frame, 0, sizeof(frame));
		/* Ethernet */
		struct eth_hdr {
			uint8_t dst[6]; uint8_t src[6]; uint16_t type;
		} __attribute__((packed));
		struct eth_hdr* eh = (struct eth_hdr*)frame;
		memcpy(eh->dst, h->dst_mac, 6);
		memcpy(eh->src, h->src_mac, 6);
		eh->type = (uint16_t)((0x08<<8)|0x00); /* 0x0800 */
		/* IPv4 */
		struct ipv4_hdr {
			uint8_t ver_ihl; uint8_t tos; uint16_t tot_len; uint16_t id;
			uint16_t flags_frag; uint8_t ttl; uint8_t proto; uint16_t hdr_checksum;
			uint32_t saddr; uint32_t daddr;
		} __attribute__((packed));
		struct ipv4_hdr* ip = (struct ipv4_hdr*)(frame + sizeof(struct eth_hdr));
		struct tcp_hdr {
			uint16_t src_port; uint16_t dst_port; uint32_t seq; uint32_t ack;
			uint8_t data_off_rsv; uint8_t flags; uint16_t window; uint16_t checksum; uint16_t urgptr;
		} __attribute__((packed));
		struct tcp_hdr* tcp = (struct tcp_hdr*)((uint8_t*)ip + sizeof(struct ipv4_hdr));
		size_t tcp_hlen = sizeof(struct tcp_hdr);
		size_t ip_len = sizeof(struct ipv4_hdr) + tcp_hlen;
		ip->ver_ihl = 0x45; ip->tos = 0; ip->tot_len = shim_bswap16((uint16_t)ip_len);
		ip->id = 0; ip->flags_frag = shim_bswap16(0x4000);
		ip->ttl = 64; ip->proto = 6; ip->hdr_checksum = 0;
		{ uint32_t myip = net_get_my_ip();
		  ip->saddr = shim_bswap32(myip);
		  ip->daddr = shim_bswap32(dst_ip);
		}
		/* tcp */
		tcp->src_port = (uint16_t)((h->sport>>8)| (h->sport<<8));
		tcp->dst_port = (uint16_t)((dst_port>>8)|(dst_port<<8));
		tcp->seq = ((h->iss>>24)&0xFF) | (((h->iss>>16)&0xFF)<<8) | (((h->iss>>8)&0xFF)<<16) | ((h->iss&0xFF)<<24);
		tcp->ack = 0;
		tcp->data_off_rsv = (uint8_t)((tcp_hlen/4)<<4);
		tcp->flags = 0x02; /* SYN */
		tcp->window = (uint16_t)((h->rcv_window>>8)|(h->rcv_window<<8));
		tcp->checksum = 0; tcp->urgptr = 0;
		/* compute basic IPv4 header checksum simply as zero (device may accept) */
		/* compute IPv4 header checksum */
		ip->hdr_checksum = shim_ip_checksum(ip, sizeof(struct ipv4_hdr));
		/* compute TCP checksum */
		{
			uint8_t *tcp_bytes = (uint8_t*)tcp;
			tcp->checksum = 0;
			uint8_t src_be[4] = { (uint8_t)( (net_get_my_ip()>>24)&0xFF ), (uint8_t)( (net_get_my_ip()>>16)&0xFF ), (uint8_t)( (net_get_my_ip()>>8)&0xFF ), (uint8_t)( net_get_my_ip() & 0xFF ) };
			uint8_t dst_be[4] = { (uint8_t)( (dst_ip>>24)&0xFF ), (uint8_t)( (dst_ip>>16)&0xFF ), (uint8_t)( (dst_ip>>8)&0xFF ), (uint8_t)( dst_ip & 0xFF ) };
			uint16_t csum = shim_tcp_checksum(src_be, dst_be, tcp_bytes, tcp_hlen, NULL, 0);
			tcp->checksum = csum;
		}
		size_t total = sizeof(struct eth_hdr) + ip_len;
		if (total < 60) total = 60;
		e1000_send(frame, total);
	}

	/* Wait for SYN+ACK */
	uint64_t start = pit_get_time_ms();
	while (pit_get_time_ms() - start < timeout_ms) {
		uint8_t buf[2048]; size_t len = 0;
		int r = e1000_poll(buf, sizeof(buf), &len);
		if (r != 1) { pit_sleep_ms(5); continue; }
		if (len < 14 + 20 + 20) continue;
		/* crude parse */
		uint16_t ethertype = (uint16_t)((buf[12]<<8)|buf[13]);
		if (ethertype != 0x0800) continue;
		uint8_t proto = buf[23];
		if (proto != 6) continue;
		uint32_t sip = ((uint32_t)buf[26]<<24)|((uint32_t)buf[27]<<16)|((uint32_t)buf[28]<<8)|((uint32_t)buf[29]);
		uint32_t dip = ((uint32_t)buf[30]<<24)|((uint32_t)buf[31]<<16)|((uint32_t)buf[32]<<8)|((uint32_t)buf[33]);
		if (sip != dst_ip || dip != net_get_my_ip()) continue;
		uint16_t sp = (uint16_t)((buf[34]<<8)|buf[35]);
		uint16_t dp = (uint16_t)((buf[36]<<8)|buf[37]);
		if (sp != dst_port || dp != h->sport) continue;
		uint8_t flags = buf[47];
		uint32_t seq = ((uint32_t)buf[38]<<24)|((uint32_t)buf[39]<<16)|((uint32_t)buf[40]<<8)|((uint32_t)buf[41]);
		uint32_t ack = ((uint32_t)buf[42]<<24)|((uint32_t)buf[43]<<16)|((uint32_t)buf[44]<<8)|((uint32_t)buf[45]);
		if ((flags & 0x12) == 0x12 && ack == h->snd_nxt) {
			/* send ACK */
			/* build minimal ACK packet */
			uint8_t pframe[1514]; memset(pframe,0,sizeof(pframe));
			/* fill eth */
			memcpy(pframe, h->dst_mac, 6); memcpy(pframe+6, h->src_mac,6); pframe[12]=0x08; pframe[13]=0x00;
			/* set simple IPv4/TCP headers (not computing checksums) */
			pframe[14]=0x45; pframe[23]=6;
			/* tcp ports */
			pframe[34] = (uint8_t)(h->sport>>8); pframe[35]=(uint8_t)h->sport;
			pframe[36] = (uint8_t)(dst_port>>8); pframe[37]=(uint8_t)dst_port;
			/* seq/ack */
			uint32_t our_seq = h->snd_nxt;
			uint32_t their_seq = seq + 1;
			pframe[38]=(uint8_t)(our_seq>>24); pframe[39]=(uint8_t)(our_seq>>16); pframe[40]=(uint8_t)(our_seq>>8); pframe[41]=(uint8_t)our_seq;
			pframe[42]=(uint8_t)(their_seq>>24); pframe[43]=(uint8_t)(their_seq>>16); pframe[44]=(uint8_t)(their_seq>>8); pframe[45]=(uint8_t)their_seq;
			pframe[46]= (uint8_t)((20/4)<<4); pframe[47]=0x10; /* ACK */
			size_t tot = 14 + 20 + 20; if (tot < 60) tot = 60;
			e1000_send(pframe, tot);
			h->rcv_nxt = their_seq;
			h->connected = 1;
			h->established = 1;
			return h;
		}
	}
	/* timeout */
	kfree(h->rxbuf); kfree(h);
	return NULL;
}

/* Very small parser to detect "GET <path> " at start of buffer */
static const char* parse_get_path(const uint8_t* data, size_t len) {
	if (len < 5) return NULL;
	if (memcmp(data, "GET ", 4) != 0) return NULL;
	/* find space after path */
	const uint8_t* p = data + 4;
	const uint8_t* end = data + len;
	while (p < end && *p != ' ') p++;
	if (p == end) return NULL;
	/* allocate a nul-terminated copy */
	size_t plen = (size_t)(p - (data + 4));
	char *path = (char*)kmalloc(plen + 1);
	if (!path) return NULL;
	memcpy(path, data + 4, plen);
	path[plen] = '\0';
	return path;
}

int lwip_tcp_send(lwip_tcp_handle_t* h, const uint8_t* data, size_t len, uint32_t timeout_ms) {
	if (!h || !h->established) return -1;
	/* send raw data as PSH|ACK segments (no fragmentation beyond chunking) */
	size_t off = 0;
	while (off < len) {
		size_t chunk = (len - off) > 1400 ? 1400 : (len - off);
		/* build frame */
		uint8_t frame[1514];
		memset(frame, 0, sizeof(frame));
		/* eth */
		memcpy(frame, h->dst_mac, 6);
		memcpy(frame + 6, h->src_mac, 6);
		frame[12]=0x08; frame[13]=0x00;
		/* ipv4 minimal */
		frame[14]=0x45; frame[23]=6;
		/* tcp ports */
		frame[34]=(uint8_t)(h->sport>>8); frame[35]=(uint8_t)h->sport;
		frame[36]=(uint8_t)(h->dst_port>>8); frame[37]=(uint8_t)h->dst_port;
		/* seq/ack */
		uint32_t seq = h->snd_nxt;
		uint32_t ack = h->rcv_nxt;
		frame[38]=(uint8_t)(seq>>24); frame[39]=(uint8_t)(seq>>16); frame[40]=(uint8_t)(seq>>8); frame[41]=(uint8_t)seq;
		frame[42]=(uint8_t)(ack>>24); frame[43]=(uint8_t)(ack>>16); frame[44]=(uint8_t)(ack>>8); frame[45]=(uint8_t)ack;
		frame[46]= (uint8_t)(((20+chunk)/4)<<4);
		frame[47]= 0x18; /* PSH|ACK */
		/* copy payload after tcp header */
		size_t tcp_offset = 14 + 20;
		memcpy(frame + tcp_offset + 20, data + off, chunk);
		/* set lengths and checksums */
		/* IPv4 total len */
		{
			uint16_t iplen = (uint16_t)(20 + 20 + chunk);
			frame[16] = (uint8_t)(iplen >> 8); frame[17] = (uint8_t)(iplen & 0xFF);
			/* tcp checksum over pseudo-header */
			uint8_t src_be[4] = { (uint8_t)((net_get_my_ip()>>24)&0xFF),(uint8_t)((net_get_my_ip()>>16)&0xFF),(uint8_t)((net_get_my_ip()>>8)&0xFF),(uint8_t)(net_get_my_ip()&0xFF) };
			uint8_t dst_be[4] = { (uint8_t)((h->dst_ip>>24)&0xFF),(uint8_t)((h->dst_ip>>16)&0xFF),(uint8_t)((h->dst_ip>>8)&0xFF),(uint8_t)(h->dst_ip&0xFF) };
			uint8_t *tcpbytes = frame + tcp_offset;
			/* zero checksum field */
			tcpbytes[16] = tcpbytes[17] = 0;
			uint16_t csum = shim_tcp_checksum(src_be, dst_be, tcpbytes, 20, (uint8_t*)data + off, chunk);
			tcpbytes[16] = (uint8_t)(csum >> 8); tcpbytes[17] = (uint8_t)(csum & 0xFF);
			/* IPv4 header checksum */
			uint8_t *ipbytes = frame + 14;
			ipbytes[10] = ipbytes[11] = 0;
			uint16_t ih = shim_ip_checksum(ipbytes, 20);
			ipbytes[10] = (uint8_t)(ih >> 8); ipbytes[11] = (uint8_t)(ih & 0xFF);
		}
		size_t tot = tcp_offset + 20 + chunk;
		if (tot < 60) tot = 60;
		e1000_send(frame, tot);
		h->snd_nxt += (uint32_t)chunk;
		off += chunk;
		/* small pause to avoid overwhelming */
		pit_sleep_ms(1);
	}
	return (int)len;
}

int lwip_tcp_recv(lwip_tcp_handle_t* h, uint8_t* out, size_t len, uint32_t timeout_ms) {
	if (!h) return -1;
	uint64_t start = pit_get_time_ms();
	while (h->rxlen == 0 && !h->closed && pit_get_time_ms() - start < (uint64_t)timeout_ms) {
		/* poll NIC for matching TCP frames and ACK them */
		uint8_t buf[2048]; size_t flen = 0;
		int r = e1000_poll(buf, sizeof(buf), &flen);
		if (r != 1) { pit_sleep_ms(2); continue; }
		if (flen < 14 + 20 + 20) continue;
		uint16_t ethertype = (uint16_t)((buf[12]<<8)|buf[13]);
		if (ethertype != 0x0800) continue;
		uint8_t proto = buf[23];
		if (proto != 6) continue;
		uint32_t sip = ((uint32_t)buf[26]<<24)|((uint32_t)buf[27]<<16)|((uint32_t)buf[28]<<8)|((uint32_t)buf[29]);
		uint32_t dip = ((uint32_t)buf[30]<<24)|((uint32_t)buf[31]<<16)|((uint32_t)buf[32]<<8)|((uint32_t)buf[33]);
		if (sip != h->dst_ip || dip != net_get_my_ip()) continue;
		uint16_t sp = (uint16_t)((buf[34]<<8)|buf[35]);
		uint16_t dp = (uint16_t)((buf[36]<<8)|buf[37]);
		if (sp != h->dst_port || dp != h->sport) continue;
		uint8_t data_off = (buf[46] >> 4) & 0xF;
		size_t tcp_hlen = (size_t)data_off * 4;
		uint16_t tot_len = (uint16_t)((buf[16]<<8)|buf[17]);
		size_t payload_len = (size_t)tot_len - 20 - tcp_hlen;
		const uint8_t* payload = buf + 14 + 20 + tcp_hlen;
		uint32_t seq = ((uint32_t)buf[38]<<24)|((uint32_t)buf[39]<<16)|((uint32_t)buf[40]<<8)|((uint32_t)buf[41]);
		uint32_t ack = ((uint32_t)buf[42]<<24)|((uint32_t)buf[43]<<16)|((uint32_t)buf[44]<<8)|((uint32_t)buf[45]);
		uint8_t flags = buf[47];
		/* if payload, append to rxbuf and send ACK */
		if (payload_len > 0) {
			if (h->rxlen + payload_len > h->rxcap) {
				size_t newcap = h->rxcap * 2;
				while (h->rxlen + payload_len > newcap) newcap *= 2;
				uint8_t* nb = (uint8_t*)krealloc(h->rxbuf, newcap);
				if (!nb) { h->closed = 1; return -1; }
				h->rxbuf = nb; h->rxcap = newcap;
			}
			memcpy(h->rxbuf + h->rxlen, payload, payload_len);
			h->rxlen += payload_len;
			h->rcv_nxt = seq + (uint32_t)payload_len;
			/* send ACK (with checksums) */
			{
				uint8_t ackf[1514]; memset(ackf,0,sizeof(ackf));
				memcpy(ackf, buf + 6, 6); memcpy(ackf+6, buf,6); ackf[12]=0x08; ackf[13]=0x00;
				/* IPv4 header */
				uint8_t *ipbytes = ackf + 14;
				ipbytes[0]=0x45; ipbytes[8]=0; ipbytes[9]=6;
				/* ports and seq/ack */
				ackf[34]=(uint8_t)(h->sport>>8); ackf[35]=(uint8_t)h->sport;
				ackf[36]=(uint8_t)(h->dst_port>>8); ackf[37]=(uint8_t)h->dst_port;
				uint32_t our_seq = h->snd_nxt;
				uint32_t their_seq = h->rcv_nxt;
				ackf[38]=(uint8_t)(our_seq>>24); ackf[39]=(uint8_t)(our_seq>>16); ackf[40]=(uint8_t)(our_seq>>8); ackf[41]=(uint8_t)our_seq;
				ackf[42]=(uint8_t)(their_seq>>24); ackf[43]=(uint8_t)(their_seq>>16); ackf[44]=(uint8_t)(their_seq>>8); ackf[45]=(uint8_t)their_seq;
				ackf[46]= (uint8_t)((20/4)<<4); ackf[47]=0x10;
				/* compute tcp checksum */
				uint8_t src_be[4] = { (uint8_t)((net_get_my_ip()>>24)&0xFF),(uint8_t)((net_get_my_ip()>>16)&0xFF),(uint8_t)((net_get_my_ip()>>8)&0xFF),(uint8_t)(net_get_my_ip()&0xFF) };
				uint8_t dst_be[4] = { (uint8_t)((h->dst_ip>>24)&0xFF),(uint8_t)((h->dst_ip>>16)&0xFF),(uint8_t)((h->dst_ip>>8)&0xFF),(uint8_t)(h->dst_ip&0xFF) };
				uint8_t *tcpbytes = ackf + 14 + 20;
				tcpbytes[16]=tcpbytes[17]=0;
				uint16_t csum = shim_tcp_checksum(src_be, dst_be, tcpbytes, 20, NULL, 0);
				tcpbytes[16] = (uint8_t)(csum>>8); tcpbytes[17] = (uint8_t)(csum & 0xFF);
				/* ip total len and checksum */
				uint16_t iplen = (uint16_t)(20 + 20);
				ipbytes[2] = (uint8_t)(iplen >> 8); ipbytes[3] = (uint8_t)(iplen & 0xFF);
				ipbytes[10]=ipbytes[11]=0;
				uint16_t ih = shim_ip_checksum(ipbytes, 20);
				ipbytes[10] = (uint8_t)(ih >> 8); ipbytes[11] = (uint8_t)(ih & 0xFF);
				size_t atot = 14 + 20 + 20; if (atot < 60) atot = 60;
				e1000_send(ackf, atot);
			}
		}
		if (flags & 0x01) { h->closed = 1; return 0; }
	}
	/* deliver up to len */
	if (h->rxlen == 0) {
		return -1;
	}
	size_t to_copy = h->rxlen < len ? h->rxlen : len;
	memcpy(out, h->rxbuf, to_copy);
	memmove(h->rxbuf, h->rxbuf + to_copy, h->rxlen - to_copy);
	h->rxlen -= to_copy;
	return (int)to_copy;
}

void lwip_tcp_close(lwip_tcp_handle_t* h) {
	if (!h) return;
	if (h->rxbuf) kfree(h->rxbuf);
	kfree(h);
}

void lwip_pump_io(void) { /* no-op */ }

int lwip_tcp_consume(lwip_tcp_handle_t* h, uint8_t* out, size_t want, uint32_t timeout_ms) {
	/* Simple wrapper around recv that waits until some data or timeout */
	uint64_t start = pit_get_time_ms();
	size_t got = 0;
	(void)timeout_ms;
	while (got < want && pit_get_time_ms() - start < (uint64_t)(timeout_ms ? timeout_ms : 5000)) {
		int r = lwip_tcp_recv(h, out + got, want - got, 0);
		if (r > 0) { got += (size_t)r; continue; }
		if (r == 0) break;
		pit_sleep_ms(1);
	}
	return got ? (int)got : -1;
}

size_t lwip_tcp_pending(const lwip_tcp_handle_t* h) { return h ? h->rxlen : 0; }
int lwip_tcp_is_closed(const lwip_tcp_handle_t* h) { return h ? h->closed : 1; }
int lwip_tcp_errflag(const lwip_tcp_handle_t* h) { (void)h; return 0; }

/* sys_check_timeouts used by TLS glue; provide no-op */
void sys_check_timeouts(void) { }


