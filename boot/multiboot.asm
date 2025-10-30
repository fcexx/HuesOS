BITS 32

; Minimal multiboot header: placed into .multiboot section and linked at start of ELF
section .multiboot
    ALIGN 4
    MB_MAGIC equ 0x1BADB002
    MB_FLAGS equ 0x00000003
    MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM