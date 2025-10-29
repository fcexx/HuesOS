bits 16

org 0x7c00

_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; Load 8x8 font to get 80x50 mode
    mov ax, 1112h
    int 10h

    ; загрузить 11 секторов (начиная с сектора 2) в адрес 0x7e00
    ; BIOS передаёт номер загрузочного диска в DL, поэтому его не трогаем
    mov ah, 0x02    ; функция: read sectors
    mov al, 13      ; количество секторов
    xor ch, ch      ; cylinder = 0
    mov cl, 2       ; сектор = 2 (1-й сектор — bootloader)
    xor dh, dh      ; head = 0
    mov bx, 0x7e00  ; смещение в памяти (offset)
    mov es, ax      ; es = 0x0000
    int 0x13
    jc disk_error

    ; print success message
    mov si, success_msg
.print_char:
    lodsb
    cmp al, 0
    je .print_done
    mov ah, 0x0e
    int 0x10
    jmp .print_char
.print_done:

    ; передаём управление ядру по адресу 0x0000:0x7e00
    jmp 0x0000:0x7e00

disk_error:
    mov si, disk_error_message
.disk_print:
    lodsb
    cmp al, 0
    je .disk_hang
    mov ah, 0x0e
    int 0x10
    jmp .disk_print
.disk_hang:
    cli
.hang:
    hlt
    jmp .hang

success_msg: db "Boot read OK", 13, 10, 0
disk_error_message: db "Disk read error", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55