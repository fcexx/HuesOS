#include <stdint.h>
#include <serial.h>
#include <vga.h>
#include <string.h>

void	kprint(uint8_t *str)
{
	while (*str)
	{
		kputchar(*str, GRAY_ON_BLACK);
		str++;
	}
}

void	kputchar(uint8_t character, uint8_t attribute_byte)
{
	uint16_t offset;

	offset = get_cursor();
	if (character == '\n')
	{
		if ((offset / 2 / MAX_COLS) == (MAX_ROWS - 1)) 
			scroll_line();
		else
			set_cursor((offset - offset % (MAX_COLS*2)) + MAX_COLS*2);
	}
	else if (character == '\b')
    {
        set_cursor(get_cursor() - 1);
        kputchar(' ', attribute_byte);
        set_cursor(get_cursor() - 2);
    }
	else 
	{
		if (offset == (MAX_COLS * MAX_ROWS * 2)) scroll_line();
		write(character, attribute_byte, offset);
		set_cursor(offset+2);
	}
}

void	scroll_line()
{
	uint8_t i = 1;
	uint16_t last_line;

	while (i < MAX_ROWS)
	{
		memcpy(
			(uint8_t *)(VIDEO_ADDRESS + (MAX_COLS * (i-1) * 2)), /* src */
			(uint8_t *)(VIDEO_ADDRESS + (MAX_COLS * i * 2)),     /* dst */
			(MAX_COLS*2)
		);
		i++;
	}

	last_line = (MAX_COLS*MAX_ROWS*2) - MAX_COLS*2;
	i = 0;
	while (i < MAX_COLS)
	{
		write('\0', WHITE_ON_BLACK, (last_line + i * 2));
		i++;
	}
	set_cursor(last_line);
}

void	kclear()
{
	uint16_t	offset = 0;
	while (offset < (MAX_ROWS * MAX_COLS * 2))
	{
		write('\0', WHITE_ON_BLACK, offset);
		offset += 2;
	}
	set_cursor(0);
}

void	write(uint8_t character, uint8_t attribute_byte, uint16_t offset)
{
	uint8_t *vga = (uint8_t *) VIDEO_ADDRESS;
	vga[offset] = character;
	vga[offset + 1] = attribute_byte;
}

uint16_t		get_cursor()
{
	outb(REG_SCREEN_CTRL, 14);
	uint8_t high_byte = inb(REG_SCREEN_DATA);
	outb(REG_SCREEN_CTRL, 15);
	uint8_t low_byte = inb(REG_SCREEN_DATA);
	return (((high_byte << 8) + low_byte) * 2);
}

void	set_cursor(uint16_t pos)
{
	pos /= 2;
	outb(REG_SCREEN_CTRL, 14);
	outb(REG_SCREEN_DATA, (uint8_t)(pos >> 8));
	outb(REG_SCREEN_CTRL, 15);
	outb(REG_SCREEN_DATA, (uint8_t)(pos & 0xff));
}

// Получить текущую позицию курсора по X
uint16_t get_cursor_x() {
    uint16_t offset = get_cursor();
    return offset % (MAX_COLS * 2);
}

// Получить текущую позицию курсора по Y
uint16_t get_cursor_y() {
    uint16_t offset = get_cursor();
    return offset / (MAX_COLS * 2);
}

// Установить позицию курсора по X
void set_cursor_x(uint16_t x) {
    uint16_t offset = get_cursor();
    uint16_t new_offset = (offset / (MAX_COLS * 2)) * (MAX_COLS * 2) + x * 2;
    set_cursor(new_offset);
}

// Установить позицию курсора по Y
void set_cursor_y(uint16_t y) {
    uint16_t offset = get_cursor();
    uint16_t new_offset = (y * MAX_COLS * 2) + (offset % (MAX_COLS * 2));
    set_cursor(new_offset);
}

