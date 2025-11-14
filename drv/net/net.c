#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <debug.h>
#include <pit.h>
#include <e1000.h>
#include <net.h>

/* Minimal Ethernet + ARP + IPv4 + ICMP for ping demonstration */

#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPv4   0x0800

#define ARP_HTYPE_ETH   0x0001
#define ARP_PTYPE_IP    0x0800
#define ARP_HLEN        6
#define ARP_PLEN        4
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#define IP_PROTO_ICMP   1
#define IP_PROTO_UDP    17

struct __attribute__((packed)) eth_hdr {
	uint8_t  dst[6];
	uint8_t  src[6];
	uint16_t type;
};

struct __attribute__((packed)) arp_pkt {
	uint16_t htype;
	uint16_t ptype;
	uint8_t  hlen;
	uint8_t  plen;
	uint16_t oper;
	uint8_t  sha[6];
	uint32_t spa;
	uint8_t  tha[6];
	uint32_t tpa;
};

struct __attribute__((packed)) ipv4_hdr {
	uint8_t  ver_ihl;
	uint8_t  tos;
	uint16_t tot_len;
	uint16_t id;
	uint16_t flags_frag;
	uint8_t  ttl;
	uint8_t  proto;
	uint16_t hdr_checksum;
	uint32_t saddr;
	uint32_t daddr;
};

struct __attribute__((packed)) icmp_echo {
	uint8_t  type;
	uint8_t  code;
	uint16_t checksum;
	uint16_t ident;
	uint16_t seq;
	/* payload follows */
};

static uint8_t   g_mac[6] = {0};
static uint32_t  g_ip = 0;          /* host order */
static uint32_t  g_gw_ip = 0;       /* host order */

/* Very small ARP cache for one peer */
static uint32_t cached_ip = 0;
static uint8_t  cached_mac[6] = {0};
static int      cached_valid = 0;

/* Last echo tracking */
static uint16_t last_ident = 0x1234;
static uint16_t last_seq = 0;
static volatile int last_reply_ok = 0;

static uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static uint32_t bswap32(uint32_t v) {
	return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

static uint16_t ip_checksum(const void* data, size_t len) {
	const uint16_t* w = (const uint16_t*)data;
	uint32_t sum = 0;
	while (len > 1) { sum += *w++; len -= 2; }
	if (len) sum += *(const uint8_t*)w;
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	return (uint16_t)(~sum);
}

/* forward decls */
static void make_eth(uint8_t* frame, const uint8_t dst[6], uint16_t eth_type);

struct __attribute__((packed)) udp_hdr {
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t len;
	uint16_t checksum;
};

static uint16_t udp_checksum(const struct ipv4_hdr* ip, const struct udp_hdr* udp, const uint8_t* payload, size_t pay_len) {
	uint32_t sum = 0;
	/* Pseudo header */
	const uint16_t* s = (const uint16_t*)&ip->saddr;
	sum += s[0] + s[1];
	const uint16_t* d = (const uint16_t*)&ip->daddr;
	sum += d[0] + d[1];
	sum += bswap16((uint16_t)IP_PROTO_UDP);
	sum += udp->len;
	/* UDP header */
	const uint16_t* w = (const uint16_t*)udp;
	for (size_t i = 0; i < sizeof(struct udp_hdr)/2; i++) sum += w[i];
	/* Payload */
	w = (const uint16_t*)payload;
	size_t l = pay_len;
	while (l > 1) { sum += *w++; l -= 2; }
	if (l) sum += ((uint16_t)(*(const uint8_t*)w)) << 8;
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	uint16_t res = (uint16_t)(~sum);
	return res ? res : 0xFFFF; /* RFC: 0x0000 -> transmitted as 0xFFFF */
}

static void send_udp(const uint8_t dst_mac[6], uint32_t dst_ip, uint16_t sport, uint16_t dport, const uint8_t* payload, size_t pay_len) {
	uint8_t frame[1514];
	make_eth(frame, dst_mac, ETH_TYPE_IPv4);
	struct ipv4_hdr* ip = (struct ipv4_hdr*)(frame + sizeof(struct eth_hdr));
	struct udp_hdr*  udp = (struct udp_hdr*)((uint8_t*)ip + sizeof(struct ipv4_hdr));
	size_t ip_len = sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr) + pay_len;

	ip->ver_ihl = 0x45;
	ip->tos = 0;
	ip->tot_len = bswap16((uint16_t)ip_len);
	ip->id = bswap16(0x2222);
	ip->flags_frag = bswap16(0x4000);
	ip->ttl = 64;
	ip->proto = IP_PROTO_UDP;
	ip->hdr_checksum = 0;
	ip->saddr = bswap32(g_ip);
	ip->daddr = bswap32(dst_ip);
	ip->hdr_checksum = ip_checksum(ip, sizeof(struct ipv4_hdr));

	udp->src_port = bswap16(sport);
	udp->dst_port = bswap16(dport);
	udp->len = bswap16((uint16_t)(sizeof(struct udp_hdr) + pay_len));
	/* For IPv4, UDP checksum 0 means 'no checksum' and is widely accepted.
	 * This avoids potential endianness/calculation issues on minimal stack. */
	udp->checksum = 0;
	if (pay_len) memcpy((uint8_t*)udp + sizeof(struct udp_hdr), payload, pay_len);
	/* If needed in future, compute checksum via udp_checksum(...) */

	size_t total = sizeof(struct eth_hdr) + ip_len;
	if (total < 60) total = 60;
	e1000_send(frame, total);
}

