#ifndef NET_UDP_H
#define NET_UDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ip_header;

void udp_init(void);
int udp_send(uint32_t dest_ip_hostorder, uint16_t src_port, uint16_t dest_port,
             const uint8_t* data, uint16_t len);
int udp_wait(uint16_t local_port, uint32_t expect_ip_hostorder, uint16_t expect_src_port,
             uint8_t* out, uint16_t max_len, uint32_t attempts);
void udp_handle_ipv4(const struct ip_header* iph, const uint8_t* payload, uint16_t payload_len);

uint32_t udp_get_packet_count(void);
uint32_t udp_get_dns_packet_count(void);

#define UDP_DEFAULT_LOCAL_PORT 12345
#define UDP_WAIT_DEFAULT_ATTEMPTS 500000

#ifdef __cplusplus
}
#endif

#endif // NET_UDP_H
