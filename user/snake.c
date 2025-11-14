
#include "../inc/snake.h"
#include "../inc/vga.h"
#include "../inc/keyboard.h"
#include "../inc/pit.h"
#include <stdint.h>
#include <string.h>

#define KEY_UP         0x80
#define KEY_DOWN       0x81
#define KEY_LEFT       0x82
#define KEY_RIGHT      0x83

#define GAME_MIN_X 1
#define GAME_MIN_Y 1
#define GAME_MAX_X (MAX_COLS - 2)
#define GAME_MAX_Y (MAX_ROWS - 2)

#define MAX_SNAKE_LEN 200

typedef struct Point { uint8_t x; uint8_t y; } Point;

static void draw_border(void) {
    for (uint8_t x = 0; x < MAX_COLS; x++) {
        draw_cell(x, 0, ' ', 0x22);
        draw_cell(x, MAX_ROWS - 1, ' ', 0x22);
    }
    for (uint8_t y = 0; y < MAX_ROWS; y++) {
        draw_cell(0, y, ' ', 0x22);
        draw_cell(MAX_COLS - 1, y, ' ', 0x22);
    }
}

void kprintf(const char* fmt, ...);

static int best_score = 0;

typedef struct Difficulty { const char* name; uint32_t delay_ms; int start_len; } Difficulty;
static const Difficulty difficulties[] = {
    {"Very Easy", 320, 3},
    {"Easy", 240, 3},
    {"Medium", 180, 4},
    {"Normal", 120, 4},
    {"Hard", 80, 5},
    {"Very Hard", 50, 6},
};
static const int DIFFICULTY_COUNT = sizeof(difficulties)/sizeof(difficulties[0]);

static int show_menu_and_get_choice(void) {
    kclear_col(0x1);
    draw_border();
    const char* title = "SNAKE";
    uint8_t title_x = (MAX_COLS - (int)strlen(title)) / 2;
    uint8_t title_y = 3;
    draw_text(title_x, title_y, title, WHITE_ON_BLACK);

    int sel = 3;
    int running = 1;
    while (running) {
        for (int i = 0; i < DIFFICULTY_COUNT; i++) {
            uint8_t x = (MAX_COLS - 20) / 2;
            uint8_t y = 6 + i * 2;
            if (i == sel) draw_text(x - 2, y, "> ", WHITE_ON_BLACK);
            else draw_text(x - 2, y, "  ", WHITE_ON_BLACK);
            draw_text(x, y, difficulties[i].name, GRAY_ON_BLACK);
        }
        draw_text((MAX_COLS-28)/2, MAX_ROWS-3, "Use arrows to select, Enter to start", GRAY_ON_BLACK);

        char c = 0;
        while (!(c = kgetc())) { asm volatile("hlt"); }
        if ((unsigned char)c == KEY_UP) { if (sel > 0) sel--; }
        else if ((unsigned char)c == KEY_DOWN) { if (sel < DIFFICULTY_COUNT-1) sel++; }
        else if (c == '\n' || c == '\r') { running = 0; }
        else if (c == 27) {
            kclear();
            return -1;
        }
    }
    return sel;
}

static void int_to_str(int v, char *buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int neg = 0; if (v < 0) { neg = 1; v = -v; }
    int i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i++] = '-';
    for (int j = 0; j < i/2; j++) { char t = buf[j]; buf[j] = buf[i-j-1]; buf[i-j-1] = t; }
    buf[i] = '\0';
}

static void show_victory(int score) {
    if (score > best_score) best_score = score;
    kclear_col(0x1);
    draw_border();
    const char *msg1 = "CONGRATULATIONS!";
    const char *msg2 = "YOU WIN!";
    const char *msg3 = "Press any key to continue";
    uint8_t x1 = (MAX_COLS - (int)strlen(msg1)) / 2;
    uint8_t x2 = (MAX_COLS - (int)strlen(msg2)) / 2;
    uint8_t x3 = (MAX_COLS - (int)strlen(msg3)) / 2;
    uint8_t y = MAX_ROWS / 2 - 1;
    draw_text(x1, y-1, msg1, WHITE_ON_BLACK);
    draw_text(x2, y, msg2, WHITE_ON_BLACK);
    draw_text(x3, y+1, msg3, GRAY_ON_BLACK);

    while (!kgetc_available()) { asm volatile("hlt"); }

    (void)kgetc();
}

