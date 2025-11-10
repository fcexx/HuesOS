#ifndef NET_IP_H
#define NET_IP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ip_init(void);
int ip_resolve_mac(uint32_t ip_hostorder, uint8_t mac_out[6]);
void ip_handle_frame(uint8_t* frame, uint16_t length);
int ip_poll_once(void);

uint32_t ip_get_arp_packets(void);
uint32_t ip_get_icmp_packets(void);
uint32_t ip_get_udp_packets(void);
uint32_t ip_get_dns_packets(void);

#ifdef __cplusplus
}
#endif

#endif // NET_IP_H