static void make_eth(uint8_t* frame, const uint8_t dst[6], uint16_t eth_type) {
	struct eth_hdr* eh = (struct eth_hdr*)frame;
	memcpy(eh->dst, dst, 6);
	memcpy(eh->src, g_mac, 6);
	eh->type = bswap16(eth_type);
}

static void send_arp_request(uint32_t target_ip) {
	uint8_t frame[64];
	memset(frame, 0, sizeof(frame));
	uint8_t bcast[6]; memset(bcast, 0xFF, 6);
	make_eth(frame, bcast, ETH_TYPE_ARP);
	struct arp_pkt* a = (struct arp_pkt*)(frame + sizeof(struct eth_hdr));
	a->htype = bswap16(ARP_HTYPE_ETH);
	a->ptype = bswap16(ARP_PTYPE_IP);
	a->hlen = ARP_HLEN;
	a->plen = ARP_PLEN;
	a->oper = bswap16(ARP_OP_REQUEST);
	memcpy(a->sha, g_mac, 6);
	a->spa = bswap32(g_ip);
	memset(a->tha, 0x00, 6);
	a->tpa = bswap32(target_ip);
	size_t len = sizeof(struct eth_hdr) + sizeof(struct arp_pkt);
	if (len < 60) len = 60;
	e1000_send(frame, len);
	qemu_debug_printf("net: ARP who-has %d.%d.%d.%d?\n",
		(target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF, (target_ip >> 8) & 0xFF, target_ip & 0xFF);
}

static void send_icmp_echo_request(const uint8_t dst_mac[6], uint32_t dst_ip, uint16_t ident, uint16_t seq) {
	uint8_t frame[1514];
	uint8_t payload[32];
	for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)i;

	make_eth(frame, dst_mac, ETH_TYPE_IPv4);

	struct ipv4_hdr* ip = (struct ipv4_hdr*)(frame + sizeof(struct eth_hdr));
	struct icmp_echo* icmp = (struct icmp_echo*)((uint8_t*)ip + sizeof(struct ipv4_hdr));
	size_t icmp_len = sizeof(struct icmp_echo) + sizeof(payload);
	size_t ip_len = sizeof(struct ipv4_hdr) + icmp_len;

	ip->ver_ihl = 0x45; /* v4, IHL=5 */
	ip->tos = 0;
	ip->tot_len = bswap16((uint16_t)ip_len);
	ip->id = bswap16(0xBEEF);
	ip->flags_frag = bswap16(0x4000); /* DF */
	ip->ttl = 64;
	ip->proto = IP_PROTO_ICMP;
	ip->hdr_checksum = 0;
	ip->saddr = bswap32(g_ip);
	ip->daddr = bswap32(dst_ip);
	ip->hdr_checksum = ip_checksum(ip, sizeof(struct ipv4_hdr));

	icmp->type = 8; /* Echo Request */
	icmp->code = 0;
	icmp->checksum = 0;
	icmp->ident = bswap16(ident);
	icmp->seq = bswap16(seq);
	memcpy((uint8_t*)icmp + sizeof(struct icmp_echo), payload, sizeof(payload));
	icmp->checksum = ip_checksum(icmp, icmp_len);

	size_t total = sizeof(struct eth_hdr) + ip_len;
	if (total < 60) total = 60;
	e1000_send(frame, total);
}

