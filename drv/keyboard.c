#include <keyboard.h>
#include <idt.h>
#include <vga.h>
#include <spinlock.h>
#include <stdint.h>
#include <serial.h>
#include <string.h>

// Размер буфера клавиатуры
#define KEYBOARD_BUFFER_SIZE 256

// Буфер для хранения символов
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static volatile int buffer_count = 0;

// Спинлок для синхронизации доступа к буферу
static spinlock_t keyboard_lock = {0};

// Таблица сканкодов для преобразования в ASCII
static const char scancode_to_ascii[128] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Таблица сканкодов для Shift
static const char scancode_to_ascii_shift[128] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', 0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Специальные коды для стрелок и других клавиш
#define KEY_UP         0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END        0x85
#define KEY_PGUP   0x86
#define KEY_PGDN   0x87
#define KEY_INSERT 0x88
#define KEY_DELETE 0x89
#define KEY_TAB         0x8A

// Флаги состояния клавиатуры
static volatile bool shift_pressed = false;
static volatile bool ctrl_pressed = false;
static volatile bool alt_pressed = false;

// Добавить символ в буфер
static void add_to_buffer(char c) {
        acquire(&keyboard_lock);
        if (buffer_count < KEYBOARD_BUFFER_SIZE) {
                keyboard_buffer[buffer_tail] = c;
                buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
                buffer_count++;
                qemu_debug_printf("BUFFER: added '%c', count=%d\n", c, buffer_count);
        } 
        // else {
        //         qemu_debug_printf("BUFFER: full, dropped '%c'\n", c);
        // }
        release(&keyboard_lock);
}

// Получить символ из буфера
static char get_from_buffer() {
        char c = 0;
        acquire(&keyboard_lock);
        
        if (buffer_count > 0) {
                c = keyboard_buffer[buffer_head];
                buffer_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
                buffer_count--;
        }
        
        release(&keyboard_lock);
        
        return c;
}

// Обработчик прерывания клавиатуры
void keyboard_handler(cpu_registers_t* regs) {
        uint8_t scancode = inb(0x60);
        
        // Обрабатываем только нажатие клавиш (не отпускание)
        if (scancode & 0x80) {
                // Клавиша отпущена
                scancode &= 0x7F; // Убираем бит отпускания
                
                switch (scancode) {
                        case 0x2A: // Left Shift
                        case 0x36: // Right Shift
                                shift_pressed = false;
                                break;
                        case 0x1D: // Left Ctrl
                        case 0x38: // Right Ctrl / Left Alt (same scancode)
                                ctrl_pressed = false;
                                alt_pressed = false;
                                break;
                }
        } else {
                // Клавиша нажата
                
                switch (scancode) {
                        case 0x2A: // Left Shift
                        case 0x36: // Right Shift
                                shift_pressed = true;
                                break;
                        case 0x1D: // Left Ctrl
                                ctrl_pressed = true;
                                break;
                        case 0x38: // Right Ctrl / Left Alt (same scancode)
                                // Определяем по контексту или дополнительным флагам
                                // Пока обрабатываем как Alt
                                alt_pressed = true;
                                break;
                        case 0x48: // Up arrow
                                add_to_buffer(KEY_UP);
                                break;
                        case 0x50: // Down arrow
                                add_to_buffer(KEY_DOWN);
                                break;
                        case 0x4B: // Left arrow
                                add_to_buffer(KEY_LEFT);
                                break;
                        case 0x4D: // Right arrow
                                add_to_buffer(KEY_RIGHT);
                                break;
                        case 0x47: // Home
                                add_to_buffer(KEY_HOME);
                                break;
                        case 0x4F: // End
                                add_to_buffer(KEY_END);
                                break;
                        case 0x49: // Page Up
                                add_to_buffer(KEY_PGUP);
                                break;
                        case 0x51: // Page Down
                                add_to_buffer(KEY_PGDN);
                                break;
                        case 0x52: // Insert
                                add_to_buffer(KEY_INSERT);
                                break;
                        case 0x53: // Delete
                                add_to_buffer(KEY_DELETE);
                                break;
                        case 0x0F: // Tab
                                add_to_buffer(KEY_TAB);
                                break;
                        default:
                                // Обычная клавиша
                                if (scancode < 128) {
                                        char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
                                        if (c != 0) {
                                                add_to_buffer(c);
                                        }
                                }
                                break;
                }
        }
        // EOI отправляется центральным диспетчером прерываний в isr_dispatch
}

