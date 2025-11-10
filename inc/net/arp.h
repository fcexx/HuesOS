#ifndef NET_ARP_H
#define NET_ARP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void arp_init(void);
void arp_request(uint32_t ip_hostorder);
int arp_try_get_mac(uint32_t ip_hostorder, uint8_t mac_out[6]);
void arp_handle_packet(const uint8_t* frame, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif // NET_ARP_H
