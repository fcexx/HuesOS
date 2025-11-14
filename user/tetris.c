// Simple Tetris in text mode, modeled after snake.c style
#include "../inc/vga.h"
#include "../inc/keyboard.h"
#include "../inc/pit.h"
#include <stdint.h>
#include <string.h>

#define KEY_UP         0x80
#define KEY_DOWN       0x81
#define KEY_LEFT       0x82
#define KEY_RIGHT      0x83

// Board size (standard 10x20)
#define TETRIS_W 10
#define TETRIS_H 20

// Screen area reserved for the game (inside the global outer border)
#define GAME_MIN_X 1
#define GAME_MIN_Y 1
#define GAME_MAX_X (MAX_COLS - 2)
#define GAME_MAX_Y (MAX_ROWS - 2)

// Colors helpers: background color in high nibble
#define BG(c)   ((uint8_t)(((c) << 4) | 0x0))

typedef struct { uint8_t x, y; } Point;

static uint16_t cell_offset(uint8_t x, uint8_t y) {
	return (uint16_t)((y * MAX_COLS + x) * 2);
}

static void draw_cell(uint8_t x, uint8_t y, uint8_t ch, uint8_t color) {
	write(ch, color, cell_offset(x, y));
}

static void draw_text(uint8_t x, uint8_t y, const char* s, uint8_t color) {
	for (uint8_t i = 0; s[i]; i++) draw_cell(x + i, y, (uint8_t)s[i], color);
}

static void draw_border(void) {
	// Outer screen border like in snake
	for (uint8_t x = 0; x < MAX_COLS; x++) {
		draw_cell(x, 0, ' ', 0x22);
		draw_cell(x, MAX_ROWS - 1, ' ', 0x22);
	}
	for (uint8_t y = 0; y < MAX_ROWS; y++) {
		draw_cell(0, y, ' ', 0x22);
		draw_cell(MAX_COLS - 1, y, ' ', 0x22);
	}
}

// Scoring and difficulty
typedef struct Difficulty { const char* name; uint32_t fall_ms; } Difficulty;
static const Difficulty difficulties[] = {
	{"Very Easy", 500},
	{"Easy",      400},
	{"Normal",    320},
	{"Hard",      240},
	{"Very Hard", 160},
};
static const int DIFFICULTY_COUNT = sizeof(difficulties)/sizeof(difficulties[0]);

static int best_score = 0;

// Tetromino definitions: 7 pieces, up to 4 rotations, each 4x4 mask
// Layout bits: for r in 0..3, 16-bit mask row-major (top-left bit = (x,y)=(0,0))
typedef struct { uint16_t rot[4]; uint8_t color; } Piece;

// Colors use background only (draw ' ' with colored bg)
enum { C_CYAN=0x3, C_BLUE=0x1, C_ORANGE=0x6, C_YELLOW=0xE, C_GREEN=0x2, C_PURPLE=0x5, C_RED=0x4 };

// Helper to define 4x4 mask from 4 strings of 4 chars (".#..")
#define ROW(b3,b2,b1,b0) ((uint16_t)((b3)<<15 | (b2)<<14 | (b1)<<13 | (b0)<<12))

// Precomputed masks for classic SRS shapes (without wall-kick tables; simple rotation)
// I
static const Piece PIECES[7] = {
	{ // I
		{ 0x0F00, 0x2222, 0x00F0, 0x4444 }, BG(C_CYAN)
	},
	{ // J
		{ 0x8E00, 0x6440, 0x0E20, 0x44C0 }, BG(C_BLUE)
	},
	{ // L
		{ 0x2E00, 0x4460, 0x0E80, 0xC440 }, BG(C_ORANGE)
	},
	{ // O
		{ 0x6600, 0x6600, 0x6600, 0x6600 }, BG(C_YELLOW)
	},
	{ // S
		{ 0x6C00, 0x4620, 0x06C0, 0x8C40 }, BG(C_GREEN)
	},
	{ // T
		{ 0x4E00, 0x4640, 0x0E40, 0x4C40 }, BG(C_PURPLE)
	},
	{ // Z
		{ 0xC600, 0x2640, 0x0C60, 0x4C80 }, BG(C_RED)
	},
};