// Инициализация PS/2 клавиатуры
void ps2_keyboard_init() {
        // Инициализируем спинлок
        keyboard_lock = (spinlock_t){0};
        
        // Очищаем буфер
        buffer_head = 0;
        buffer_tail = 0;
        buffer_count = 0;
        
        // Сбрасываем флаги
        shift_pressed = false;
        ctrl_pressed = false;
        alt_pressed = false;
        
        // Устанавливаем обработчик прерывания
        idt_set_handler(33, keyboard_handler);
}

// Получить символ (блокирующая функция, как в Unix)
char kgetc() {
        // Простая проверка - если нет символов, возвращаем 0
        if (buffer_count == 0) {
                return 0;
        }
        
        return get_from_buffer();
}

// Проверить, есть ли доступные символы (неблокирующая)
int kgetc_available() {
        return buffer_count;
}

// Убрана локальная реализация автодополнения — используется глобальная в sys_read

// Получить строку с поддержкой стрелок и редактирования
char* kgets(char* buffer, int max_length) {
        if (!buffer || max_length <= 0) {
                return NULL;
        }
        
        int buffer_pos = 0;
        int cursor_pos = 0;
        memset(buffer, 0, max_length);

        uint32_t start_x = 0, start_y = 0; vga_get_cursor(&start_x, &start_y);
        
        vga_set_cursor(start_x, start_y);
        
        while (1) {
                char c = kgetc();
                // qemu_debug_printf("kgets got char: %d\n", c);
                
                if (c == 0) {
                        continue;
                }
                
                if (c == '\n') {
                        // VGA hw cursor: nothing to erase; we'll rewrite line
                        buffer[buffer_pos] = '\0';
                        kprint("\n");
                        return buffer;
                }
                
                // Скрываем курсор перед любым изменением
                // VGA hw cursor: nothing to erase
                
                if ((c == '\b' || c == 127) && cursor_pos > 0) {
                        // Backspace
                        for (int i = cursor_pos - 1; i < buffer_pos; i++) {
                                buffer[i] = buffer[i + 1];
                        }
                        buffer_pos--;
                        cursor_pos--;
                } else if (c == (char)KEY_LEFT && cursor_pos > 0) {
                        cursor_pos--;
                } else if (c == (char)KEY_RIGHT && cursor_pos < buffer_pos) {
                        cursor_pos++;
                } else if (c == (char)KEY_HOME && cursor_pos > 0) {
                        cursor_pos = 0;
                } else if (c == (char)KEY_END && cursor_pos < buffer_pos) {
                        cursor_pos = buffer_pos;
                } else if (c == (char)KEY_DELETE && cursor_pos < buffer_pos) {
                        for (int i = cursor_pos; i < buffer_pos - 1; i++) {
                                buffer[i] = buffer[i + 1];
                        }
                        buffer_pos--;
                } else if (c == (char)KEY_TAB) {
                        // Простая вставка пробела при Tab в kgets (автодополнение выполняется в sys_read для шелла)
                        if (buffer_pos < max_length - 1) {
                                for (int i = buffer_pos; i > cursor_pos; i--) {
                                        buffer[i] = buffer[i - 1];
                                }
                                buffer[cursor_pos] = ' ';
                                buffer_pos++;
                                cursor_pos++;
                        }
                } else if (c >= 32 && c < 127 && buffer_pos < max_length - 1) {
                        // Вставка символа
                        for (int i = buffer_pos; i > cursor_pos; i--) {
                                buffer[i] = buffer[i - 1];
                        }
                        buffer[cursor_pos] = c;
                        buffer_pos++;
                        cursor_pos++;
                }
                
                // Всегда перерисовываем всю строку заново
                // 1. Очищаем всю строку от промпта до конца
                vga_set_cursor(start_x, start_y);
                
                for (int i = 0; i < buffer_pos + 10; i++) { // Очищаем с запасом
                kprint(" ");
                }
                
                // 2. Перерисовываем строку с начала
                vga_set_cursor(start_x, start_y);
                for (int i = 0; i < buffer_pos; i++) {
                        kputchar((uint8_t)buffer[i], GRAY_ON_BLACK);
                }
                
                // 3. Устанавливаем курсор в правильную позицию
                vga_set_cursor(start_x + (uint32_t)cursor_pos, start_y);
        }
        
        return buffer;
}