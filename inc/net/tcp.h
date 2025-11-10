#ifndef NET_TCP_H
#define NET_TCP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ip_header;

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT1   3
#define TCP_STATE_FIN_WAIT2   4
#define TCP_STATE_TIME_WAIT   5
#define TCP_STATE_CLOSE_WAIT  6
#define TCP_STATE_LAST_ACK    7

void tcp_init(void);
int tcp_connect(uint32_t dest_ip_hostorder, uint16_t dest_port);
int tcp_send(const uint8_t* data, uint16_t len, uint32_t attempts);
int tcp_recv(uint8_t* out, uint16_t max_len, uint32_t attempts);
void tcp_close(void);
void tcp_handle_ipv4(const struct ip_header* iph, const uint8_t* payload, uint16_t payload_len);
int tcp_get_state(void);
int tcp_remote_closed(void);

#ifdef __cplusplus
}
#endif

#endif // NET_TCP_H
