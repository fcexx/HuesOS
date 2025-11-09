#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Simple L2/L3 helper API to demonstrate ping (ARP + ICMP) */

/* Initialize network layer with static IPv4 addresses.
 * 'my_ip' and 'gateway_ip' are in host byte order (e.g., 0x0A00020F for 10.0.2.15).
 * Reads NIC MAC via e1000_get_mac().
 */
void net_init(uint32_t my_ip, uint32_t gateway_ip);

/* Poll and process incoming frames (IPv4/ARP). Call periodically or from IRQ. */
void net_poll(void);

/* Send one ICMP Echo Request to 'target_ip' (host order) and wait up to timeout_ms
 * for Echo Reply. Returns 0 on success (reply received), <0 on timeout/error.
 */
int net_ping(uint32_t target_ip, uint32_t timeout_ms);

/* Utility to pack IPv4 address from bytes (host order) */
static inline uint32_t ip4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

/* Minimal HTTP GET over raw TCP (no retransmissions).
 * Connects to dst_ip:dst_port, sends GET path and reads response until FIN or buffer full.
 * Returns 0 on success, <0 on error/timeout. out_len set to received size.
 */
int net_http_get(uint32_t dst_ip, uint16_t dst_port, const char* path,
                 uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms);

/* Resolve IPv4 A record via DNS server (dns_ip) using UDP.
 * host is a dot-separated name ("www.google.com").
 * On success: *out_ip (host order) set and returns 0; otherwise <0.
 */
int net_dns_query(const char* host, uint32_t dns_ip, uint32_t* out_ip, uint32_t timeout_ms);

/* Resolve ARP for target_ip; on success fills out_mac (6 bytes) and returns 0 */
int net_arp_resolve(uint32_t target_ip, uint8_t out_mac[6], uint32_t timeout_ms);
/* Return our configured IPv4 (host order) */
uint32_t net_get_my_ip(void);
/* Return gateway IP (host order) */
uint32_t net_get_gateway_ip(void);

#ifdef __cplusplus
}
#endif


