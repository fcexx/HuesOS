#ifndef CONTEXT_H
#define CONTEXT_H
#include <stdint.h>

typedef struct context {
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
        uint64_t rip, rsp;
        uint64_t rflags;
} context_t;

#ifdef __cplusplus
extern "C" {
#endif

void context_switch(context_t *old_ctx, context_t *new_ctx);

#ifdef __cplusplus
}
#endif

#endif // CONTEXT_H 