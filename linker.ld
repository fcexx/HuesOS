OUTPUT_FORMAT("elf32-i386")
ENTRY(kernel_main)

SECTIONS
{
    . = 0x00100000;

    .text :
    {
        *(.multiboot)
        *(.text*)
    }

    .rodata : { *(.rodata*) }

    .data :
    {
        *(.data*)
        *(.bss*)
    }

    .bss : { *(.bss) }

    /DISCARD/ : { *(.eh_frame) }
}