static void handle_arp(const uint8_t* frame, size_t len) {
	if (len < sizeof(struct eth_hdr) + sizeof(struct arp_pkt)) return;
	const struct arp_pkt* a = (const struct arp_pkt*)(frame + sizeof(struct eth_hdr));
	if (bswap16(a->htype) != ARP_HTYPE_ETH || bswap16(a->ptype) != ARP_PTYPE_IP) return;
	if (a->hlen != 6 || a->plen != 4) return;
	uint16_t op = bswap16(a->oper);
	if (op == ARP_OP_REPLY) {
		uint32_t spa = bswap32(a->spa);
		memcpy(cached_mac, a->sha, 6);
		cached_ip = spa;
		cached_valid = 1;
		qemu_debug_printf("net: ARP reply %d.%d.%d.%d is %02x:%02x:%02x:%02x:%02x:%02x\n",
			(spa >> 24)&0xFF,(spa>>16)&0xFF,(spa>>8)&0xFF,spa&0xFF,
			cached_mac[0],cached_mac[1],cached_mac[2],cached_mac[3],cached_mac[4],cached_mac[5]);
	} else if (op == ARP_OP_REQUEST) {
		/* Learn sender MAC and reply if asked for our IP */
		uint32_t spa = bswap32(a->spa);
		uint32_t tpa = bswap32(a->tpa);
		/* Cache host MAC from request */
		memcpy(cached_mac, a->sha, 6);
		cached_ip = spa;
		cached_valid = 1;
		/* If the request is "who-has our IP", send ARP reply */
		if (tpa == g_ip) {
			uint8_t reply[64]; memset(reply, 0, sizeof(reply));
			/* Ethernet */
			make_eth(reply, a->sha, ETH_TYPE_ARP);
			struct arp_pkt* r = (struct arp_pkt*)(reply + sizeof(struct eth_hdr));
			r->htype = bswap16(ARP_HTYPE_ETH);
			r->ptype = bswap16(ARP_PTYPE_IP);
			r->hlen = ARP_HLEN;
			r->plen = ARP_PLEN;
			r->oper = bswap16(ARP_OP_REPLY);
			memcpy(r->sha, g_mac, 6);
			r->spa = bswap32(g_ip);
			memcpy(r->tha, a->sha, 6);
			r->tpa = a->spa; /* already in network order in original pkt */
			size_t rlen = sizeof(struct eth_hdr) + sizeof(struct arp_pkt);
			if (rlen < 60) rlen = 60;
			e1000_send(reply, rlen);
			qemu_debug_printf("net: ARP reply sent to %d.%d.%d.%d\n",
				(spa>>24)&0xFF,(spa>>16)&0xFF,(spa>>8)&0xFF,spa&0xFF);
		}
	}
}

