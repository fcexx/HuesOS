#include "../inc/mmio.h"
#include "../inc/paging.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Minimal MMIO window manager:
 *  - Dedicated virtual area for MMIO starts at 4 GiB
 *  - Hands out contiguous, 2 MiB–aligned chunks; VA space is not recycled
 *  - Each 2 MiB page is mapped with flags: RW | NX | UC (PCD|PWT)
 */

#define MMIO_VA_BASE          (0x0000000100000000ULL)   /* 4 GiB */
#define MMIO_GRANULE          (PAGE_SIZE_2M)

static uint64_t mmio_next_va = MMIO_VA_BASE;

static inline uint64_t align_down(uint64_t v, uint64_t a) {
	return v & ~(a - 1);
}

static inline uint64_t align_up(uint64_t v, uint64_t a) {
	return (v + (a - 1)) & ~(a - 1);
}

void* ioremap(uint64_t phys_addr, size_t size) {
	if (size == 0) return 0;

	/* Align physical base to 2 MiB and compute offset */
	uint64_t phys_base = align_down(phys_addr, MMIO_GRANULE);
	uint64_t offset    = phys_addr - phys_base;
	uint64_t total     = offset + (uint64_t)size;
	uint64_t page_cnt  = (align_up(total, MMIO_GRANULE)) / MMIO_GRANULE;

	/* Pick next virtual range */
	uint64_t va_base = align_up(mmio_next_va, MMIO_GRANULE);

	/* Flags: RW | NX | UC (PCD|PWT). US=0 (kernel only) */
	uint64_t flags = PG_RW | PG_PWT | PG_PCD | PG_NX;

	for (uint64_t i = 0; i < page_cnt; i++) {
		uint64_t va = va_base + i * MMIO_GRANULE;
		uint64_t pa = phys_base + i * MMIO_GRANULE;
		if (map_page_2m(va, pa, flags) != 0) {
			/* Rollback already created mappings */
			for (uint64_t j = 0; j < i; j++) {
				unmap_page_2m(va_base + j * MMIO_GRANULE);
			}
			return 0;
		}
	}

	mmio_next_va = va_base + page_cnt * MMIO_GRANULE;

	return (void*)(va_base + offset);
}

int iounmap(void* virt_addr, size_t size) {
	if (!virt_addr || size == 0) return -1;

	uint64_t va_start = (uint64_t)virt_addr;
	uint64_t aligned_start = align_down(va_start, MMIO_GRANULE);
	uint64_t offset        = va_start - aligned_start;
	uint64_t total         = offset + (uint64_t)size;
	uint64_t page_cnt      = (align_up(total, MMIO_GRANULE)) / MMIO_GRANULE;

	for (uint64_t i = 0; i < page_cnt; i++) {
		uint64_t va = aligned_start + i * MMIO_GRANULE;
		unmap_page_2m(va);
	}

	/* We do not recycle virtual space for simplicity */
	return 0;
}


