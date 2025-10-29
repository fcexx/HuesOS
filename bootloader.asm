org 0x7c00

start:
    sub ax, ax
    mov sp, 0x7C00
    jmp 0x7e00

times 510-($-$$) db 0
dw 0xAA55