static void handle_ipv4(const uint8_t* frame, size_t len) {
	if (len < sizeof(struct eth_hdr) + sizeof(struct ipv4_hdr)) return;
	const struct ipv4_hdr* ip = (const struct ipv4_hdr*)(frame + sizeof(struct eth_hdr));
	const struct eth_hdr* eh_req = (const struct eth_hdr*)frame;
	uint8_t ihl = ip->ver_ihl & 0x0F;
	if (ihl < 5) return;
	size_t ip_hlen = (size_t)ihl * 4;
	if (len < sizeof(struct eth_hdr) + ip_hlen) return;
	uint16_t tot_len = bswap16(ip->tot_len);
	if (len < sizeof(struct eth_hdr) + tot_len) return;
	if (ip->proto == IP_PROTO_UDP) {
		/* DNS handling is done in synchronous query function; nothing here */
		return;
	}
	if (ip->proto != IP_PROTO_ICMP) return;
	const struct icmp_echo* icmp = (const struct icmp_echo*)((const uint8_t*)ip + ip_hlen);
	size_t icmp_len = (size_t)tot_len - ip_hlen;
	if (icmp_len < sizeof(struct icmp_echo)) return;
	if (icmp->type == 0 && icmp->code == 0) {
		/* Echo Reply */
		uint16_t id = bswap16(icmp->ident);
		uint16_t sq = bswap16(icmp->seq);
		if (id == last_ident && sq == last_seq) {
			last_reply_ok = 1;
			qemu_debug_printf("net: ICMP echo reply seq=%u\n", sq);
		}
	} else if (icmp->type == 8 && icmp->code == 0) {
		/* Echo Request to us -> send Echo Reply */
		if (ip->daddr != bswap32(g_ip)) return;
		const uint8_t* req_payload = (const uint8_t*)icmp + sizeof(struct icmp_echo);
		size_t req_pay_len = icmp_len - sizeof(struct icmp_echo);

		uint8_t reply[1514];
		/* Ethernet */
		make_eth(reply, eh_req->src, ETH_TYPE_IPv4);
		struct ipv4_hdr* rip = (struct ipv4_hdr*)(reply + sizeof(struct eth_hdr));
		struct icmp_echo* ricmp = (struct icmp_echo*)((uint8_t*)rip + sizeof(struct ipv4_hdr));

		size_t ricmp_len = sizeof(struct icmp_echo) + req_pay_len;
		size_t rip_len = sizeof(struct ipv4_hdr) + ricmp_len;
		rip->ver_ihl = 0x45;
		rip->tos = 0;
		rip->tot_len = bswap16((uint16_t)rip_len);
		rip->id = bswap16(0xCAFE);
		rip->flags_frag = bswap16(0x4000);
		rip->ttl = 64;
		rip->proto = IP_PROTO_ICMP;
		rip->hdr_checksum = 0;
		rip->saddr = bswap32(g_ip);
		rip->daddr = ip->saddr;
		rip->hdr_checksum = ip_checksum(rip, sizeof(struct ipv4_hdr));

		ricmp->type = 0; /* Echo Reply */
		ricmp->code = 0;
		ricmp->checksum = 0;
		ricmp->ident = icmp->ident;
		ricmp->seq = icmp->seq;
		if (req_pay_len) memcpy((uint8_t*)ricmp + sizeof(struct icmp_echo), req_payload, req_pay_len);
		ricmp->checksum = ip_checksum(ricmp, ricmp_len);

		size_t total = sizeof(struct eth_hdr) + rip_len;
		if (total < 60) total = 60;
		e1000_send(reply, total);
		qemu_debug_printf("net: ICMP echo request -> reply\n");
	}
}

/* DNS (UDP)
 * NOTE: when USE_LWIP is enabled, net_dns_query is implemented in lwip_glue.c
 * to avoid competing with lwIP's input path. The raw version below is used
 * only when lwIP is disabled.
 */
#ifndef USE_LWIP
struct __attribute__((packed)) dns_header {
	uint16_t id;
	uint16_t flags;
	uint16_t qdcount;
	uint16_t ancount;
	uint16_t nscount;
	uint16_t arcount;
};

static int build_dns_query(const char* host, uint8_t* out, size_t* out_len, uint16_t txid) {
	uint8_t* p = out;
	struct dns_header* h = (struct dns_header*)p;
	h->id = bswap16(txid);
	h->flags = bswap16(0x0100); /* RD=1 */
	h->qdcount = bswap16(1);
	h->ancount = 0;
	h->nscount = 0;
	h->arcount = 0;
	p += sizeof(struct dns_header);
	/* QNAME */
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
	*p++ = 0; /* end */
	/* QTYPE=A, QCLASS=IN */
	*p++ = 0; *p++ = 1;
	*p++ = 0; *p++ = 1;
	*out_len = (size_t)(p - out);
	return 0;
}

int net_dns_query(const char* host, uint32_t dns_ip, uint32_t* out_ip, uint32_t timeout_ms) {
	if (!host || !out_ip) return -1;
	/* Resolve ARP for the gateway; send DNS to dns_ip via L2 gateway MAC */
	uint32_t arp_target = g_gw_ip ? g_gw_ip : dns_ip;
	if (!(cached_valid && cached_ip == arp_target)) {
		send_arp_request(arp_target);
		uint64_t start = pit_get_time_ms();
		uint64_t last_req = start;
		while (pit_get_time_ms() - start < timeout_ms) {
			net_poll();
			if (cached_valid && cached_ip == arp_target) break;
			if (pit_get_time_ms() - last_req >= 250) {
				send_arp_request(arp_target);
				last_req = pit_get_time_ms();
			}
			pit_sleep_ms(10);
		}
		if (!(cached_valid && cached_ip == arp_target)) return -2;
	}
	uint8_t* dmac = cached_mac;
	uint8_t q[512]; size_t qlen = 0;
	static uint16_t txid = 0x5353;
	txid++;
	if (build_dns_query(host, q, &qlen, txid) != 0) return -3;

	uint16_t sport = (uint16_t)(40000 + (txid & 0xFF));
	send_udp(dmac, dns_ip, sport, 53, q, qlen);

	uint64_t deadline = pit_get_time_ms() + timeout_ms;
	uint8_t buf[2048]; size_t len = 0;
	while (pit_get_time_ms() < deadline) {
		int r = e1000_poll(buf, sizeof(buf), &len);
		if (r != 1) { pit_sleep_ms(5); continue; }
		if (len < sizeof(struct eth_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr) + sizeof(struct dns_header)) continue;
		const struct eth_hdr* eh = (const struct eth_hdr*)buf;
		if (bswap16(eh->type) != ETH_TYPE_IPv4) continue;
		const struct ipv4_hdr* ip = (const struct ipv4_hdr*)(buf + sizeof(struct eth_hdr));
		if (ip->proto != IP_PROTO_UDP) continue;
		/* Accept DNS reply from any server (NAT may forward from different IP).
		 * Ensure it's addressed to us and from UDP/53 to our ephemeral port. */
		if (ip->daddr != bswap32(g_ip)) continue;
		const struct udp_hdr* udp = (const struct udp_hdr*)((const uint8_t*)ip + sizeof(struct ipv4_hdr));
		if (udp->dst_port != bswap16(sport)) continue;
		if (udp->src_port != bswap16(53)) continue;
		const uint8_t* dns = (const uint8_t*)udp + sizeof(struct udp_hdr);
		const struct dns_header* dh = (const struct dns_header*)dns;
		if (dh->id != bswap16(txid)) continue;
		/* Parse DNS answer: skip header and question, then read first A RR */
		const uint8_t* p = dns + sizeof(struct dns_header);
		/* skip QNAME */
		while (*p && p < buf + len) p += (*p) + 1;
		if (p >= buf + len) continue;
		p++; /* zero label */
		if (p + 4 > buf + len) continue; /* QTYPE/QCLASS */
		p += 4;
		/* Answers */
		uint16_t an = bswap16(dh->ancount);
		for (uint16_t i = 0; i < an; i++) {
			if (p + 12 > buf + len) break;
			/* NAME: could be pointer (0xC0) or labels; skip it generically */
			if ((*p & 0xC0) == 0xC0) {
				p += 2;
			} else {
				while (*p && p < buf + len) p += (*p) + 1;
				if (p >= buf + len) break;
				p++;
			}
			if (p + 10 > buf + len) break;
			uint16_t type = (p[0] << 8) | p[1];
			/* class=p[2:4], ttl=p[4:8] */
			uint16_t rdlen = (p[8] << 8) | p[9];
			p += 10;
			if (p + rdlen > buf + len) break;
			if (type == 1 && rdlen == 4) {
				/* A record */
				uint32_t nip = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
				*out_ip = nip;
				return 0;
			}
			p += rdlen;
		}
	}
	return -4;
}
#endif
/* Exported helpers */
int net_arp_resolve(uint32_t target_ip, uint8_t out_mac[6], uint32_t timeout_ms) {
	if (cached_valid && cached_ip == target_ip) {
		if (out_mac) memcpy(out_mac, cached_mac, 6);
		return 0;
	}
	send_arp_request(target_ip);
	uint64_t start = pit_get_time_ms();
	uint64_t last_req = start;
	while (pit_get_time_ms() - start < timeout_ms) {
		net_poll();
		if (cached_valid && cached_ip == target_ip) {
			if (out_mac) memcpy(out_mac, cached_mac, 6);
			return 0;
		}
		if (pit_get_time_ms() - last_req >= 250) {
			send_arp_request(target_ip);
			last_req = pit_get_time_ms();
		}
		pit_sleep_ms(5);
	}
	return -1;
}

