org 0x7e00

start:
    mov si, welcome
    call print
    mov si, console
    call print
    mov bx, ds
    mov es, bx
    mov di, command
    mov dl, 0
    jmp input

print:
    lodsb
    cmp al, 0
    je done
    mov ah, 0x0e
    int 0x10
    jmp print

done:
    ret

input:
    mov ah, 0x00
    int 0x16
    cmp al, 8
    je input
    cmp al, 13
    je check
    stosb
    mov ah, 0x0e
    int 0x10
    inc dl
    jmp input

check:
    mov si, enter
    call print
    mov bl, 0

    mov si, command
    mov di, command_about
    mov cx, 5
    repe cmpsb
    je print_about

    mov si, command
    mov di, command_cls
    mov cx, 3
    repe cmpsb
    je cls

    mov si, command
    mov di, command_hardware
    mov cx, 8
    repe cmpsb
    je hardware_check

    mov si, command
    mov di, command_help
    mov cx, 4
    repe cmpsb
    je print_help

    mov si, command
    mov di, command_mem
    mov cx, 3
    repe cmpsb
    je mem

    mov si, command
    mov di, command_reboot
    mov cx, 6
    repe cmpsb
    je reboot

    mov si, command
    mov di, command_time
    mov cx, 4
    repe cmpsb
    je time

    cmp dl, 0
    je return

    mov si, error
    call print
    mov si, console
    call print
    mov si, 0
    mov bx, ds
    mov es, bx
    mov di, command
    mov dl, 0
    jmp input

print_about:
    mov si, about
    call print

    jmp return

cls:
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    jmp return

hardware_check:
    cmp bl, 0
    je check_disk_drive
    cmp bl, 1
    je check_coprocessor

    jmp return

check_disk_drive:
    mov si, disk_drive
    call print
    int 0x11
    shr al, 7
    inc bl

    cmp al, 0
    je no
    cmp al, 0
    jne yes

check_coprocessor:
    mov si, coprocessor
    call print
    int 0x11
    shr al, 6
    shl al, 1
    inc bl

    cmp al, 0
    je no
    cmp al, 0
    jne yes

no:
    mov si, hardware_no
    call print
    mov si, enter
    call print
    jmp hardware_check

yes:
    mov si, hardware_yes
    call print
    mov si, enter
    call print
    jmp hardware_check

print_help:
    mov si, help
    call print

    jmp return

mem:
    lodsb
    mov ah, 0x0e
    int 0x10
    jmp mem

reboot:
    jmp 0xFFFF:0x0000

time:
    mov ah, 0x01
    mov ch, 0x20
    mov cl, 0x00
    int 0x10

    mov al, 13
    mov ah, 0x0e
    int 0x10
    
    mov ah, 0x02
    int 0x1a

    mov dl, ch
    call convert
    mov al, ":"
    mov ah, 0x0e
    int 0x10

    mov dl, cl
    call convert
    mov al, ":"
    mov ah, 0x0e
    int 0x10

    mov dl, dh
    call convert
    jmp time

convert:
    mov al, dl
    mov ah, al
    shr ah, 4
    add ah, "0"
    mov bl, ah
    
    mov al, dl
    and al, 0Fh
    add al, "0"
    mov bh, al

    mov al, bl
    mov ah, 0x0e
    int 0x10

    mov al, bh
    mov ah, 0x0e
    int 0x10

    mov al, 0
    mov bl, 0
    ret

return:
    mov si, console
    call print
    mov si, 0
    mov bx, ds
    mov es, bx
    mov dx, 0
    mov [command], dx
    mov di, command
    mov dl, 0
    jmp input

welcome: db "Welcome to HuesOS!", 10, 13, "Type <help> to show available commands.", 13, 10, 0
console: db "HuesOS> ", 0
enter: db 10, 13, 0
command_about: db "about"
command_cls: db "cls"
command_hardware: db "hardware"
command_help: db "help"
command_mem: db "mem"
command_reboot: db "reboot"
command_time: db "time"
about: db "System:", 10, 13, "  1. HuesOS", 10, 13, "  2. Version: Alpha 1.7", 10, 13, "  3. Made by @acvmn", 10, 13, 0
disk_drive: db "Hardware:", 10, 13, "  1. Are the disk drives installed: ", 0
coprocessor: db "  2. Are the coprocessor installed: ", 0
hardware_yes: db "YES", 0
hardware_no: db "NO", 0
help: db "Available Commands:", 10, 13, "  1. ABOUT - displaying information about the system.", 10, 13, "  2. CLS - clear the screen.", 10, 13,  "  3. HARDWARE - hardware display.", 10, 13, "  4. HELP - displaying available commands.", 10, 13, "  5. MEM - launches the memory app.", 10, 13, "  6. REBOOT - reboot the computer.", 10, 13, "  7. TIME - launches the watch app.", 10, 13, 0
error: db "Unknown command!", 10, 13, 0
command: db ""

times 5120-($-$$) db 0