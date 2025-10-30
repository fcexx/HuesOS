section .multiboot2
align 8
mb2_start:
        dd 0xe85250d6
        dd 0
        dd mb2_end - mb2_start
        dd 0x100000000 - (0xe85250d6 + 0 + (mb2_end - mb2_start))

        align 8
        dw 0
        dw 0 
        dd 20
        dd 0
        dd 0
        dd 0

        align 8
        dw 0
        dw 0
        dd 8
mb2_end:

section .bss
align 16
stack_bottom:
        resb 16384
stack_top:

section .text
global _start
extern kernel_main

bits 32
_start:
        mov esp, stack_top

        push ebx
        push eax

        call check_cpuid
        call check_long_mode

        pop eax
        pop ebx
        mov edi, eax
        mov esi, ebx

        call setup_page_tables
        call enable_paging

        lea         eax, [tmp_gdt_ptr]
        lgdt        [eax]

        jmp 0x08:long_mode_start

        cli
        hlt

check_cpuid:
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 1 << 21
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd
        cmp eax, ecx
        je .no_cpuid
        ret
.no_cpuid:
        mov al, "1"
        jmp error

check_long_mode:
        mov eax, 0x80000000
        cpuid
        cmp eax, 0x80000001
        jb .no_long_mode
        mov eax, 0x80000001
        cpuid
        test edx, 1 << 29
        jz .no_long_mode
        ret
.no_long_mode:
        mov al, "2"
        jmp error

setup_page_tables:
        mov edi, page_table_l4
        xor eax, eax
        mov ecx, 4096
        rep stosd

        mov eax, page_table_l3
        or  eax, 0b11
        mov [page_table_l4], eax

        mov ecx, 0
.map_l3_table:
        mov eax, ecx
        shl eax, 30
        or  eax, 0b10000011
        mov [page_table_l3 + ecx*8], eax

        inc ecx
        cmp ecx, 4
        jne .map_l3_table

        mov eax, page_table_l3_fb
        or  eax, 0b11
        mov [page_table_l4 + 0x1F0*8], eax

        mov eax, 0xFD000000
        or  eax, 0b10000011
        mov [page_table_l3_fb], eax

        ret

enable_paging:
        mov eax, cr4
        or eax, 1 << 5
        mov cr4, eax

        mov eax, page_table_l4
        mov cr3, eax

        mov ecx, 0xC0000080
        rdmsr
        or eax, 1 << 8
        wrmsr

        mov eax, cr0
        or eax, 1 << 31
        mov cr0, eax

        ret

error:
        mov dword [0xb8000], 0x4f524f45
        mov dword [0xb8004], 0x4f3a4f52
        mov dword [0xb8008], 0x4f204f20
        mov byte  [0xb800a], al
        hlt

section .bss
align 4096
page_table_l4:
        resb 4096
page_table_l3:
        resb 4096
page_table_l3_fb:
        resb 4096

section .rodata
; ---------------- GDT ----------------
align 8
tmp_gdt:
        dq 0                                          ; null
        dq 0x00AF9A000000FFFF         ; kernel 64-bit code (DPL0)
        dq 0x00AF92000000FFFF         ; kernel data (DPL0)
tmp_gdt_end:

tmp_gdt_ptr:
        dw tmp_gdt_end - tmp_gdt - 1
        dq tmp_gdt

section .text
bits 64
long_mode_start:
        mov ax, 0x10                 ; kernel data selector in tmp_gdt
        mov ss, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        ; !!! включаем sse чтобы использовать fpu или sse инструкции для установки gdt
        mov rax, cr0
        and rax, ~(1 << 2)           ; CR0.EM = 0 (enable FPU/SSE instructions)
        or  rax,  (1 << 1)           ; CR0.MP = 1 (monitor coprocessor)
        mov cr0, rax

        mov rax, cr4
        or  rax, (1 << 9)            ; CR4.OSFXSR = 1 (FXSAVE/FXRSTOR + SSE)
        or  rax, (1 << 10)           ; CR4.OSXMMEXCPT = 1 (SSE exceptions)
        mov cr4, rax

	lea rsp, [rel stack_top]
	and rsp, -16
	sub rsp, 8
        cli
        call kernel_main

        cli
.hang:
        hlt
        jmp .hang 