uint32_t net_get_my_ip(void) { return g_ip; }
uint32_t net_get_gateway_ip(void) { return g_gw_ip; }
void net_init(uint32_t my_ip, uint32_t gateway_ip) {
	g_ip = my_ip;
	g_gw_ip = gateway_ip;
	e1000_get_mac(g_mac);
	qemu_debug_printf("net: my IP %d.%d.%d.%d, gw %d.%d.%d.%d\n",
		(my_ip>>24)&0xFF,(my_ip>>16)&0xFF,(my_ip>>8)&0xFF,my_ip&0xFF,
		(gateway_ip>>24)&0xFF,(gateway_ip>>16)&0xFF,(gateway_ip>>8)&0xFF,gateway_ip&0xFF);
}

void net_poll(void) {
	uint8_t buf[2048];
	size_t len = 0;
	for (int i = 0; i < 64; i++) {
		int r = e1000_poll(buf, sizeof(buf), &len);
		if (r != 1) break;
		if (len < sizeof(struct eth_hdr)) continue;
		const struct eth_hdr* eh = (const struct eth_hdr*)buf;
		uint16_t et = bswap16(eh->type);
		if (et == ETH_TYPE_ARP) handle_arp(buf, len);
		else if (et == ETH_TYPE_IPv4) handle_ipv4(buf, len);
	}
}

int net_ping(uint32_t target_ip, uint32_t timeout_ms) {
	/* Resolve ARP (cache miss) */
	if (!(cached_valid && cached_ip == target_ip)) {
		send_arp_request(target_ip);
		uint64_t start = pit_get_time_ms();
		uint64_t last_req = start;
		while (pit_get_time_ms() - start < timeout_ms) {
			net_poll();
			if (cached_valid && cached_ip == target_ip) break;
			/* re-send ARP request every 250ms until resolved */
			if (pit_get_time_ms() - last_req >= 250) {
				send_arp_request(target_ip);
				last_req = pit_get_time_ms();
			}
			pit_sleep_ms(10);
		}
		if (!(cached_valid && cached_ip == target_ip)) {
			qemu_debug_printf("net: ARP resolve timeout\n");
			return -1;
		}
	}

	/* Send echo request */
	last_seq++;
	last_reply_ok = 0;
	send_icmp_echo_request(cached_mac, target_ip, last_ident, last_seq);
	qemu_debug_printf("net: ICMP echo request sent\n");

	uint64_t start = pit_get_time_ms();
	while (pit_get_time_ms() - start < timeout_ms) {
		net_poll();
		if (last_reply_ok) return 0;
		pit_sleep_ms(10);
	}
	return -2; /* timeout */
}

/* ------------------- Minimal HTTP over raw TCP (demo) ------------------- */
struct __attribute__((packed)) tcp_hdr {
	uint16_t src_port;
	uint16_t dst_port;
	uint32_t seq;
	uint32_t ack;
	uint8_t  data_off_rsv;
	uint8_t  flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgptr;
};

static uint16_t tcp_checksum(const struct ipv4_hdr* ip, const struct tcp_hdr* tcp, const uint8_t* payload, size_t pay_len, size_t tcp_hdr_len) {
	uint32_t sum = 0;
	/* Pseudo header */
	const uint16_t* s = (const uint16_t*)&ip->saddr;
	sum += s[0] + s[1];
	const uint16_t* d = (const uint16_t*)&ip->daddr;
	sum += d[0] + d[1];
	sum += bswap16((uint16_t)6); /* protocol TCP */
	uint16_t tlen = (uint16_t)(tcp_hdr_len + pay_len);
	sum += bswap16(tlen);
	/* TCP header */
	const uint16_t* w = (const uint16_t*)tcp;
	for (size_t i = 0; i < tcp_hdr_len/2; i++) sum += w[i];
	/* Payload */
	w = (const uint16_t*)payload;
	size_t l = pay_len;
	while (l > 1) { sum += *w++; l -= 2; }
	if (l) sum += ((uint16_t)(*(const uint8_t*)w)) << 8;
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	return (uint16_t)(~sum);
}