void snake_run(void) {
    int choice = show_menu_and_get_choice();
    if (choice < 0) return;

    const Difficulty *diff = &difficulties[choice];

    kclear_col(0x01);
    draw_border();

    Point snake[MAX_SNAKE_LEN];
    int snake_len = diff->start_len;
    uint8_t start_x = MAX_COLS / 2;
    uint8_t start_y = MAX_ROWS / 2;
    for (int i = 0; i < snake_len; i++) { snake[i].x = start_x - i; snake[i].y = start_y; }

    Point food;
    uint64_t ticks = pit_get_ticks();
    food.x = (uint8_t)(GAME_MIN_X + (ticks % (GAME_MAX_X - GAME_MIN_X + 1)));
    food.y = (uint8_t)(GAME_MIN_Y + ((ticks / 7) % (GAME_MAX_Y - GAME_MIN_Y + 1)));

    enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } dir = DIR_RIGHT;

    for (int i = 0; i < snake_len; i++) draw_cell(snake[i].x, snake[i].y, (i==0)?'@':'o', WHITE_ON_BLACK);
    draw_cell(food.x, food.y, '*', WHITE_ON_BLACK);

    int running = 1;
    int score = 0;
    int paused = 0;
    int won = 0;

    while (running) {
        /* Non-blocking keyboard check: don't call blocking kgetc() here
         * because that would halt game progression until a key is pressed.
         */
        char c = 0;
        if (kgetc_available()) c = kgetc();

        if (c) {
            if (c == 27) {
                paused = !paused;
                if (paused) draw_text((MAX_COLS-6)/2, (MAX_ROWS/2), "PAUSED", WHITE_ON_BLACK);
                else { for (int i = 0; i < 6; i++) draw_cell((MAX_COLS-6)/2 + i, (MAX_ROWS/2), ' ', WHITE_ON_BLACK); }
            }
            if (paused) { /* ignore other keys while paused */ }
            else {
                if ((unsigned char)c == KEY_UP && dir != DIR_DOWN) dir = DIR_UP;
                else if ((unsigned char)c == KEY_DOWN && dir != DIR_UP) dir = DIR_DOWN;
                else if ((unsigned char)c == KEY_LEFT && dir != DIR_RIGHT) dir = DIR_LEFT;
                else if ((unsigned char)c == KEY_RIGHT && dir != DIR_LEFT) dir = DIR_RIGHT;
                else if (c == 'q' || c == 'Q') { running = 0; break; }
            }
        }

        if (paused) { pit_sleep_ms(100); continue; }

        Point new_head = snake[0];
        if (dir == DIR_UP) { if (new_head.y == GAME_MIN_Y) { running = 0; } else new_head.y--; }
        else if (dir == DIR_DOWN) { if (new_head.y == GAME_MAX_Y) { running = 0; } else new_head.y++; }
        else if (dir == DIR_LEFT) { if (new_head.x == GAME_MIN_X) { running = 0; } else new_head.x--; }
        else if (dir == DIR_RIGHT) { if (new_head.x == GAME_MAX_X) { running = 0; } else new_head.x++; }

        if (!running) break;

        for (int i = 0; i < snake_len; i++) { if (snake[i].x == new_head.x && snake[i].y == new_head.y) { running = 0; break; } }
        if (!running) break;

        int ate = (new_head.x == food.x && new_head.y == food.y);

        if (!ate) {
            draw_cell(snake[snake_len-1].x, snake[snake_len-1].y, ' ', WHITE_ON_BLACK);
        } else {
            if (snake_len < MAX_SNAKE_LEN) snake_len++;
            score++;
            if (score > best_score) best_score = score;
            uint64_t t2 = pit_get_ticks();
            food.x = (uint8_t)(GAME_MIN_X + (t2 % (GAME_MAX_X - GAME_MIN_X + 1)));
            food.y = (uint8_t)(GAME_MIN_Y + ((t2 / 11) % (GAME_MAX_Y - GAME_MIN_Y + 1)));
            if (snake_len >= MAX_SNAKE_LEN) { won = 1; break; }
        }

        for (int i = snake_len - 1; i > 0; i--) snake[i] = snake[i - 1];
        snake[0] = new_head;

        draw_cell(snake[0].x, snake[0].y, '@', 0x0e);
        if (snake_len > 1) draw_cell(snake[1].x, snake[1].y, 'o', 0x0e);
        draw_cell(food.x, food.y, '*', 0x0c);

    
        char buf[32];
        draw_text(2, 0, "                ", 0x20);
        draw_text(MAX_COLS - 14, 0, "              ", 0x20);
        strcpy(buf, "Score: "); int_to_str(score, buf + 7);
        draw_text(2, 0, buf, 0x20);
        strcpy(buf, "Best: "); int_to_str(best_score, buf + 6);
        draw_text(MAX_COLS - 10, 0, buf, 0x20);
        if (score == best_score && score > 0) draw_text((MAX_COLS-12)/2, 0, "NEW RECORD!", 0x20);
        draw_text(2, 24, "Use Arrows to move, q to quit, esc to pause", 0x20);
        draw_text(40, 25, "Difficulty: ", 0x20);
        draw_text(2, 26, diff->name, 0x20);

        pit_sleep_ms(diff->delay_ms);
    }

    if (won) {
        show_victory(score);
    } else {
        kprintf("<(24)>    Game over. Score=%d Best=%d. Press any key to go to main menu", score, best_score);
        pit_sleep_ms(2000);
        while (!kgetc_available()) { asm volatile("hlt"); }
        snake_run();
    }
}




