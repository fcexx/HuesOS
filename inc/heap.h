#pragma once

#include <stddef.h>
#include <stdint.h>

// Initialize kernel heap. If start/size are zero, implementation chooses defaults.
void heap_init(uintptr_t heap_start, size_t heap_size);

void* kmalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);
void* kcalloc(size_t num, size_t size);
void* kmalloc_aligned(size_t size, size_t alignment);
void  kfree_aligned(void* ptr);