typedef struct { int x, y; int r; int type; } Active;

// Board: 0 empty, else attribute color
static uint8_t board[TETRIS_H][TETRIS_W];

// UI placement
static uint8_t pf_x0, pf_y0; // top-left of outer playfield border

static void board_clear(void) { memset(board, 0, sizeof(board)); }

static uint8_t rnd_u8(void) {
	uint64_t t = pit_get_ticks();
	// Simple xorshift-ish mix
	t ^= t << 13; t ^= t >> 7; t ^= t << 17;
	return (uint8_t)t;
}

static int piece_cell(uint16_t mask, int px, int py) {
	// 4x4, top row is highest 4 bits (15..12)
	int bit = 15 - (py*4 + px);
	return (mask >> bit) & 1;
}

static int can_place(const Active* a) {
	uint16_t m = PIECES[a->type].rot[a->r & 3];
	for (int dy=0; dy<4; dy++) {
		for (int dx=0; dx<4; dx++) {
			if (!piece_cell(m, dx, dy)) continue;
			int x = a->x + dx;
			int y = a->y + dy;
			// Allow pieces to be above the board (y < 0) but not below or outside horizontally
			if (x < 0 || x >= TETRIS_W || y >= TETRIS_H) return 0;
			// Only check collision with board if cell is inside visible area
			if (y >= 0 && board[y][x]) return 0;
		}
	}
	return 1;
}

static void lock_piece(const Active* a) {
	uint8_t col = PIECES[a->type].color;
	uint16_t m = PIECES[a->type].rot[a->r & 3];
	for (int dy=0; dy<4; dy++) {
		for (int dx=0; dx<4; dx++) {
			if (!piece_cell(m, dx, dy)) continue;
			int x = a->x + dx;
			int y = a->y + dy;
			if (x>=0 && x<TETRIS_W && y>=0 && y<TETRIS_H) board[y][x] = col;
		}
	}
}

static int clear_lines(void) {
	int cleared = 0;
	for (int y=TETRIS_H-1; y>=0; y--) {
		int full = 1;
		for (int x=0; x<TETRIS_W; x++) if (!board[y][x]) { full = 0; break; }
		if (full) {
			cleared++;
			for (int yy=y; yy>0; yy--) memcpy(board[yy], board[yy-1], TETRIS_W);
			memset(board[0], 0, TETRIS_W);
			y++; // re-check same row after pull-down
		}
	}
	return cleared;
}

static void draw_playfield_border(void) {
	// Border around the 10x20 playfield
	uint8_t bx0 = pf_x0, by0 = pf_y0;
	uint8_t bx1 = pf_x0 + (uint8_t)(TETRIS_W+1);
	uint8_t by1 = pf_y0 + (uint8_t)(TETRIS_H+1);
	for (uint8_t x = bx0; x <= bx1; x++) {
		draw_cell(x, by0, ' ', 0x22);
		draw_cell(x, by1, ' ', 0x22);
	}
	for (uint8_t y = by0; y <= by1; y++) {
		draw_cell(bx0, y, ' ', 0x22);
		draw_cell(bx1, y, ' ', 0x22);
	}
}

