#include <spinlock.h>

void acquire(spinlock_t* lock) {
        /* Atomic spinlock, don't use very much */
        while (__sync_lock_test_and_set(&lock->lock, 1));
}

void release(spinlock_t* lock) {
        __sync_lock_release(&lock->lock);
}