static void send_tcp_segment(const uint8_t dst_mac[6], uint32_t dst_ip, uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack, uint8_t flags, const uint8_t* payload, size_t pay_len, uint16_t win) {
	uint8_t frame[1514];
	make_eth(frame, dst_mac, ETH_TYPE_IPv4);
	struct ipv4_hdr* ip = (struct ipv4_hdr*)(frame + sizeof(struct eth_hdr));
	struct tcp_hdr*  tcp = (struct tcp_hdr*)((uint8_t*)ip + sizeof(struct ipv4_hdr));
	size_t tcp_hlen = sizeof(struct tcp_hdr);
	size_t ip_len = sizeof(struct ipv4_hdr) + tcp_hlen + pay_len;

	ip->ver_ihl = 0x45;
	ip->tos = 0;
	ip->tot_len = bswap16((uint16_t)ip_len);
	ip->id = bswap16(0x4444);
	ip->flags_frag = bswap16(0x4000);
	ip->ttl = 64;
	ip->proto = 6; /* TCP */
	ip->hdr_checksum = 0;
	ip->saddr = bswap32(g_ip);
	ip->daddr = bswap32(dst_ip);
	ip->hdr_checksum = ip_checksum(ip, sizeof(struct ipv4_hdr));

	tcp->src_port = bswap16(sport);
	tcp->dst_port = bswap16(dport);
	tcp->seq = bswap32(seq);
	tcp->ack = bswap32(ack);
	tcp->data_off_rsv = (uint8_t)((sizeof(struct tcp_hdr) / 4) << 4);
	tcp->flags = flags;
	tcp->window = bswap16(win);
	tcp->checksum = 0;
	tcp->urgptr = 0;
	if (pay_len) memcpy((uint8_t*)tcp + sizeof(struct tcp_hdr), payload, pay_len);
	tcp->checksum = tcp_checksum(ip, tcp, pay_len ? ((const uint8_t*)tcp + sizeof(struct tcp_hdr)) : (const uint8_t*)"", pay_len, sizeof(struct tcp_hdr));

	size_t total = sizeof(struct eth_hdr) + ip_len;
	if (total < 60) total = 60;
	e1000_send(frame, total);
}

static int append_dec(char* buf, int cap, int pos, uint8_t v) {
	char tmp[3]; int n = 0;
	if (v >= 100) { tmp[n++] = '0' + (v / 100); v %= 100; } 
	if (v >= 10 || n) { tmp[n++] = '0' + (v / 10); v %= 10; }
	tmp[n++] = '0' + v;
	if (pos + n >= cap) return pos;
	memcpy(buf + pos, tmp, n);
	return pos + n;
}

