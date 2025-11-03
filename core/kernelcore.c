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
    // build in shell
    for (;;) {
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
                kprintf("%s x86_64 version %s (2025)\nAuthors: kotazzz, fcexx, dasteldi, whiterose\n", OS_NAME, OS_VERSION);
                kprintf("GitHub organization: <(0b)>https://github.com/Axon-company\n");
                kprintf("Official site: <(0b)>wh27961.web4.maze-tech.ru\n");
                kprintf("Axon team 2025. All rights reserved.\n\n");
                kprintf("CPU: %s\n", sysinfo_cpu_name());
                //kprintf("RAM: %d MB\n", sysinfo_ram_mb());
                kprintf("PC: %s\n", sysinfo_pc_type() ? "BIOS" : "UEFI");
            }
            else if (strcmp(tokens[0], "exit") == 0) {
                exit = 1;
                return;
            }
            else if (strcmp(tokens[0], "echo") == 0) {
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
    
<<<<<<< HEAD
    kprintf("<(0b)> °±²ÛÛÛÛÛÛ²±°°±²Û²±°°±²Û²±°°±²ÛÛÛÛÛÛ²±°°±²ÛÛÛÛÛÛÛ²±° °±²ÛÛÛÛÛÛ²±° °±²ÛÛÛÛÛÛÛ²±°\n");
    kprintf("<(0b)>°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°\n");
    kprintf("<(0b)>°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°\n");
    kprintf("<(0b)>°±²ÛÛÛÛÛÛÛÛ²±°°±²ÛÛÛÛÛÛ²±°°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°°±²ÛÛÛÛÛÛ²±°\n");
    kprintf("<(0b)>°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°      °±²Û²±°\n");
    kprintf("<(0b)>°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°      °±²Û²±°\n");
    kprintf("<(0b)>°±²Û²±°°±²Û²±°±²Û²±°°±²Û²±°°±²ÛÛÛÛÛÛ²±°°±²Û²±°°±²Û²±°°±²ÛÛÛÛÛÛ²±°°±²ÛÛÛÛÛÛÛ²±°\n\n");
=======
    kprintf("<(0b)> øñý??????ýñøøñý?ýñøøñý?ýñøøñý??????ýñøøñý???????ýñø øñý??????ýñø øñý???????ýñø\n");
    kprintf("<(0b)>øñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñø\n");
    kprintf("<(0b)>øñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñø\n");
    kprintf("<(0b)>øñý????????ýñøøñý??????ýñøøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøøñý??????ýñø\n");
    kprintf("<(0b)>øñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñø      øñý?ýñø\n");
    kprintf("<(0b)>øñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøñý?ýñøøñý?ýñø      øñý?ýñø\n");
    kprintf("<(0b)>øñý?ýñøøñý?ýñøñý?ýñøøñý?ýñøøñý??????ýñøøñý?ýñøøñý?ýñøøñý??????ýñøøñý???????ýñø\n\n");
>>>>>>> whiterose

    kprint("Initializing kernel...\n");
    /* getting system information */
    sysinfo_init(multiboot_magic, multiboot_info);

    gdt_init();
    idt_init();
    pic_init();
    pit_init();

    paging_init();
    heap_init(0, 0);

    thread_init();
    iothread_init();

    ps2_keyboard_init();
    asm volatile("sti");

    kprintf("kernel base: done (idt, gdt, pic, pit, paging, heap, keyboard)\n");
    kprintf("\n<(0f)>Welcome to %s <(0b)>%s<(0f)>!\n", OS_NAME, OS_VERSION);
    kprint("shell: ring0 build-in shell\n");

    ring0_shell();  

    kprint("\nShutting down in 5 seconds...");
    pit_sleep_ms(5000);
    shutdown_system();
    kprintf("Shutdown. If PC is not ACPI turn off power manually");
    for(;;);
}