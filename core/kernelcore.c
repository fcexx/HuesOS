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
#include <axosh.h>
#include <iothread.h>
#include <fs.h>
#include <ext2.h>
#include <ramfs.h>
#include <editor.h>
#include <intel_chipset.h>
#include <mmio.h>
#include <e1000.h>
#include <net.h>
#include <tls.h>
#include <net_lwip.h>

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

void ring0_shell()  { osh_run(); }

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

void kb_null() {
    (void)inb(0x60);
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    kclear();
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
    intel_chipset_init();

    thread_init();
    iothread_init();
    /* Регистрируем файловую систему */
    ramfs_register();
    ext2_register();
    
    ps2_keyboard_init();
    //idt_set_handler(33, kb_null);
    rtc_init();
    
    asm volatile("sti");

    /* Initialize Intel E1000 NIC (if present) */
    if (e1000_init() == 0) {
        uint8_t mac[6];
        if (e1000_get_mac(mac) == 0) {
            kprintf("e1000: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        /* Bring up minimal L2/L3 and try ping to host (configure tap0 accordingly) */
        uint32_t my_ip = ip4_addr(10,0,2,15);
		uint32_t gw_ip = ip4_addr(10,0,2,2);
        net_init(my_ip, gw_ip);
		/* Re-enable lwIP stack (NO_SYS) with static /24 */
		lwip_stack_init(my_ip, ip4_addr(255,255,255,0), gw_ip);
        /* HTTPS (stub/proxy or real TLS): получить страницу и сохранить тело в ramfs */
        /* Stream directly to file with soft-wrap at 78 cols (faster, no big buffers) */
		kprintf("https: start github.com /\n");
		int tr = https_get_to_file("github.com", "/", "/test", 8000, 78);
		kprintf("https: done rc=%d -> /test\n", tr);
    } else {
        kprintf("e1000: init failed or NIC not found\n");
    }

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
    ascii_art();
    
    kprintf("\n<(0f)>Welcome to %s <(0b)>%s<(0f)>!\n", OS_NAME, OS_VERSION);
    // autostart: run /start script once if present
    {
        struct fs_file *f = fs_open("/start");
        if (f) { fs_file_free(f); (void)exec_line("osh /start"); }
    }

    kprint("\nShutting down in 5 seconds...");
    pit_sleep_ms(5000);
    shutdown_system();
    kprintf("Shutdown. If PC is not ACPI turn off power manually");
    for(;;);
}
