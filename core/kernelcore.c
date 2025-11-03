#include <axonos.h>
#include <keyboard.h>
#include <stdint.h>
#include <gdt.h>
#include <string.h>
#include <vga.h>
#include <idt.h>
#include <pic.h>
#include <pit.h>
#include <rtc.h>
#include <heap.h>
#include <paging.h>
#include <snake.h>
#include <tetris.h>
#include <clock.h>
#include <sysinfo.h>
#include <thread.h>
#include <neofetch.h>

#include <iothread.h>

int exit = 0;

void ring0_shell()  {
    static char input_buf[512];
    char *input;
    // build in shell
    for (;;) {
        kprintf("<(0f)>AxonOS > ");
        input = kgets(input_buf, 512);
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
                kprint("tetris - run the tetris game\n");
                kprint("clock - run the analog clock\n");
                kprint("time - show current time from RTC\n");
                kprint("date - show current date from RTC\n");
                kprint("uptime - show system uptime based on RTC ticks\n");
                kprint("about - show information about authors and system\n");
                kprint("neofetch - show system info with logo\n");
                kprint("exit - exit the shell\n");
            } 
            else if (strcmp(tokens[0], "clear") == 0) {
                kclear();
            }
            else if (strcmp(tokens[0], "snake") == 0) {
                snake_run();
            }
            else if (strcmp(tokens[0], "tetris") == 0) {
                tetris_run();
            }
            else if (strcmp(tokens[0], "clock") == 0) {
                clock_run();
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
                            kprintf("%d: %s - %s\n", thread_get(i)->tid, thread_get(i)->name, 
                                thread_get(i)->state == THREAD_RUNNING ? "running" : 
                                thread_get(i)->state == THREAD_READY ? "ready" : 
                                thread_get(i)->state == THREAD_BLOCKED ? "blocked" : 
                                thread_get(i)->state == THREAD_TERMINATED ? "terminated" : 
                                thread_get(i)->state == THREAD_SLEEPING ? "sleeping" : "unknown");
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
                kprintf("PC: %s\n", sysinfo_pc_type() ? "BIOS" : "UEFI");
            }
            else if (strcmp(tokens[0], "time") == 0) {
                rtc_datetime_t dt;
                rtc_read_datetime(&dt);
                kprintf("Current time: <(0b)>%02d:%02d:%02d\n", 
                    dt.hour, dt.minute, dt.second);
            }
            else if (strcmp(tokens[0], "date") == 0) {
                rtc_datetime_t dt;
                rtc_read_datetime(&dt);
                kprintf("Current date: <(0b)>%02d/%02d/%d\n", 
                    dt.day, dt.month, dt.year);
            }
            else if (strcmp(tokens[0], "uptime") == 0) {
                // RTC ticks ׁ ׁ‡׀°ׁׁ‚׀¾ׁ‚׀¾׀¹ 2 ׀“ׁ† (rate=15)
                uint64_t seconds = rtc_ticks / 2;
                uint64_t minutes = seconds / 60;
                uint64_t hours = minutes / 60;
                seconds %= 60;
                minutes %= 60;
                kprintf("System uptime: <(0b)>%llu<(0f)>h <(0b)>%llu<(0f)>m <(0b)>%llu<(0f)>s (RTC ticks: <(0b)>%llu<(0f)>)\n", 
                    hours, minutes, seconds, rtc_ticks);
            }
            else if (strcmp(tokens[0], "exit") == 0) {
                exit = 1;
                return;
            }
            else if (strcmp(tokens[0], "art") == 0) {
                ascii_art();
            }
<<<<<<< HEAD
            else if (strcmp(tokens[0], "neofetch") == 0) {
                neofetch_run();
            }
=======
>>>>>>> 4c07fbfd79ea6bc2eb1dda3e1ee7f3b3e5fb0d19
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

void ascii_art() {
<<<<<<< HEAD
<<<<<<< HEAD
    kprintf("<(0f)>\n רס‎??????‎סררס‎?‎סררס‎?‎סררס‎??????‎סררס‎???????‎סר<(0b)> רס‎??????‎סר רס‎???????‎סר\n");
    kprintf("<(0f)>רס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎ס<(0b)>רס‎?‎סררס‎?‎סרס‎?‎סר\n");
    kprintf("<(0f)>רס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎ס<(0b)>רס‎?‎סררס‎?‎סרס‎?‎סר\n");
    kprintf("<(0f)>רס‎????????‎סררס‎??????‎סררס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎ס<(0b)>רס‎?‎סררס‎?‎סררס‎??????‎סר\n");
    kprintf("<(0f)>רס‎????????‎סררס‎??????‎סררס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎ס<(0b)>רס‎?‎סררס‎?‎סררס‎??????‎סר\n");
    kprintf("<(0f)>רס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎ס<(0b)>רס‎?‎סררס‎?‎סר      רס‎?‎סר\n");
    kprintf("<(0f)>רס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎ס<(0b)>רס‎?‎סררס‎?‎סר      רס‎?‎סר\n");
    kprintf("<(0f)>רס‎?‎סררס‎?‎סרס‎?‎סררס‎?‎סררס‎??????‎סררס‎?‎סררס‎?‎ס<(0b)>ררס‎??????‎סררס‎???????‎סר\n\n");
=======
=======
>>>>>>> whiterose
    kprintf("<(0f)> \xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0<(0b)> \xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0 \xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0      \xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0      \xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n\n");
<<<<<<< HEAD
>>>>>>> 4c07fbfd79ea6bc2eb1dda3e1ee7f3b3e5fb0d19
=======
>>>>>>> whiterose
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    kclear();
    ascii_art();
    kprint("Initializing kernel...\n");
    sysinfo_init(multiboot_magic, multiboot_info);

    gdt_init();
    idt_init();
    pic_init(); 
    pit_init();

    paging_init();
    heap_init(0, 0);

    pci_init();
    pci_dump_devices();

    thread_init();
    iothread_init();

    ps2_keyboard_init();
    
    // ׀˜׀½׀¸ׁ†׀¸׀°׀»׀¸׀·׀¸ׁ€ׁƒ׀µ׀¼ RTC ׀¿׀µׁ€׀µ׀´ ׀²׀÷׀»ׁׁ‡׀µ׀½׀¸׀µ׀¼ ׀¿ׁ€׀µׁ€ׁ‹׀²׀°׀½׀¸׀¹
    rtc_init();
    
    asm volatile("sti");

    kprintf("kernel base: done (idt, gdt, pic, pit, rtc, paging, heap, keyboard)\n");
    
    // ׀׀¾׀÷׀°׀·ׁ‹׀²׀°׀µ׀¼ ׁ‚׀µ׀÷ׁƒׁ‰׀µ׀µ ׀²ׁ€׀µ׀¼ׁ ׀¸׀· RTC
    rtc_datetime_t current_time;
    rtc_read_datetime(&current_time);
    kprintf("Current date and time: %02d/%02d/%d %02d:%02d:%02d\n", 
        current_time.day, current_time.month, current_time.year,
        current_time.hour, current_time.minute, current_time.second);
    
    kprintf("\n<(0f)>Welcome to %s <(0b)>%s<(0f)>!\n", OS_NAME, OS_VERSION);
    kprint("shell: ring0 build-in shell\n");

    ring0_shell();  

    kprint("\nShutting down in 5 seconds...");
    pit_sleep_ms(5000);
    shutdown_system();
    kprintf("Shutdown. If PC is not ACPI turn off power manually");
    for(;;);
}
