#include <stdint.h>

#define VIDEO_ADDRESS 0xb8000
#define MAX_ROWS 25
#define MAX_COLS 80

#define WHITE_ON_BLACK 0x0f
#define GRAY_ON_BLACK 0x07

#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

void kprint(uint8_t *str);
void kputchar(uint8_t character, uint8_t attribute_byte);
void kprint_colorized(const char* str);
void kclear();
void kclear_col(uint8_t attribute_byte);
void write(uint8_t character, uint8_t attribute_byte, uint16_t offset);
void scroll_line();
uint16_t get_cursor();
void set_cursor(uint16_t pos);
uint16_t get_cursor_x();
uint16_t get_cursor_y();
void set_cursor_x(uint16_t x);
void set_cursor_y(uint16_t y);
void vga_set_cursor(uint32_t x, uint32_t y);
void vga_get_cursor(uint32_t* x, uint32_t* y);