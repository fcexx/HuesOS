#include <axonos.h>
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
#include <thread.h>

#include <iothread.h>

int exit = 0;

void ring0_shell()  {
    static char input_buf[512];
    char *input;
    exit = 0;
    // Ensure interrupts are enabled in this thread context so PS/2 IRQs are delivered
    asm volatile("sti");
    // build in shell
    for (;;) {
        // Log RFLAGS/tid at shell prompt to verify interrupts in shell context
        unsigned long long _rf = 0;
        asm volatile("pushfq; pop %0" : "=r"(_rf));
        thread_t* _cur = thread_current();
        int _tid = _cur ? _cur->tid : -1;
        qemu_debug_printf("ring0_shell: prompt tid=%d rflags=0x%x\n", _tid, (unsigned int)_rf);

        kprintf("<(0f)>AxonOS > ");
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
            else if (strcmp(tokens[0], "thread") == 0) {
                if (ntok == 1) {
                    kprint("Available commands:\n");
                    kprint("thread list - list all threads\n");
                    kprint("thread stop <pid> - stop a thread\n");
                    kprint("thread block <pid> - block a thread\n");
                    kprint("thread unblock <pid> - unblock a thread\n");
                } else if (ntok > 1) {
                    if (strcmp(tokens[1], "list") == 0) {
                        for (int i = 0; i < thread_get_count(); i++) {
                            kprintf("%d: %s - %s\n", thread_get(i)->tid, thread_get(i)->name, thread_get(i)->state == THREAD_RUNNING ? "running" : thread_get(i)->state == THREAD_READY ? "ready" : thread_get(i)->state == THREAD_BLOCKED ? "blocked" : thread_get(i)->state == THREAD_TERMINATED ? "terminated" : thread_get(i)->state == THREAD_SLEEPING ? "sleeping" : "unknown");
                        }
                    } else if (strcmp(tokens[1], "stop") == 0) {
                        if (ntok < 3) {
                            kprintf("<(0c)>thread stop: missing pid\n");
                        } else {
                            thread_stop(atoi(tokens[2]));
                        }
                    } else if (strcmp(tokens[1], "block") == 0) {
                        if (ntok < 3) {
                            kprintf("<(0c)>thread block: missing pid\n");
                        } else {
                            thread_block(atoi(tokens[2]));
                        }
                    } else if (strcmp(tokens[1], "unblock") == 0) {
                        if (ntok < 3) {
                            kprintf("<(0c)>thread unblock: missing pid\n");
                        } else {
                            thread_unblock(atoi(tokens[2]));
                        }
                    }
                }
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
                kprintf("%s x86_64 version %s (2025)\nAuthors: Nenboard, fcexx\n", OS_NAME, OS_VERSION);
                kprintf("CPU: %s\n", sysinfo_cpu_name());
                //kprintf("RAM: %d MB\n", sysinfo_ram_mb());
                kprintf("PC: %s\n", sysinfo_pc_type() ? "BIOS" : "UEFI");
            }
            else if (strcmp(tokens[0], "exit") == 0) {
                exit = 1;
                return;
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
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    kclear();
    kprint("Initializing kernel...\n");
    // Инициализируем сбор системной информации (CPUID, multiboot hints)
    sysinfo_init(multiboot_magic, multiboot_info);
    idt_init();
    gdt_init();
    pic_init();
    pit_init();
    paging_init();
    heap_init(0, 0);
    thread_init();
    iothread_init();
    ps2_keyboard_init();

    // Diagnostics: print RFLAGS and addresses of ISR stubs, then trigger soft-IRQs
    {
        unsigned long long rflags = 0;
        asm volatile("pushfq; pop %%rax" : "=a"(rflags));
        qemu_debug_printf("diag: RFLAGS before STI = 0x%x\n", (unsigned int)rflags);
        // Address of isr stubs for vectors 32 and 33
        extern uint64_t isr_stub_table[];
        qemu_debug_printf("diag: isr_stub[32]=0x%x isr_stub[33]=0x%x\n", (unsigned int)isr_stub_table[32], (unsigned int)isr_stub_table[33]);

        // Trigger software interrupts to verify IDT/isr_dispatch wiring
        qemu_debug_printf("diag: invoking soft int 32\n");
        asm volatile("int $32");
        qemu_debug_printf("diag: invoked soft int 32\n");
        qemu_debug_printf("diag: invoking soft int 33\n");
        asm volatile("int $33");
        qemu_debug_printf("diag: invoked soft int 33\n");
    }

    kprintf("kernel base: done (idt, gdt, pic, pit, paging, heap, keyboard)\n");
    kprintf("\n<(0f)>Welcome to %s <(0b)>%s!\n", OS_NAME, OS_VERSION);
    kprint("Shell: HuesSH ring0\n");
    kprint("Type \"help\" to show available commands\n");
    asm volatile("sti");

    ring0_shell();

    kprint("\nShutting down in 5 seconds...");
    pit_sleep_ms(5000);
    shutdown_system();
    kprintf("Shutdown. If PC is not ACPI turn off power manually");
    for(;;);
}