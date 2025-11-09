#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize lwIP stack and attach e1000 netif (NO_SYS/raw API).
 * IPs are in host byte order.
 * Returns 0 on success, <0 if lwIP is not enabled or on error.
 */
int lwip_stack_init(uint32_t ip, uint32_t netmask, uint32_t gateway);

/* Perform HTTP GET to dst_ip:dst_port using lwIP TCP and read response body.
 * Returns 0 on success, <0 on error or if lwIP not enabled.
 */
int lwip_http_get_ip(uint32_t dst_ip, uint16_t dst_port, const char* path,
                     uint8_t* out, size_t cap, size_t* out_len, uint32_t timeout_ms);

/* Blocking TCP helpers (busy-wait with sys_check_timeouts+service_input).
 * Provide a simple stream API for upper layers (e.g., TLS).
 */
typedef struct lwip_tcp_handle lwip_tcp_handle_t;

/* Create and connect to dst_ip:dst_port. Returns NULL on error. */
lwip_tcp_handle_t* lwip_tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms);
/* Send data. Returns bytes sent or <0 on error. */
int lwip_tcp_send(lwip_tcp_handle_t* h, const uint8_t* data, size_t len, uint32_t timeout_ms);
/* Receive up to len bytes. Returns bytes received (0 on EOF) or <0 on error/timeout. */
int lwip_tcp_recv(lwip_tcp_handle_t* h, uint8_t* out, size_t len, uint32_t timeout_ms);
/* Close and free. */
void lwip_tcp_close(lwip_tcp_handle_t* h);
/* Diagnostics helpers */
size_t lwip_tcp_pending(const lwip_tcp_handle_t* h);
int lwip_tcp_is_closed(const lwip_tcp_handle_t* h);
int lwip_tcp_errflag(const lwip_tcp_handle_t* h);

/* Pump lwIP once (timers + RX) for progressing WANT_READ/WRITE loops */
void lwip_pump_io(void);

/* Atomically copy up to 'want' bytes from the connection internal RX buffer
 * into 'out'. Returns number of bytes copied, 0 on EOF, or <0 on error/timeout.
 * This avoids extra pbuf allocations and lets callers pull large chunks. */
int lwip_tcp_consume(lwip_tcp_handle_t* h, uint8_t* out, size_t want, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif


