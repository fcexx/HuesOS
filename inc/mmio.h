#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * MMIO (Memory-Mapped I/O) support.
 *
 * Provides:
 *  - ioremap/iounmap to map/unmap a physical MMIO range into kernel VA
 *  - lightweight memory barriers (mb/rmb/wmb) to order MMIO accesses
 *  - safe volatile 8/16/32/64-bit reads and writes
 *
 * Implementation uses 2 MiB pages and places mappings in a dedicated
 * virtual window above 4 GiB. Ranges are always mapped RW, NX, UC (PCD|PWT).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Memory barriers to order MMIO accesses */
static inline void mmio_mb(void) {
	__asm__ volatile("mfence" ::: "memory");
}
static inline void mmio_rmb(void) {
	__asm__ volatile("lfence" ::: "memory");
}
static inline void mmio_wmb(void) {
	__asm__ volatile("sfence" ::: "memory");
}

/* Map the physical range [phys_addr, phys_addr + size) into virtual memory.
 * Returns a virtual pointer corresponding to phys_addr within the created window,
 * or 0 on error. The range is mapped with flags: RW, NX, UC (PCD|PWT).
 */
void* ioremap(uint64_t phys_addr, size_t size);

/* Unmap a mapping created by ioremap. Returns 0 on success, <0 on error.
 * Virtual address space is not recycled yet (PTEs are just cleared).
 */
int iounmap(void* virt_addr, size_t size);

/* Low-level volatile MMIO register accesses */
static inline uint8_t mmio_read8(const volatile void* addr) {
	return *(const volatile uint8_t*)addr;
}
static inline uint16_t mmio_read16(const volatile void* addr) {
	return *(const volatile uint16_t*)addr;
}
static inline uint32_t mmio_read32(const volatile void* addr) {
	return *(const volatile uint32_t*)addr;
}
static inline uint64_t mmio_read64(const volatile void* addr) {
	return *(const volatile uint64_t*)addr;
}

static inline void mmio_write8(volatile void* addr, uint8_t val) {
	*(volatile uint8_t*)addr = val;
}
static inline void mmio_write16(volatile void* addr, uint16_t val) {
	*(volatile uint16_t*)addr = val;
}
static inline void mmio_write32(volatile void* addr, uint32_t val) {
	*(volatile uint32_t*)addr = val;
}
static inline void mmio_write64(volatile void* addr, uint64_t val) {
	*(volatile uint64_t*)addr = val;
}

#ifdef __cplusplus
}
#endif