void hex_to_str(uint32_t num, char *str);
void hex_to_str(uint32_t num, char *str) {
    int i = 0;
    
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    while (num != 0) {
        uint32_t rem = num % 16;
        if (rem < 10) {
            str[i++] = rem + '0';
        } else {
            str[i++] = (rem - 10) + 'A';
        }
        num = num / 16;
    }

    str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

uint8_t parse_color_code(char bg, char fg);
uint8_t parse_color_code(char bg, char fg) {
    uint8_t background = 0;
    uint8_t foreground = 0;
    
    // Преобразование шестнадцатеричного символа в число
    if (bg >= '0' && bg <= '9') {
        background = bg - '0';
    } else if (bg >= 'a' && bg <= 'f') {
        background = bg - 'a' + 0xa;
    } else if (bg >= 'A' && bg <= 'F') {
        background = bg - 'A' + 0xa;
    }

    if (fg >= '0' && fg <= '9') {
        foreground = fg - '0';
    } else if (fg >= 'a' && fg <= 'f') {
        foreground = fg - 'a' + 0xa;
    } else if (fg >= 'A' && fg <= 'F') {
        foreground = fg - 'A' + 0xa;
    }
    
    return (background << 4) | foreground;
}

void ftos(double n, char *buf, int precision) {
    int i = 0;
    int sign = 1;
    if (n < 0) {
        sign = -1;
        n = -n;
    }

    double integer_part = (int)n;
    double fractional_part = n - integer_part;

    // Вывод целой части
    while (integer_part > 0) {
        buf[i++] = ((int)integer_part % 10) + '0';
        integer_part /= 10;
    }

    // Вывод точки
    buf[i++] = '.';

    // Вывод дробной части
    for (int j = 0; j < precision; j++) {
        fractional_part *= 10;
        buf[i++] = (int)fractional_part + '0';
        fractional_part -= (int)fractional_part;
    }

    // Добавление знака
    if (sign == -1) {
        buf[i++] = '-';
    }

    // Обратная запись строки
    for (int j = 0; j < i / 2; j++) {
        char temp = buf[j];
        buf[j] = buf[i - j - 1];
        buf[i - j - 1] = temp;
    }

    buf[i] = '\0';
}

void kprintf(const char* format, ...) {
    char c;
    uint8_t current_color = 0x0f; // По умолчанию светло-серый текст на черном фоне
    char **arg = (char **) &format;
    int *int_arg;
    char *str_arg;
    char num_buf[32];
    unsigned int uint_arg;
    double double_arg;

    arg++; // Переходим к первому аргументу после format

    while ((c = *format++) != 0) {
        if (c == '<' && *format == '(') {
            format++; // Пропускаем '('

            // Читаем два символа цветового кода
            char bg_color = *format++;
            char fg_color = *format++;

            if (*format == ')' && *(format + 1) == '>') {
                current_color = parse_color_code(bg_color, fg_color);
                format += 2; // Пропускаем ')>'
                continue;
            }
        }

        if (c != '%') {
            kputchar(c, current_color);
            continue;
        }

        c = *format++;
        int width = 0; // Инициализируем ширину
        int precision = -1; // По умолчанию precision не задан

        // Обработка ширины
        while (c >= '0' && c <= '9') {
            width = width * 10 + (c - '0'); // Собираем число
            c = *format++;
        }
        // Обработка precision
        if (c == '.') {
            c = *format++;
            precision = 0;
            while (c >= '0' && c <= '9') {
                precision = precision * 10 + (c - '0');
                c = *format++;
            }
        }

        switch (c) {
            case 'd':
                int_arg = (int *)arg++;
                intToString(*int_arg, num_buf);
                for (char *ptr = num_buf; *ptr; ptr++) {
                    kputchar(*ptr, current_color);
                }
                break;

            case 'u':
                uint_arg = *(unsigned int *)arg++;
                intToString(uint_arg, num_buf);
                for (char *ptr = num_buf; *ptr; ptr++) {
                    kputchar(*ptr, current_color);
                }
                break;

            case 'x': {
                int_arg = (int *)arg++;
                hex_to_str(*int_arg, num_buf);
                int len = strlen(num_buf);
                // Добавляем пробелы для выравнивания
                for (int i = 0; i < width - len; i++) {
                    kputchar('0', current_color); // Заполняем нулями
                }
                for (char *ptr = num_buf; *ptr; ptr++) {
                    kputchar(*ptr, current_color);
                }
                break;
            }

            case 'X': {
                int_arg = (int *)arg++;
                hex_to_str(*int_arg, num_buf);
                int len = strlen(num_buf);
                // Добавляем пробелы для выравнивания
                for (int i = 0; i < width - len; i++) {
                    kputchar('0', current_color); // Заполняем нулями
                }
                for (char *ptr = num_buf; *ptr; ptr++) {
                    kputchar(*ptr, current_color);
                }
                break;
            }

            case 'c':
                int_arg = (int *)arg++;
                kputchar((char)(*int_arg), current_color);
                break;

            case 's': {
                str_arg = *(char **)arg++;
                int slen = strlen(str_arg);
                int to_print = slen;
                if (precision >= 0 && precision < slen) to_print = precision;
                for (int i = 0; i < to_print; i++) {
                    kputchar(str_arg[i], current_color);
                }
                break;
            }

            case 'f': {
                double_arg = *(double *)arg++;
                char float_buf[32];
                ftos(double_arg, float_buf, 2); // Вывод с двумя знаками после запятой
                for (char *ptr = float_buf; *ptr; ptr++) {
                    kputchar(*ptr, current_color);
                }
                break;
            }

            default:
                kputchar(c, current_color);
                break;
        }
    }
}