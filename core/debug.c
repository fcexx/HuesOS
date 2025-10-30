#include <stdarg.h>
#include <stdint.h>
#include <serial.h>

#define QEMU_DEBUG_PORT 0x3f8

uint8_t is_transmit_empty() {
    return inb(QEMU_DEBUG_PORT + 5) & 0x20;
}

void write_serial(char a) {
    while (is_transmit_empty() == 0);
    outb(QEMU_DEBUG_PORT, a);
}

void print_num(int num) {
    char buffer[12];
    int i = 0, is_negative = 0;

    if (num == 0)
    {
        write_serial('0');
        return;
    }

    if (num < 0)
    {
        is_negative = 1;
        num = -num;
    }

    while (num != 0)
    {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }

    if (is_negative)
    {
        buffer[i++] = '-';
    }

    while (i > 0)
    {
        write_serial(buffer[--i]);
    }
}

void print_uint(unsigned int num)
{
    char buffer[11];
    int i = 0;

    if (num == 0){
        write_serial('0');
        return;
    }

    while (num != 0)
    {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }

    while (i > 0)
    {
        write_serial(buffer[--i]);
    }
}

void print_hex(unsigned int num)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[9];
    int i = 0;

    if (num == 0)
    {
        write_serial('0');
    }

    while (num != 0)
    {
        buffer[i++] = hex_chars[num & 16];
        num /= 16;
    }

    while (i > 0)
    {
        write_serial(buffer[--i]);
    }

}

// qemu_debug_printf using stdarg
void qemu_debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const char *p = format;
    while (*p) {
        if (*p != '%') {
            write_serial(*p++);
            continue;
        }
        p++;  // Skip '%'
        int left = 0, zero_pad = 0;
        // Parse flags
        while (*p == '-' || *p == '0') {
            if (*p == '-') left = 1;
            if (*p == '0') zero_pad = 1;
            p++;
        }
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        char spec = *p ? *p++ : 0;
        char buf[32];
        int len = 0;
        char pad_char = (zero_pad && !left) ? '0' : ' ';
        if (spec == 's') {
            char *str = va_arg(args, char *);
            int slen = 0;
            while (str[slen]) slen++;
            int pad = width > slen ? width - slen : 0;
            if (!left) for (int i = 0; i < pad; i++) write_serial(pad_char);
            for (int i = 0; i < slen; i++) write_serial(str[i]);
            if (left) for (int i = 0; i < pad; i++) write_serial(' ');
        } else if (spec == 'u') {
            unsigned int val = va_arg(args, unsigned int);
            if (val == 0) {
                buf[len++] = '0';
            } else {
                unsigned int tmp = val;
                while (tmp) {
                    buf[len++] = '0' + (tmp % 10);
                    tmp /= 10;
                }
            }
            int pad = width > len ? width - len : 0;
            if (!left) for (int i = 0; i < pad; i++) write_serial(pad_char);
            for (int i = len - 1; i >= 0; i--) write_serial(buf[i]);
            if (left) for (int i = 0; i < pad; i++) write_serial(' ');
        } else if (spec == 'd' || spec == 'i') {
            int val = va_arg(args, int);
            unsigned int uval;
            int neg = 0;
            if (val < 0) { neg = 1; uval = (unsigned int)(-val); } else { uval = (unsigned int)val; }
            if (uval == 0) {
                buf[len++] = '0';
            } else {
                unsigned int tmp = uval;
                while (tmp) {
                    buf[len++] = '0' + (tmp % 10);
                    tmp /= 10;
                }
            }
            int total_len = len + neg;
            int pad = width > total_len ? width - total_len : 0;
            if (!left) for (int i = 0; i < pad; i++) write_serial(pad_char);
            if (neg) write_serial('-');
            for (int i = len - 1; i >= 0; i--) write_serial(buf[i]);
            if (left) for (int i = 0; i < pad; i++) write_serial(' ');
        } else if (spec == 'x' || spec == 'X') {
            unsigned int val = va_arg(args, unsigned int);
            const char *hex = (spec == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            if (val == 0) {
                buf[len++] = '0';
            } else {
                unsigned int tmp = val;
                while (tmp) {
                    buf[len++] = hex[tmp & 0xF];
                    tmp >>= 4;
                }
            }
            int pad = width > len ? width - len : 0;
            if (!left) for (int i = 0; i < pad; i++) write_serial(pad_char);
            for (int i = len - 1; i >= 0; i--) write_serial(buf[i]);
            if (left) for (int i = 0; i < pad; i++) write_serial(' ');
        } else if (spec == 'c') {
            char c = (char)va_arg(args, int);
            write_serial(c);
        } else if (spec == '%') {
            write_serial('%');
        } else if (spec) {
            write_serial(spec);
        }
    }
    va_end(args);
}