static void draw_board_and_piece(const Active* a) {
	// Clear interior with checkerboard pattern
	for (int y=0; y<TETRIS_H; y++) {
		for (int x=0; x<TETRIS_W; x++) {
			uint8_t col = board[y][x];
			uint8_t sx = pf_x0 + 1 + (uint8_t)x;
			uint8_t sy = pf_y0 + 1 + (uint8_t)y;
			// Checkerboard pattern: dark gray on alternating cells
			if (!col) {
				col = ((x + y) & 1) ? 0x00 : 0x80; // 0x80 = dark gray background
			}
			draw_cell(sx, sy, ' ', col);
		}
	}
	// Draw active on top
	if (a) {
		uint16_t m = PIECES[a->type].rot[a->r & 3];
		uint8_t col = PIECES[a->type].color;
		for (int dy=0; dy<4; dy++) for (int dx=0; dx<4; dx++) if (piece_cell(m, dx, dy)) {
			int x = a->x + dx, y = a->y + dy;
			if (x>=0 && x<TETRIS_W && y>=0 && y<TETRIS_H) {
				uint8_t sx = pf_x0 + 1 + (uint8_t)x;
				uint8_t sy = pf_y0 + 1 + (uint8_t)y;
				draw_cell(sx, sy, ' ', col);
			}
		}
	}
}

static void draw_hud(int score, int lines, const char* diff_name) {
	char buf[64];
	// Top HUD bar
	draw_text(2, 0, "                                                                 ", 0x20);
	strcpy(buf, "Score: "); int v=score; char* p=buf+7; // int_to_str inline
	if (v==0) { *p++='0'; } else { int s[10],n=0; while(v){s[n++]=v%10; v/=10;} for(int i=n-1;i>=0;i--)*p++=(char)('0'+s[i]); }
	*p='\0';
	draw_text(2, 0, buf, 0x20);

	strcpy(buf, "Lines: "); v=lines; p=buf+7; if (v==0) { *p++='0'; } else { int s[10],n=0; while(v){s[n++]=v%10; v/=10;} for(int i=n-1;i>=0;i--)*p++=(char)('0'+s[i]); } *p='\0';
	draw_text(18, 0, buf, 0x20);

	strcpy(buf, "Best: "); v=best_score; p=buf+6; if (v==0) { *p++='0'; } else { int s[10],n=0; while(v){s[n++]=v%10; v/=10;} for(int i=n-1;i>=0;i--)*p++=(char)('0'+s[i]); } *p='\0';
	draw_text(34, 0, buf, 0x20);

	draw_text(MAX_COLS - 20, 0, "Diff: ", 0x20);
	draw_text(MAX_COLS - 14, 0, diff_name, 0x20);

	draw_text(2, MAX_ROWS-2, "Arrows: move/rotate, Down: soft drop, Esc: pause, Q: quit", 0x20);
}

static int show_menu_and_get_choice(void) {
	kclear_col(0x01);
	draw_border();
	const char* title = "TETRIS";
	uint8_t title_x = (MAX_COLS - (int)strlen(title)) / 2;
	uint8_t title_y = 3;
	draw_text(title_x, title_y, title, WHITE_ON_BLACK);

	int sel = 2;
	int running = 1;
	while (running) {
		for (int i = 0; i < DIFFICULTY_COUNT; i++) {
			uint8_t x = (MAX_COLS - 20) / 2;
			uint8_t y = 6 + i * 2;
			if (i == sel) draw_text(x - 2, y, "> ", WHITE_ON_BLACK);
			else draw_text(x - 2, y, "  ", WHITE_ON_BLACK);
			draw_text(x, y, difficulties[i].name, GRAY_ON_BLACK);
		}
		draw_text((MAX_COLS-36)/2, MAX_ROWS-3, "Use arrows to select, Enter to start", GRAY_ON_BLACK);

	char c = 0; while (!(c = kgetc())) { pit_sleep_ms(1); }
		if ((unsigned char)c == KEY_UP) { if (sel > 0) sel--; }
		else if ((unsigned char)c == KEY_DOWN) { if (sel < DIFFICULTY_COUNT-1) sel++; }
		else if (c == '\n' || c == '\r') { running = 0; }
		else if (c == 27) { kclear(); return -1; }
	}
	return sel;
}

static void spawn_piece(Active* a, int next_type) {
	a->type = next_type;
	a->r = 0;
	a->x = (TETRIS_W/2) - 2;
	a->y = 0; // start at top
}

