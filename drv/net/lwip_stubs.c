/* Minimal local stubs to replace lwIP headers after third_party removal.
 * Provide tiny typedefs used by linker-visible symbols in this file.
 */
#include <stdint.h>
#include <stddef.h>

typedef uint16_t u16_t;
typedef int err_t;

struct pbuf {
	void *payload;
	u16_t len;
	u16_t tot_len;
	struct pbuf *next;
};

typedef struct { uint32_t addr; } ip4_addr_t;
struct netif { int dummy; };

/* Stubs to satisfy linker if some features are referenced by lwIP tables.
 * We disable IP_FRAG and IP_REASSEMBLY, but some objects may still weakly
 * reference timers/handlers; provide no-op versions.
 */
void ip_reass_tmr(void) { }

struct pbuf* ip4_reass(struct pbuf* p) {
	/* Pass-through (no reassembly) */
	return p;
}

err_t ip4_frag(struct pbuf* p, struct netif* netif, const ip4_addr_t* dest) {
	(void)p; (void)netif; (void)dest;
	return 0;
}


