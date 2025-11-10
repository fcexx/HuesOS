#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ip_header;

void icmp_init(void);
int icmp_send_echo(uint32_t dest_ip_hostorder, uint16_t ident, uint16_t sequence,
                   const uint8_t* payload, uint16_t payload_len);
int icmp_wait_echo(uint32_t expect_ip_hostorder, uint16_t ident, uint16_t sequence,
                   uint8_t* out, uint16_t max_len, uint32_t attempts);
void icmp_handle_ipv4(const struct ip_header* iph, const uint8_t* payload, uint16_t payload_len);

#define ICMP_WAIT_DEFAULT_ATTEMPTS 500000

#ifdef __cplusplus
}
#endif

#endif // NET_ICMP_H
