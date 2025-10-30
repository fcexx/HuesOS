#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct {
        volatile uint32_t lock;
} spinlock_t;

// WARNING: ATOMIC
void acquire(spinlock_t* lock);
void release(spinlock_t* lock);

#endif