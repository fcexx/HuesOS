#include <huesos.h>
#include <keyboard.h>
#include <stdint.h>
#include <gdt.h>
#include <string.h>
#include <vga.h>
#include <idt.h>
#include <pic.h>
#include <pit.h>
#include <heap.h>
#include <paging.h>
#include <snake.h>
#include <sysinfo.h>

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    kprintf("x86_64 kernel loaded: address: %p, magic: 0x%08x, mbinfo: 0x%08x\n", kernel_main, multiboot_magic, multiboot_info);
    kprint("Initializing kernel...\n");
    // Инициализируем сбор системной информации (CPUID, multiboot hints)
    sysinfo_init(multiboot_magic, multiboot_info);
    idt_init();
    gdt_init();
    pic_init();
    pit_init();
    paging_init();
    heap_init(0, 0);
    ps2_keyboard_init();
    kprintf("kernel base: done (idt, gdt, pic, pit, paging, heap, keyboard)\n");
    kprintf("\n<(0f)>Welcome to %s <(0b)>%s!\n", OS_NAME, OS_VERSION);
    kprint("Shell: HuesSH ring0\n");
    kprint("Type \"help\" to show available commands\n");
    asm volatile("sti");
    static char input_buf[512];
    char *input;
    for (;;) {
        kprintf("<(0f)>HuesOS > ");
        input = kgets(input_buf, 512);
        //if (!input) { asm volatile("hlt"); continue; }
        int ntok = 0;
        char **tokens = split(input, " ", &ntok);
        if (ntok > 0) { 
            if (strcmp(tokens[0], "help") == 0) {
                kprint("Available commands:\n");
                kprint("help - show available commands\n");
                kprint("clear, cls - clear the screen\n");
                kprint("reboot - reboot the system\n");
                kprint("shutdown - shutdown the system\n");
                kprint("echo <text> - print text\n");
                kprint("snake - run the snake game\n");
                kprint("about - show information about authors and system\n");
                kprint("exit - exit the shell\n");
            } 
            else if (strcmp(tokens[0], "clear") == 0) {
                kclear();
            }
            else if (strcmp(tokens[0], "snake") == 0) {
                snake_run();
            }
            else if (strcmp(tokens[0], "cls") == 0) {
                kclear();
            }
            else if (strcmp(tokens[0], "reboot") == 0) {
                reboot_system();
            }
            else if (strcmp(tokens[0], "shutdown") == 0) {
                shutdown_system();
            }
            else if (strcmp(tokens[0], "about") == 0) {
                kprintf("%s x86_64 (new!) version %s (2025)\nAuthors: %s (Nenboard, fcexx)\n", OS_NAME, OS_VERSION, OS_AUTHORS);
                kprintf("CPU: %s\n", sysinfo_cpu_name());
                //kprintf("RAM: %d MB\n", sysinfo_ram_mb());
                kprintf("PC: %s\n", sysinfo_pc_type() ? "BIOS" : "UEFI");
            }
            else if (strcmp(tokens[0], "exit") == 0) {
                break;
            }
            else if (strcmp(tokens[0], "echo") == 0) {
                // печатаем остаток строки интерпретируя цветовые теги
                const char* p = input;
                while (*p == ' ' || *p == '\t') p++;
                const char* word = "echo";
                while (*p && *word && *p == *word) { p++; word++; }
                while (*p == ' ' || *p == '\t') p++;
                kprint_colorized(p);
                kprint("\n");
            }
            else {
                kprintf("<(0c)>%s: command or program not found\n", tokens[0]);
            }
        }
        for (int i = 0; tokens && tokens[i]; i++) kfree(tokens[i]);
        if (tokens) kfree(tokens);
    }

    kprint("\nShutting down in 5 seconds...");
    pit_sleep_ms(5000);
    shutdown_system();
    for(;;);
}