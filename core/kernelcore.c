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
#include <fs.h>
#include <ext2.h>
#include <ramfs.h>
#include <editor.h>

int exit = 0;

static char g_cwd[256] = "/";

static void resolve_path(const char *cwd, const char *arg, char *out, size_t outlen) {
    if (!arg || arg[0] == '\0') {
        /* return cwd */
        strncpy(out, cwd, outlen-1);
        out[outlen-1] = '\0';
        return;
    }
    if (arg[0] == '/') {
        strncpy(out, arg, outlen-1);
        out[outlen-1] = '\0';
        return;
    }
    /* remove leading ./ if present */
    const char *p = arg;
    if (p[0] == '.' && p[1] == '/') p += 2;
    /* build absolute path into tmp then normalize */
    char tmp[512];
    if (arg[0] == '\0') {
        strncpy(tmp, cwd, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';
    } else if (arg[0] == '/') {
        strncpy(tmp, arg, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';
    } else {
        if (strcmp(cwd, "/") == 0) {
            tmp[0] = '/';
            size_t n = strlen(p);
            if (n > sizeof(tmp) - 2) n = sizeof(tmp) - 2;
            if (n) memcpy(tmp + 1, p, n);
            tmp[1 + n] = '\0';
        } else {
            size_t a = strlen(cwd);
            if (a > sizeof(tmp) - 2) a = sizeof(tmp) - 2;
            memcpy(tmp, cwd, a);
            tmp[a] = '/';
            size_t n = strlen(p);
            if (n > sizeof(tmp) - a - 2) n = sizeof(tmp) - a - 2;
            if (n) memcpy(tmp + a + 1, p, n);
            tmp[a + 1 + n] = '\0';
        }
    }
    /* normalize tmp into out (handle "." and "..") */
    /* algorithm: split by '/', push segments, pop on '..' */
    char **parts = (char**)kmalloc(128);
    if (!parts) return;
    int pc = 0;
    char *s = tmp;
    /* ensure leading slash */
    if (*s != '/') {
        /* make absolute by prepending cwd */
        char t2[512]; strncpy(t2, tmp, sizeof(t2)-1); t2[sizeof(t2)-1] = '\0';
        if (strcmp(cwd, "/") == 0) {
            tmp[0] = '/';
            size_t n = strlen(t2);
            if (n > sizeof(tmp) - 2) n = sizeof(tmp) - 2;
            if (n) memcpy(tmp + 1, t2, n);
            tmp[1 + n] = '\0';
        } else {
            size_t a = strlen(cwd);
            if (a > sizeof(tmp) - 2) a = sizeof(tmp) - 2;
            memcpy(tmp, cwd, a);
            tmp[a] = '/';
            size_t n = strlen(t2);
            if (n > sizeof(tmp) - a - 2) n = sizeof(tmp) - a - 2;
            if (n) memcpy(tmp + a + 1, t2, n);
            tmp[a + 1 + n] = '\0';
        }
        s = tmp;
    }
    /* tokenize */
    s++; /* skip leading slash */
    while (*s) {
        char *seg = s;
        while (*s && *s != '/') s++;
        size_t len = (size_t)(s - seg);
        if (len == 0) { if (*s) s++; continue; }
        char save = seg[len];
        seg[len] = '\0';
        if (strcmp(seg, ".") == 0) {
            /* ignore */
        } else if (strcmp(seg, "..") == 0) {
            if (pc > 0) pc--; /* pop */
        } else {
            parts[pc++] = seg;
        }
        seg[len] = save;
        if (*s) s++;
    }
    /* build output */
    if (pc == 0) {
        strncpy(out, "/", outlen-1); out[outlen-1] = '\0';
    } else {
        size_t pos = 0;
        for (int i = 0; i < pc; i++) {
            size_t need = strlen(parts[i]) + 1; /* '/' + name */
            if (pos + need >= outlen) break;
            out[pos++] = '/';
            size_t n = strlen(parts[i]);
            memcpy(out + pos, parts[i], n);
            pos += n;
        }
        out[pos] = '\0';
    }
    kfree(parts);
}

static int is_dir_path(const char *path) {
    struct fs_file *f = fs_open(path);
    if (!f) return 0;
    /* If driver explicitly set type, use it */
    if (f->type == FS_TYPE_DIR) {
        fs_file_free(f);
        return 1;
    }
    if (f->type == FS_TYPE_REG) {
        fs_file_free(f);
        return 0;
    }
    /* Fallback: attempt to read directory entries */
    size_t want = f->size ? f->size : 512;
    if (want > 8192) want = 8192;
    void *buf = kmalloc(want + 1);
    int found = 0;
    if (buf) {
        ssize_t r = fs_read(f, buf, want, 0);
        if (r > 0) {
            uint32_t off = 0;
            while ((size_t)off + sizeof(struct ext2_dir_entry) < (size_t)r) {
                struct ext2_dir_entry *de = (struct ext2_dir_entry *)((uint8_t*)buf + off);
                if (de->inode != 0 && de->rec_len > 0 && de->name_len > 0 && de->name_len < 255) { found = 1; break; }
                if (de->rec_len == 0) break;
                off += de->rec_len;
            }
        }
        kfree(buf);
    }
    fs_file_free(f);
    return found;
}

void ring0_shell()  {
    static char input_buf[512];
    char *input;
    // build in shell
    for (;;) {
        kprintf("%s> ", g_cwd);
        input = kgets(input_buf, 512);
        /* handle redirection '>' (simple, single redirect) */
        int redirect = 0;
        char redir_path[256];
        char *rp = strchr(input, '>');
        if (rp) {
            redirect = 1;
            *rp = '\0';
            rp++; /* filename starts here */
            while (*rp == ' ' || *rp == '\t') rp++;
            /* copy filename until whitespace or end */
            size_t i = 0;
            while (*rp && *rp != ' ' && *rp != '\t' && i + 1 < sizeof(redir_path)) redir_path[i++] = *rp++;
            redir_path[i] = '\0';
        }
        int ntok = 0;
        char **tokens = split(input, " ", &ntok);
        if (ntok > 0) { 
            if (strcmp(tokens[0], "help") == 0) {
                kprint("Available commands:\n");
                kprint("help - show available commands\n");
                kprint("clear, cls - clear the screen\n");
                kprint("ls [path] - list directory contents\n");
                kprint("cat <file> - print file contents\n");
                kprint("cd <path> - change directory\n");
                kprint("pwd - print working directory\n");
                kprint("mkdir <dir> - create directory (ramfs)\n");
                kprint("touch <file> - create empty file (ramfs)\n");
                kprint("rm <path> - remove file or directory (ramfs)\n");
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
                // RTC ticks с частотой 2 Гц (rate=15)
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
            else if (strcmp(tokens[0], "neofetch") == 0) {
                neofetch_run();
            }
            else if (strcmp(tokens[0], "ls") == 0) {
                char path[256];
                if (ntok < 2) resolve_path(g_cwd, "", path, sizeof(path));
                else resolve_path(g_cwd, tokens[1], path, sizeof(path));
                struct fs_file *f = fs_open(path);
                if (!f) { kprintf("<(0c)>ls: cannot access %s\n", path); }
                else {
                    size_t want = f->size ? f->size : 4096;
                    void *buf = kmalloc(want + 1);
                    if (buf) {
                        ssize_t r = fs_read(f, buf, want, 0);
                        if (r > 0) {
                            uint32_t off = 0;
                            while ((size_t)off < (size_t)r) {
                                struct ext2_dir_entry *de = (struct ext2_dir_entry *)((uint8_t*)buf + off);
                                if (de->inode == 0) break;
                                int nlen = de->name_len;
                                if (nlen > 255) nlen = 255;
                                char name[256];
                                memcpy(name, (uint8_t*)buf + off + sizeof(*de), nlen);
                                name[nlen] = '\0';
                                const char *suffix = (de->file_type == EXT2_FT_DIR) ? "/" : "";
                                kprintf("%s%s\n", name, suffix);
                                if (de->rec_len == 0) break;
                                off += de->rec_len;
                            }
                        }
                        kfree(buf);
                    }
                    fs_file_free(f);
                }
            }
            else if (strcmp(tokens[0], "cat") == 0) {
                if (ntok < 2) { kprint("cat: missing operand\n"); }
                else {
                    char path[256];
                    resolve_path(g_cwd, tokens[1], path, sizeof(path));
                    struct fs_file *f = fs_open(path);
                    if (!f) { kprintf("<(0c)>cat: %s: No such file\n", path); }
                    else {
                        size_t want = f->size ? f->size : 0;
                        void *buf = kmalloc(want + 1);
                        if (buf) {
                            ssize_t r = fs_read(f, buf, want, 0);
                            if (r > 0) {
                                ((char*)buf)[r] = '\0';
                                kprint((uint8_t*)buf);
                                kprint("\n");
                            }
                            kfree(buf);
                        }
                        fs_file_free(f);
                    }
                }
            }
            else if (strcmp(tokens[0], "echo") == 0) {
                /* echo supports optional redirection: echo text > file */
                /* extract rest of input after 'echo' token (original input was modified by split) */
                const char* p = input;
                while (*p == ' ' || *p == '\t') p++;
                const char* word = "echo";
                while (*p && *word && *p == *word) { p++; word++; }
                while (*p == ' ' || *p == '\t') p++;
                /* p now points to text to echo */
                if (redirect) {
                    char outpath[256];
                    resolve_path(g_cwd, redir_path, outpath, sizeof(outpath));
                    /* strip surrounding quotes if present */
                    const char *start = p;
                    const char *end = start + strlen(start);
                    if (*start == '"' && end > start && *(end-1) == '"') { start++; ((char*)end)[-1] = '\0'; }
                    /* create or open target */
                    struct fs_file *f = fs_open(outpath);
                    if (!f) f = fs_create_file(outpath);
                    if (!f) { kprintf("<(0c)>echo: cannot create %s\n", outpath); }
                    else {
                        ssize_t w = fs_write(f, start, strlen(start), 0);
                        if (w < 0) kprintf("<(0c)>echo: write failed (err %d)\n", (int)w);
                        fs_file_free(f);
                    }
                } else {
                    kprint_colorized(p);
                    kprint("\n");
                }
            }
            else if (strcmp(tokens[0], "mkdir") == 0) {
                if (ntok < 2) { kprint("mkdir: missing operand\n"); }
                else {
                    char path[256];
                    resolve_path(g_cwd, tokens[1], path, sizeof(path));
                    int r = ramfs_mkdir(path);
                    if (r == 0) kprintf("mkdir: created %s\n", path);
                    else kprintf("<(0c)>mkdir: cannot create %s (err %d)\n", path, r);
                }
            }
            else if (strcmp(tokens[0], "touch") == 0) {
                if (ntok < 2) { kprint("touch: missing operand\n"); }
                else {
                    char path[256];
                    resolve_path(g_cwd, tokens[1], path, sizeof(path));
                    struct fs_file *f = fs_create_file(path);
                    if (!f) kprintf("<(0c)>touch: cannot create %s\n", path);
                    else { fs_file_free(f); }
                }
            }
            else if (strcmp(tokens[0], "rm") == 0) {
                if (ntok < 2) { kprint("rm: missing operand\n"); }
                else {
                    char path[256];
                    resolve_path(g_cwd, tokens[1], path, sizeof(path));
                    int r = ramfs_remove(path);
                    if (r == 0) kprintf("rm: removed %s\n", path);
                    else kprintf("<(0c)>rm: cannot remove %s (err %d)\n", path, r);
                }
            }
            else if (strcmp(tokens[0], "pwd") == 0) {
                kprintf("%s\n", g_cwd);
            }
            else if (strcmp(tokens[0], "edit") == 0) {
                char path[256];
                if (ntok < 2) resolve_path(g_cwd, "untitled", path, sizeof(path));
                else resolve_path(g_cwd, tokens[1], path, sizeof(path));
                editor_run(path);
            }
            else if (strcmp(tokens[0], "cd") == 0) {
                if (ntok < 2) { strncpy(g_cwd, "/", sizeof(g_cwd)); }
                else {
                    char path[256];
                    resolve_path(g_cwd, tokens[1], path, sizeof(path));
                    if (is_dir_path(path)) {
                        /* normalize: remove trailing slash unless root */
                        size_t l = strlen(path);
                        if (l > 1 && path[l-1] == '/') path[l-1] = '\0';
                        strncpy(g_cwd, path, sizeof(g_cwd)-1);
                        g_cwd[sizeof(g_cwd)-1] = '\0';
                    } else {
                        kprintf("<(0c)>cd: %s: Not a directory\n", path);
                    }
                }
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

void ascii_art() {
    kprintf("<(0f)> \xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0<(0b)> \xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0 \xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0      \xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0      \xB0\xB1\xB2\xDB\xB2\xB1\xB0\n");
    kprintf("<(0f)>\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xB2\xB1<(0b)>\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\xB0\xB1\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB1\xB0\n\n");
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
    /* Регистрируем файловую систему */
    ramfs_register();
    ext2_register();

    ps2_keyboard_init();
    rtc_init();
    
    asm volatile("sti");

    kprintf("kernel base: done (idt, gdt, pic, pit, pci, rtc, paging, heap, keyboard)\n");

    static const char license_text[] =
"MIT License\n"
"Copyright (c) 2025 The Axon Team\n\n"
"Permission is hereby granted, free of charge, to any person obtaining a copy\n"
"of this software and associated documentation files (the 'Software'), to deal\n"
"in the Software without restriction, including without limitation the rights\n"
"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
"copies of the Software, and to permit persons to whom the Software is\n"
"furnished to do so, subject to the following conditions:\n\n"
"The above copyright notice and this permission notice shall be included in all\n"
"copies or substantial portions of the Software.\n\n"
"THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
"SOFTWARE.\n";
    struct fs_file *license_file = fs_create_file("/LICENSE");
    if (license_file) {
        fs_write(license_file, license_text, strlen(license_text), 0);
        fs_file_free(license_file);
    }
    
    // Показываем текущее время из RTC
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