void tetris_run(void) {
	int choice = show_menu_and_get_choice();
	if (choice < 0) return;
	const Difficulty* diff = &difficulties[choice];

	// Layout
	kclear_col(0x01);
	draw_border();
	pf_x0 = (uint8_t)((MAX_COLS - (TETRIS_W + 2)) / 2);
	pf_y0 = 2; // leave room for HUD
	draw_playfield_border();

	board_clear();
	int score = 0, lines = 0;

	// Next piece queue
	int next_type = rnd_u8() % 7;
	Active cur; spawn_piece(&cur, rnd_u8() % 7);

	// Draw initial HUD
	draw_hud(score, lines, diff->name);

	// Game loop
	uint64_t last_drop_ms = pit_get_time_ms();
	uint64_t last_input_ms = last_drop_ms;
	int paused = 0;
	int running = 1;

	while (running) {
		// Input (non-blocking)
		char c = 0;
		if (kgetc_available()) c = kgetc();
		if (c) {
			if (c == 27) { // ESC pause
				paused = !paused;
				if (paused) draw_text((MAX_COLS-6)/2, (MAX_ROWS/2), "PAUSED", WHITE_ON_BLACK);
				else {
					for (int i=0;i<6;i++) draw_cell((MAX_COLS-6)/2 + i, (MAX_ROWS/2), ' ', WHITE_ON_BLACK);
				}
			}
			if (!paused) {
				Active t = cur;
				if ((unsigned char)c == KEY_LEFT) { t.x--; if (can_place(&t)) cur = t; }
				else if ((unsigned char)c == KEY_RIGHT) { t.x++; if (can_place(&t)) cur = t; }
				else if ((unsigned char)c == KEY_DOWN) { t.y++; if (can_place(&t)) { cur = t; score += 1; draw_hud(score, lines, diff->name); last_drop_ms = pit_get_time_ms(); } }
				else if ((unsigned char)c == KEY_UP) {
					t.r = (t.r + 1) & 3;
					// simple wall-kick left/right 1 tile
					if (!can_place(&t)) { t.x++; if (!can_place(&t)) { t.x-=2; if (!can_place(&t)) t = cur; } }
					cur = t;
				}
				else if (c=='q' || c=='Q') { running = 0; break; }
			}
		}

		if (paused) { pit_sleep_ms(50); continue; }

		uint64_t now = pit_get_time_ms();
		// Gravity
		if (now - last_drop_ms >= diff->fall_ms) {
			Active t = cur; t.y++;
			if (can_place(&t)) {
				cur = t;
			} else {
				// Lock, clear lines, spawn next
				lock_piece(&cur);
				int got = clear_lines();
				if (got) {
					// Basic scoring inspired by classic tetris
					static const int per[5] = {0, 100, 300, 500, 800};
					score += per[got];
					lines += got;
					if (score > best_score) best_score = score;
					draw_hud(score, lines, diff->name);
				}

				// New piece
				spawn_piece(&cur, next_type);
				next_type = rnd_u8() % 7;

				// If cannot place initial position -> game over
				if (!can_place(&cur)) {
					running = 0; break;
				}
			}
			last_drop_ms = now;
		}

		// Draw frame
		draw_board_and_piece(&cur);
		pit_sleep_ms(16); // ~60 FPS-ish idle
	}

	// Game over screen
	kclear_col(0x01);
	draw_border();
	const char *msg1 = "GAME OVER";
	const char *msg2 = "Press any key to return";
	uint8_t x1 = (MAX_COLS - (int)strlen(msg1)) / 2;
	uint8_t x2 = (MAX_COLS - (int)strlen(msg2)) / 2;
	uint8_t y = MAX_ROWS / 2;
	draw_text(x1, y-1, msg1, WHITE_ON_BLACK);
	draw_text(x2, y+1, msg2, GRAY_ON_BLACK);
	if (score > best_score) best_score = score;
	// wait key
	while (!kgetc_available()) { pit_sleep_ms(1); }
	(void)kgetc();
	
	// Clear screen before returning to shell
	kclear();
}

