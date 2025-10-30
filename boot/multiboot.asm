BITS 32

; Minimal multiboot header: placed into .multiboot section and linked at start of ELF
section .multiboot
    ALIGN 4
    dd 0x1BADB002
    dd 0x00000003
    dd -(0x1BADB002 + 0x00000003)

section .text
global _kernel_main
_kernel_main:
    push ebx
    push eax
    extern kernel_main
    sti
    call kernel_main
    add esp, 8
    ret