int net_http_get(uint32_t dst_ip, uint16_t dst_port, const char* path,
                 uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms) {
	/* Resolve ARP */
	if (!(cached_valid && cached_ip == dst_ip)) {
		send_arp_request(dst_ip);
		uint64_t s = pit_get_time_ms();
		while (pit_get_time_ms() - s < timeout_ms) {
			net_poll();
			if (cached_valid && cached_ip == dst_ip) break;
			pit_sleep_ms(10);
		}
		if (!(cached_valid && cached_ip == dst_ip)) return -1;
	}
	const uint8_t* dmac = cached_mac;
	if (!path) path = "/";
	uint16_t sport = 40000;
	uint32_t iss = 0x11112222;
	uint32_t snd_nxt = iss;
	uint32_t rcv_nxt = 0;

	/* Build request */
	char req[256]; int p = 0;
	const char* m1 = "GET ";
	memcpy(req + p, m1, 4); p += 4;
	size_t pl = strlen(path); if (p + (int)pl >= (int)sizeof(req)) pl = sizeof(req) - p - 1;
	memcpy(req + p, path, pl); p += (int)pl;
	const char* m2 = " HTTP/1.1\r\nHost: ";
	memcpy(req + p, m2, 17); p += 17;
	p = append_dec(req, (int)sizeof(req), p, (uint8_t)((dst_ip >> 24) & 0xFF)); if (p < (int)sizeof(req)) req[p++]='.';
	p = append_dec(req, (int)sizeof(req), p, (uint8_t)((dst_ip >> 16) & 0xFF)); if (p < (int)sizeof(req)) req[p++]='.';
	p = append_dec(req, (int)sizeof(req), p, (uint8_t)((dst_ip >> 8) & 0xFF));  if (p < (int)sizeof(req)) req[p++]='.';
	p = append_dec(req, (int)sizeof(req), p, (uint8_t)(dst_ip & 0xFF));
	const char* m3 = "\r\nUser-Agent: AxonOS\r\nConnection: close\r\n\r\n";
	size_t m3l = strlen(m3); if (p + (int)m3l >= (int)sizeof(req)) m3l = sizeof(req) - p;
	memcpy(req + p, m3, m3l); p += (int)m3l;

	/* SYN */
	send_tcp_segment(dmac, dst_ip, sport, dst_port, snd_nxt, 0, 0x02 /* SYN */, 0, 0, 0x4000);
	snd_nxt++;

	uint64_t deadline = pit_get_time_ms() + timeout_ms;
	uint8_t buf[2048]; size_t len = 0;
	int state = 1; /* 1 SYN-SENT, 2 ESTABLISHED */
	size_t written = 0;

	while (pit_get_time_ms() < deadline) {
		int r = e1000_poll(buf, sizeof(buf), &len);
		if (r != 1) { pit_sleep_ms(5); continue; }
		if (len < sizeof(struct eth_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct tcp_hdr)) continue;
		const struct eth_hdr* eh = (const struct eth_hdr*)buf;
		if (bswap16(eh->type) != ETH_TYPE_IPv4) continue;
		const struct ipv4_hdr* ip = (const struct ipv4_hdr*)(buf + sizeof(struct eth_hdr));
		if (ip->proto != 6) continue;
		if (ip->saddr != bswap32(dst_ip) || ip->daddr != bswap32(g_ip)) continue;
		const struct tcp_hdr* tcp = (const struct tcp_hdr*)((const uint8_t*)ip + sizeof(struct ipv4_hdr));
		if (tcp->src_port != bswap16(dst_port) || tcp->dst_port != bswap16(sport)) continue;
		uint8_t flags = tcp->flags;
		uint32_t seq = bswap32(tcp->seq);
		uint32_t ack = bswap32(tcp->ack);
		(void)ack;
		uint8_t data_off = (tcp->data_off_rsv >> 4) & 0xF;
		size_t tcp_hlen = (size_t)data_off * 4;
		uint16_t tot_len = bswap16(ip->tot_len);
		if (tot_len < sizeof(struct ipv4_hdr) + tcp_hlen) continue;
		size_t payload_len = (size_t)tot_len - sizeof(struct ipv4_hdr) - tcp_hlen;
		const uint8_t* payload = (const uint8_t*)tcp + tcp_hlen;

		if (state == 1 && (flags & 0x12) == 0x12) { /* SYN+ACK */
			rcv_nxt = seq + 1;
			send_tcp_segment(dmac, dst_ip, sport, dst_port, snd_nxt, rcv_nxt, 0x10 /* ACK */, 0, 0, 0x4000);
			send_tcp_segment(dmac, dst_ip, sport, dst_port, snd_nxt, rcv_nxt, 0x18 /* PSH|ACK */, (const uint8_t*)req, (size_t)p, 0x4000);
			snd_nxt += (uint32_t)p;
			state = 2;
			continue;
		}
		if (state >= 2) {
			if (payload_len > 0) {
				if (written + payload_len > cap) payload_len = cap - written;
				if (payload_len) {
					memcpy(out + written, payload, payload_len);
					written += payload_len;
				}
				rcv_nxt = seq + (uint32_t)payload_len;
				send_tcp_segment(dmac, dst_ip, sport, dst_port, snd_nxt, rcv_nxt, 0x10 /* ACK */, 0, 0, 0x4000);
			}
			if (flags & 0x01 /* FIN */) {
				rcv_nxt = seq + 1;
				send_tcp_segment(dmac, dst_ip, sport, dst_port, snd_nxt, rcv_nxt, 0x10 /* ACK */, 0, 0, 0x4000);
				break;
			}
		}
	}
	if (out_len) *out_len = written;
	return written > 0 ? 0 : -2;
}