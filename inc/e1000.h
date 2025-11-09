#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public E1000 driver API */

/* Initialize first Intel E1000-compatible NIC found on PCI bus.
 * Returns 0 on success, <0 on error.
 */
int e1000_init(void);

/* Send a raw Ethernet frame. Returns 0 on success, <0 on error. */
int e1000_send(const void* data, size_t length);

/* Poll for a received frame.
 * If a frame is available and fits into 'buf', copies it, sets *out_len,
 * and returns 1. If no frame available, returns 0. If buffer too small,
 * returns -1 and sets *out_len to required size.
 */
int e1000_poll(uint8_t* buf, size_t bufsize, size_t* out_len);

/* Get current MAC address (6 bytes). Returns 0 on success, <0 if NIC not ready. */
int e1000_get_mac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif


