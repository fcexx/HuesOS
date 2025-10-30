#include <huesos.h>
#include <keyboard.h>
#include <stdint.h>
#include <gdt.h>
#include <vga.h>
#include <idt.h>
#include <pic.h>

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    kprint("Initializing HuesOS kernel...\n");
    kprint("Initializing IDT...\n");
    idt_init();
    kprint("Initializing GDT...\n");
    gdt_init();
    pic_init();
    ps2_keyboard_init();
    kprint("\nWelcome to HuesOS!\n");
    kprint("Shell: HuesSH ring0\n");
    kprint("Type \"help\" to show available commands\n");
    asm volatile("sti");
    char *input;
    for (;;) {
        kprint("HuesOS> ");
        input = kgets(input, 512);
        if (strcmp(input, "help") == 0) {
            kprint("Available commands:\n");
            
        } else if (strcmp(input, "exit") == 0) {
            break;
        }
    }

    kprint("\nOperating System done; endless cycle");
    for(;;);
}