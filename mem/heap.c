#include <heap.h>
#include <string.h>

// Very simple kernel heap: first-fit free list with headers, 16-byte alignment,
// coalescing on free. No thread safety assumed (callers should serialize).

typedef struct heap_block_header {
    size_t size;                 // payload size (bytes)
    struct heap_block_header* next;
    struct heap_block_header* prev;
    int    free;
} heap_block_header_t;

#define ALIGN16(x)   (((x) + 15) & ~((size_t)15))

static uint8_t* heap_base = 0;
static size_t   heap_capacity = 0;
static heap_block_header_t* head = 0;

extern uint8_t _end[]; // provided by linker as end of kernel image

void heap_init(uintptr_t heap_start, size_t heap_size) {
    if (heap_start == 0) {
        // Default: place heap right after kernel end, align to 16 bytes
        uintptr_t base = ((uintptr_t)_end + 0xFFF) & ~((uintptr_t)0xFFF);
        heap_start = base;
    }
    if (heap_size == 0) {
        // Default size: 16 MiB
        heap_size = 16ULL * 1024 * 1024;
    }

    heap_base = (uint8_t*)heap_start;
    heap_capacity = heap_size;

    head = (heap_block_header_t*)heap_base;
    head->size = heap_capacity - sizeof(heap_block_header_t);
    head->next = 0;
    head->prev = 0;
    head->free = 1;
}

static void split_block(heap_block_header_t* blk, size_t size) {
    size_t remaining = blk->size - size;
    if (remaining <= sizeof(heap_block_header_t) + 16) return; // too small to split
    heap_block_header_t* newblk = (heap_block_header_t*)((uint8_t*)blk + sizeof(heap_block_header_t) + size);
    newblk->size = remaining - sizeof(heap_block_header_t);
    newblk->free = 1;
    newblk->next = blk->next;
    newblk->prev = blk;
    if (newblk->next) newblk->next->prev = newblk;
    blk->next = newblk;
    blk->size = size;
}

static void coalesce(heap_block_header_t* blk) {
    // merge with next
    if (blk->next && blk->next->free) {
        blk->size += sizeof(heap_block_header_t) + blk->next->size;
        blk->next = blk->next->next;
        if (blk->next) blk->next->prev = blk;
    }
    // merge with prev
    if (blk->prev && blk->prev->free) {
        blk->prev->size += sizeof(heap_block_header_t) + blk->size;
        blk->prev->next = blk->next;
        if (blk->next) blk->next->prev = blk->prev;
        blk = blk->prev;
    }
}

void* kmalloc(size_t size) {
    if (!head || size == 0) return 0;
    size = ALIGN16(size);
    heap_block_header_t* cur = head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            split_block(cur, size);
            cur->free = 0;
            return (uint8_t*)cur + sizeof(heap_block_header_t);
        }
        cur = cur->next;
    }
    return 0; // out of memory
}

void kfree(void* ptr) {
    if (!ptr) return;
    heap_block_header_t* blk = (heap_block_header_t*)((uint8_t*)ptr - sizeof(heap_block_header_t));
    blk->free = 1;
    coalesce(blk);
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }
    heap_block_header_t* blk = (heap_block_header_t*)((uint8_t*)ptr - sizeof(heap_block_header_t));
    size_t old_size = blk->size;
    new_size = ALIGN16(new_size);
    if (new_size <= old_size) {
        split_block(blk, new_size);
        return ptr;
    }
    // try to grow in place if next is free and large enough
    if (blk->next && blk->next->free && old_size + sizeof(heap_block_header_t) + blk->next->size >= new_size) {
        blk->size += sizeof(heap_block_header_t) + blk->next->size;
        blk->next = blk->next->next;
        if (blk->next) blk->next->prev = blk;
        split_block(blk, new_size);
        return ptr;
    }
    void* n = kmalloc(new_size);
    if (!n) return 0;
    size_t to_copy = old_size < new_size ? old_size : new_size;
    memcpy(n, ptr, to_copy);
    kfree(ptr);
    return n;
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/**
 * kmalloc_aligned - Allocate memory with a specific alignment
 * 
 * @size:      Size of the allocation
 * @alignment: Required alignment, must be a power of two
 * 
 * Returns a pointer to the aligned memory, or NULL on failure.
 * The pointer must be freed with kfree_aligned().
 */
void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        // Alignment must be a power of two
        return 0;
    }

    // We need space for the requested size, plus alignment adjustment,
    // plus space to store the original pointer for kfree_aligned.
    size_t total_size = size + alignment - 1 + sizeof(void*);
    
    // Allocate the raw memory block
    void* raw_ptr = kmalloc(total_size);
    if (!raw_ptr) {
        return 0;
    }
    
    // Calculate the aligned pointer
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    
    // Store the original raw pointer just before the aligned address
    void** original_ptr_loc = (void**)(aligned_addr - sizeof(void*));
    *original_ptr_loc = raw_ptr;
    
    return (void*)aligned_addr;
}

/**
 * kfree_aligned - Free memory allocated with kmalloc_aligned
 */
void kfree_aligned(void* ptr) {
    if (!ptr) {
        return;
    }
    
    // Retrieve the original raw pointer stored just before the aligned block
    void** original_ptr_loc = (void**)((uintptr_t)ptr - sizeof(void*));
    void* raw_ptr = *original_ptr_loc;
    
    // Free the original raw block
    kfree(raw_ptr);
}


