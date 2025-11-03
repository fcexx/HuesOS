#include "../inc/clock.h"
#include "../inc/vga.h"
#include "../inc/keyboard.h"
#include "../inc/pit.h"
#include "../inc/rtc.h"
#include <stdint.h>
#include <string.h>

#define KEY_UP         0x80
#define KEY_DOWN       0x81
#define KEY_LEFT       0x82
#define KEY_RIGHT      0x83

// Screen area
#define CLOCK_MIN_X 1
#define CLOCK_MIN_Y 1
#define CLOCK_MAX_X (MAX_COLS - 2)
#define CLOCK_MAX_Y (MAX_ROWS - 2)

// Clock parameters
#define CLOCK_CENTER_X (MAX_COLS / 2)
#define CLOCK_CENTER_Y (MAX_ROWS / 2)
#define CLOCK_RADIUS 10
// --- ИСПРАВЛЕНО: Коэффициент для компенсации непропорциональности символов ---
#define ASPECT_RATIO 1.77

typedef struct Point { int x; int y; } Point;

// --- Вспомогательные цвета для наглядности ---
#define COLOR_BACKGROUND 0x01 // Синий фон
#define COLOR_BORDER     0x22 // Зеленый на зеленом для рамки
#define COLOR_TEXT       WHITE_ON_BLACK
#define COLOR_FACE       WHITE_ON_BLACK
#define COLOR_NUMBERS    0x0B // Светло-голубой
#define COLOR_CENTER     0x0E // Желтый
#define COLOR_HOUR       0x0E // Желтый
#define COLOR_MINUTE     0x0A // Зеленый
#define COLOR_SECOND     0x0C // Красный
#define COLOR_DIGITAL_FG 0x0F // Ярко-белый
#define COLOR_DIGITAL_BG 0x07 // Серый

static uint16_t cell_offset(uint8_t x, uint8_t y) {
    return (uint16_t)((y * MAX_COLS + x) * 2);
}

static void draw_cell(uint8_t x, uint8_t y, uint8_t ch, uint8_t color) {
    if (x < MAX_COLS && y < MAX_ROWS) {
        write(ch, color, cell_offset(x, y));
    }
}

static void draw_text(uint8_t x, uint8_t y, const char* s, uint8_t color) {
    for (uint8_t i = 0; s[i]; i++) draw_cell(x + i, y, (uint8_t)s[i], color);
}

static void draw_border(void) {
    for (uint8_t x = 0; x < MAX_COLS; x++) {
        draw_cell(x, 0, ' ', COLOR_BORDER);
        draw_cell(x, MAX_ROWS - 1, ' ', COLOR_BORDER);
    }
    for (uint8_t y = 0; y < MAX_ROWS; y++) {
        draw_cell(0, y, ' ', COLOR_BORDER);
        draw_cell(MAX_COLS - 1, y, ' ', COLOR_BORDER);
    }
}

static int sin_table[60] = {
      0,   27,   53,   79,  104,  128,  150,  171,  190,  207,  222,  234,  243,  250,  255,
    256,  255,  250,  243,  234,  222,  207,  190,  171,  150,  128,  104,   79,   53,   27,
      0,  -27,  -53,  -79, -104, -128, -150, -171, -190, -207, -222, -234, -243, -250, -255,
   -256, -255, -250, -243, -234, -222, -207, -190, -171, -150, -128, -104,  -79,  -53,  -27
};

static int cos_table[60] = {
    256,  255,  250,  243,  234,  222,  207,  190,  171,  150,  128,  104,   79,   53,   27,
      0,  -27,  -53,  -79, -104, -128, -150, -171, -190, -207, -222, -234, -243, -250, -255,
   -256, -255, -250, -243, -234, -222, -207, -190, -171, -150, -128, -104,  -79,  -53,  -27,
      0,   27,   53,   79,  104,  128,  150,  171,  190,  207,  222,  234,  243,  250,  255
};


