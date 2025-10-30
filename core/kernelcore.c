#include <huesos.h>
#include <stdint.h>
#include <gdt.h>
#include <vga.h>
#include <idt.h>

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    kprint("Initializing HuesOS kernel...\n");
    kprint("Initializing GDT...\n");
    init_gdt();
    kprint("Initializing IDT...\n");
    init_idt();
    kprint("\nOperating System done; endless cycle");
    for(;;);
}