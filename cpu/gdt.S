.global lgdt_load
.global ltr_load
.global enter_user_mode_asm

lgdt_load:
        lgdt (%rdi)
        ret

ltr_load:
        mov %di, %ax
        ltr %ax
        ret

// void enter_user_mode_asm(uint64_t entry, uint64_t user_stack, uint16_t user_ds, uint16_t user_cs)
enter_user_mode_asm:
        // rdi=entry, rsi=user_stack, rdx=user_ds, rcx=user_cs
        // force RPL=3 on selectors
        or $3, %rdx
        or $3, %rcx

        // Prepare RFLAGS with IF=1 and build IRET frame
        pushfq
        pop %rax
        or $0x200, %rax                  // IF=1 in user image
        cli

        // IRET frame: SS, RSP, RFLAGS, CS, RIP
        pushq %rdx                           // SS (user_ds|3)
        pushq %rsi                           // RSP (user stack)
        pushq %rax                           // RFLAGS (with IF)
        pushq %rcx                           // CS (user_cs|3)
        pushq %rdi                           // RIP (entry)

        cld
        iretq
// ensure newline at end of file