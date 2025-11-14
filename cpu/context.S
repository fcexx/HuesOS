.global context_switch
// void context_switch(context_t *old context_t *new)
// old: rdi, new: rsi
context_switch:
        // saving only callee-saved registers
        movq %rbx, 0x68(%rdi)
        movq %rbp, 0x50(%rdi)
        movq %r12, 0x18(%rdi)
        movq %r13, 0x10(%rdi)
        movq %r14, 0x08(%rdi)
        movq %r15, 0x00(%rdi)
        movq %rsp, 0x80(%rdi)
        pushfq
        popq %rax
        /* Ensure IF (bit 9) is set in saved RFLAGS so resumed threads have interrupts enabled */
        orq $0x200, %rax
        movq %rax, 0x88(%rdi)

        // restoring!!
        movq 0x68(%rsi), %rbx
        movq 0x50(%rsi), %rbp
        movq 0x18(%rsi), %r12
        movq 0x10(%rsi), %r13
        movq 0x08(%rsi), %r14
        movq 0x00(%rsi), %r15
        movq 0x80(%rsi), %rsp
        pushq 0x88(%rsi)
        popfq
        // my favorite 'ret'
        ret