#include <paging.h>

// Bootstrap page tables are defined in boot/multiboot.asm
// We import only L4. L3 identity maps first 4GiB using 1GiB pages already.
extern uint64_t page_table_l4[];   // 4KiB aligned, 512 entries

// Simple page-table allocator for creating new PDPT/PD tables for 2MiB mappings
static uint64_t* next_free_table(void) {
    static uint64_t pool[16][PT_ENTRIES] __attribute__((aligned(4096)));
    static size_t used = 0;
    if (used >= 16) return 0;
    for (size_t i = 0; i < PT_ENTRIES; i++) pool[used][i] = 0;
    return pool[used++];
}

static inline uint64_t read_cr3(void) {
    uint64_t v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v;
}
static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory");
}

void invlpg(void* va) { __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory"); }

void paging_init(void) {
    // Ensure CR3 is loaded with our L4 base (it already is after bootstrap)
    (void)read_cr3();
}

int map_page_2m(uint64_t va, uint64_t pa, uint64_t flags) {
    // Extract indices
    uint64_t l4i = (va >> 39) & 0x1FF;
    uint64_t l3i = (va >> 30) & 0x1FF;
    uint64_t l2i = (va >> 21) & 0x1FF;

    uint64_t* l4 = (uint64_t*)((uint64_t)page_table_l4);
    if (!(l4[l4i] & PG_PRESENT)) {
        uint64_t* new_l3 = next_free_table();
        if (!new_l3) return -1;
        l4[l4i] = ((uint64_t)new_l3) | PG_PRESENT | PG_RW;
    }

    uint64_t* l3 = (uint64_t*)(l4[l4i] & ~0xFFFULL);
    if (!(l3[l3i] & PG_PRESENT)) {
        uint64_t* new_l2 = next_free_table();
        if (!new_l2) return -2;
        l3[l3i] = ((uint64_t)new_l2) | PG_PRESENT | PG_RW;
    }

    uint64_t* l2 = (uint64_t*)(l3[l3i] & ~0xFFFULL);
    // Set 2MiB page entry
    l2[l2i] = (pa & ~(PAGE_SIZE_2M - 1)) | PG_PRESENT | PG_RW | PG_PS_2M | (flags & (PG_US|PG_PWT|PG_PCD|PG_GLOBAL));

    invlpg((void*)va);
    return 0;
}

int unmap_page_2m(uint64_t va) {
    uint64_t l4i = (va >> 39) & 0x1FF;
    uint64_t l3i = (va >> 30) & 0x1FF;
    uint64_t l2i = (va >> 21) & 0x1FF;
    uint64_t* l4 = (uint64_t*)((uint64_t)page_table_l4);
    if (!(l4[l4i] & PG_PRESENT)) return -1;
    uint64_t* l3 = (uint64_t*)(l4[l4i] & ~0xFFFULL);
    if (!(l3[l3i] & PG_PRESENT)) return -1;
    uint64_t* l2 = (uint64_t*)(l3[l3i] & ~0xFFFULL);
    l2[l2i] = 0;
    invlpg((void*)va);
    return 0;
}

/**
 * virtual_to_physical - Translate virtual address to physical address
 * 
 * This function walks the page tables to find the physical address
 * corresponding to a virtual address. Essential for DMA operations.
 * 
 * Returns physical address, or 0 if not mapped.
 */
uint64_t virtual_to_physical(uint64_t va) {
    uint64_t l4i = (va >> 39) & 0x1FF;
    uint64_t l3i = (va >> 30) & 0x1FF;
    uint64_t l2i = (va >> 21) & 0x1FF;
    uint64_t l1i = (va >> 12) & 0x1FF;
    
    uint64_t* l4 = (uint64_t*)((uint64_t)page_table_l4);
    
    // Check L4 entry
    if (!(l4[l4i] & PG_PRESENT)) {
        return 0;
    }
    
    uint64_t* l3 = (uint64_t*)(l4[l4i] & ~0xFFFULL);
    
    // Check L3 entry - might be 1GB page
    if (!(l3[l3i] & PG_PRESENT)) {
        return 0;
    }
    if (l3[l3i] & PG_PS_2M) {
        // 1GB page
        uint64_t pa_base = l3[l3i] & ~((1ULL << 30) - 1);
        uint64_t offset = va & ((1ULL << 30) - 1);
        return pa_base + offset;
    }
    
    uint64_t* l2 = (uint64_t*)(l3[l3i] & ~0xFFFULL);
    
    // Check L2 entry - might be 2MB page
    if (!(l2[l2i] & PG_PRESENT)) {
        return 0;
    }
    if (l2[l2i] & PG_PS_2M) {
        // 2MB page
        uint64_t pa_base = l2[l2i] & ~(PAGE_SIZE_2M - 1);
        uint64_t offset = va & (PAGE_SIZE_2M - 1);
        return pa_base + offset;
    }
    
    uint64_t* l1 = (uint64_t*)(l2[l2i] & ~0xFFFULL);
    
    // Check L1 entry - 4KB page
    if (!(l1[l1i] & PG_PRESENT)) {
        return 0;
    }
    
    uint64_t pa_base = l1[l1i] & ~0xFFFULL;
    uint64_t offset = va & 0xFFFULL;
    return pa_base + offset;
}


