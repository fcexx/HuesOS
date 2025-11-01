#include <spinlock.h>

void acquire(spinlock_t* lock) {
        /* Default atomic acquire for compatibility */
        while (__sync_lock_test_and_set(&lock->lock, 1));
}

void release(spinlock_t* lock) {
        __sync_lock_release(&lock->lock);
}

// Попытка захватить спинлок без блокировки (используется в ISR)
int try_acquire(spinlock_t* lock) {
        return (__sync_lock_test_and_set(&lock->lock, 1) == 0) ? 1 : 0;
}

// IRQ-save variants: disable interrupts while acquiring, save rflags
void acquire_irqsave(spinlock_t* lock, unsigned long* rflags) {
        unsigned long flags;
        asm volatile("pushfq; pop %0" : "=r"(flags));
        *rflags = flags;
        asm volatile("cli" ::: "memory");
        while (__sync_lock_test_and_set(&lock->lock, 1));
}

void release_irqrestore(spinlock_t* lock, unsigned long rflags) {
        __sync_lock_release(&lock->lock);
        asm volatile("push %0; popfq" :: "r"(rflags) : "memory");
}