// --- ИСПРАВЛЕНО: Функция учитывает ASPECT_RATIO ---
static Point get_hand_point(int value, int length) {
    Point p;
    // Умножаем смещение по X на ASPECT_RATIO для коррекции
    p.x = CLOCK_CENTER_X + (sin_table[value] * length * ASPECT_RATIO) / 256;
    p.y = CLOCK_CENTER_Y - (cos_table[value] * length) / 256;
    return p;
}

static void draw_line(int x0, int y0, int x1, int y1, uint8_t ch, uint8_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        if (x0 >= CLOCK_MIN_X && x0 <= CLOCK_MAX_X && y0 >= CLOCK_MIN_Y && y0 <= CLOCK_MAX_Y) {
            draw_cell((uint8_t)x0, (uint8_t)y0, ch, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

// --- ИСПРАВЛЕНО: Функция учитывает ASPECT_RATIO ---
static void draw_circle(int cx, int cy, int r, uint8_t ch, uint8_t color) {
    for (int angle = 0; angle < 60; angle++) {
        // Умножаем смещение по X на ASPECT_RATIO для коррекции
        int x = cx + (sin_table[angle] * r * ASPECT_RATIO) / 256;
        int y = cy - (cos_table[angle] * r) / 256;
        if (x >= CLOCK_MIN_X && x <= CLOCK_MAX_X && y >= CLOCK_MIN_Y && y <= CLOCK_MAX_Y) {
            draw_cell((uint8_t)x, (uint8_t)y, ch, color);
        }
    }
}

static void draw_clock_face(void) {
    draw_circle(CLOCK_CENTER_X, CLOCK_CENTER_Y, CLOCK_RADIUS, '.', COLOR_FACE);
    
    Point p;
    p = get_hand_point(0, CLOCK_RADIUS);  draw_text(p.x - 1, p.y, "12", COLOR_NUMBERS);
    p = get_hand_point(15, CLOCK_RADIUS); draw_text(p.x, p.y, "3", COLOR_NUMBERS);
    p = get_hand_point(30, CLOCK_RADIUS); draw_text(p.x, p.y, "6", COLOR_NUMBERS);
    p = get_hand_point(45, CLOCK_RADIUS); draw_text(p.x - 1, p.y, "9", COLOR_NUMBERS);
    
    draw_cell(CLOCK_CENTER_X, CLOCK_CENTER_Y, '+', COLOR_CENTER);
}

static void draw_digital_time(rtc_datetime_t *dt) {
    char buf[12];
    
    // Format time HH:MM:SS
    buf[0] = '0' + (dt->hour / 10); buf[1] = '0' + (dt->hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (dt->minute / 10); buf[4] = '0' + (dt->minute % 10);
    buf[5] = ':';
    buf[6] = '0' + (dt->second / 10); buf[7] = '0' + (dt->second % 10);
    buf[8] = '\0';
    draw_text(2, 2, "          ", 0); // Clear old text
    draw_text(2, 2, buf, COLOR_DIGITAL_FG);
    
    // Format date DD/MM/YYYY
    int year = dt->year;
    buf[0] = '0' + (dt->day / 10); buf[1] = '0' + (dt->day % 10);
    buf[2] = '/';
    buf[3] = '0' + (dt->month / 10); buf[4] = '0' + (dt->month % 10);
    buf[5] = '/';
    buf[9] = '0' + (year % 10); year /= 10;
    buf[8] = '0' + (year % 10); year /= 10;
    buf[7] = '0' + (year % 10); year /= 10;
    buf[6] = '0' + (year % 10);
    buf[10] = '\0';
    draw_text(2, 3, "          ", 0); // Clear old text
    draw_text(2, 3, buf, COLOR_DIGITAL_BG);
}


// --- ИСПРАВЛЕНО: Полностью переработанная основная функция ---
void clock_run(void) {
    kclear_col(COLOR_BACKGROUND);
    
    // --- Шаг 1: Рисуем все статичные элементы ОДИН РАЗ ---
    draw_border();
    const char* title = "ANALOG CLOCK";
    uint8_t title_x = (MAX_COLS - (uint8_t)strlen(title)) / 2;
    draw_text(title_x, 1, title, COLOR_TEXT);
    draw_text(2, MAX_ROWS - 2, "Press Q or ESC to exit", GRAY_ON_BLACK);
    draw_clock_face();

    int running = 1;
    rtc_datetime_t last_time;
    memset(&last_time, 0, sizeof(last_time)); // Инициализируем, чтобы первая отрисовка сработала

    while (running) {
        char c = 0;
        if (kgetc_available()) c = kgetc();
        
        if (c == 'q' || c == 'Q' || c == 27) {
            running = 0;
            break;
        }
        
        rtc_datetime_t current_time;
        rtc_read_datetime(&current_time);
        
        // Перерисовываем только если время изменилось
        if (current_time.second != last_time.second) {
            
            // --- Шаг 2: Стираем старые стрелки ---
            // Стираем старую секундную стрелку
            Point sec_end_old = get_hand_point(last_time.second, 9);
            draw_line(CLOCK_CENTER_X, CLOCK_CENTER_Y, sec_end_old.x, sec_end_old.y, ' ', COLOR_BACKGROUND);

            // Минутную и часовую стираем и перерисовываем, только если они изменились
            if (current_time.minute != last_time.minute) {
                Point min_end_old = get_hand_point(last_time.minute, 8);
                draw_line(CLOCK_CENTER_X, CLOCK_CENTER_Y, min_end_old.x, min_end_old.y, ' ', COLOR_BACKGROUND);

                int hour_pos_old = ((last_time.hour % 12) * 5) + (last_time.minute / 12);
                Point hour_end_old = get_hand_point(hour_pos_old, 5);
                draw_line(CLOCK_CENTER_X, CLOCK_CENTER_Y, hour_end_old.x, hour_end_old.y, ' ', COLOR_BACKGROUND);
            }

            // --- Шаг 3: Рисуем новые стрелки ---
            int hour_12 = current_time.hour % 12;
            int hour_pos = (hour_12 * 5) + (current_time.minute / 12);
            Point hour_end = get_hand_point(hour_pos, 5);
            draw_line(CLOCK_CENTER_X, CLOCK_CENTER_Y, hour_end.x, hour_end.y, '#', COLOR_HOUR);
            
            Point min_end = get_hand_point(current_time.minute, 8);
            draw_line(CLOCK_CENTER_X, CLOCK_CENTER_Y, min_end.x, min_end.y, '=', COLOR_MINUTE);
            
            Point sec_end = get_hand_point(current_time.second, 9);
            draw_line(CLOCK_CENTER_X, CLOCK_CENTER_Y, sec_end.x, sec_end.y, '-', COLOR_SECOND);

            // --- Шаг 4: Восстанавливаем элементы, которые могли быть затерты ---
            // Цифры циферблата могли быть затерты стрелками, восстановим их
            if (current_time.minute != last_time.minute) { // Делаем это реже для оптимизации
                Point p;
                p = get_hand_point(0, CLOCK_RADIUS);  draw_text(p.x - 1, p.y, "12", COLOR_NUMBERS);
                p = get_hand_point(15, CLOCK_RADIUS); draw_text(p.x, p.y, "3", COLOR_NUMBERS);
                p = get_hand_point(30, CLOCK_RADIUS); draw_text(p.x, p.y, "6", COLOR_NUMBERS);
                p = get_hand_point(45, CLOCK_RADIUS); draw_text(p.x - 1, p.y, "9", COLOR_NUMBERS);
            }
            // Центр затирается всегда, восстанавливаем его
            draw_cell(CLOCK_CENTER_X, CLOCK_CENTER_Y, '+', COLOR_CENTER);
            
            // Обновляем цифровое время
            draw_digital_time(&current_time);
            
            last_time = current_time;
        }
        
        pit_sleep_ms(50); // Небольшая задержка для экономии ресурсов
    }
    
    kclear();
}