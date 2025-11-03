// AxonOS fullscreen text editor (Turbo Pascal-like)
#include "../inc/editor.h"
#include "../inc/vga.h"
#include "../inc/keyboard.h"
#include "../inc/fs.h"
#include "../inc/string.h"
#include "../inc/heap.h"
#include <stdint.h>

// ---- UI layout ----
#define VIEW_Y0 1
#define VIEW_H (MAX_ROWS - 2) // 1..23
#define VIEW_W (MAX_COLS)
#define TAB_WIDTH 4

// Темы оформления
typedef struct {
	const char *name;
	uint8_t menu_attr;
	uint8_t status_attr;
	uint8_t text_attr;
	uint8_t text_dim_attr;
} Theme;

static const Theme THEMES[] = {
	{"Classic",   0x78, 0x78, 0x8F, 0x87}, // white on blue menu, black on light gray status, white text, gray tildes
	{"Midnight",  0x17, 0x71, 0x1F, 0x19}, // white on navy, blue-ish status, bright text, dim blue tildes
	{"Solarized", 0x3F, 0xE0, 0x0E, 0x06}, // white on cyan menu, black on yellow status, yellow text, brown tildes
	{"Contrast",  0xF0, 0x0F, 0xF0, 0x70}, // black on white menu, white on black status, black on white text, dim gray
};
static const int THEME_COUNT = sizeof(THEMES)/sizeof(THEMES[0]);
static int g_theme_index = 0;
static uint8_t g_attr_menu = 0x1F;
static uint8_t g_attr_status = 0x70;
static uint8_t g_attr_text = 0x0F;
static uint8_t g_attr_text_dim = 0x07;

static void apply_theme(int idx) {
	if (idx < 0) idx = 0; if (idx >= THEME_COUNT) idx = THEME_COUNT - 1;
	g_theme_index = idx;
	g_attr_menu = THEMES[idx].menu_attr;
	g_attr_status = THEMES[idx].status_attr;
	g_attr_text = THEMES[idx].text_attr;
	g_attr_text_dim = THEMES[idx].text_dim_attr;
}

static void cycle_theme(void) { int ni = g_theme_index + 1; if (ni >= THEME_COUNT) ni = 0; apply_theme(ni); }

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} Line;

typedef struct {
	Line *lines;
	int line_count;
	int line_cap;

	int cursor_row; // 0-based in buffer
	int cursor_col; // 0-based in line
	int view_top;   // first visible buffer row

	int insert_mode; // 1 insert, 0 overwrite
	int modified;

	char filename[256];
} Editor;

static void ui_draw_menu(void);
static void ui_draw_status(Editor *E, const char *msg);
static void ui_draw_view(Editor *E);
static void ui_place_cursor(Editor *E);
static void ensure_cursor_visible(Editor *E);

static void buf_init(Editor *E);
static void buf_free(Editor *E);
static void buf_clear(Editor *E);
static void buf_ensure_lines(Editor *E, int need);
static void line_reserve(Line *L, size_t need_cap);
static void buf_insert_char(Editor *E, int r, int c, char ch);
static void buf_overwrite_char(Editor *E, int r, int c, char ch);
static void buf_delete_char(Editor *E, int r, int c);
static void buf_backspace(Editor *E);
static void buf_newline(Editor *E);
static void buf_join_with_next(Editor *E, int r);

static int prompt_input(const char *title, char *out, int out_size, const char *initial);
static int file_load(Editor *E, const char *path);
static int file_save(Editor *E, const char *path);

// ---- Buffer management ----
static void buf_init(Editor *E) {
	E->lines = (Line*)kcalloc(16, sizeof(Line));
	E->line_cap = 16;
	E->line_count = 1;
	E->cursor_row = 0;
	E->cursor_col = 0;
	E->view_top = 0;
	E->insert_mode = 1;
	E->modified = 0;
	E->filename[0] = '\0';
	E->lines[0].data = (char*)kcalloc(64, 1);
	E->lines[0].len = 0;
	E->lines[0].cap = 64;
}

static void buf_free(Editor *E) {
	if (!E->lines) return;
	for (int i = 0; i < E->line_count; i++) if (E->lines[i].data) kfree(E->lines[i].data);
	kfree(E->lines);
	E->lines = 0; E->line_count = E->line_cap = 0;
}

static void buf_clear(Editor *E) {
	for (int i = 0; i < E->line_count; i++) if (E->lines[i].data) { kfree(E->lines[i].data); E->lines[i].data = 0; }
	E->line_count = 1;
	E->lines[0].data = (char*)kcalloc(64, 1);
	E->lines[0].len = 0;
	E->lines[0].cap = 64;
	E->cursor_row = 0; E->cursor_col = 0; E->view_top = 0; E->modified = 0;
}

static void buf_ensure_lines(Editor *E, int need) {
	if (need <= E->line_cap) return;
	int cap = E->line_cap ? E->line_cap : 16;
	while (cap < need) cap *= 2;
	Line *nl = (Line*)kcalloc(cap, sizeof(Line));
	for (int i=0;i<E->line_count;i++) nl[i] = E->lines[i];
	kfree(E->lines);
	E->lines = nl;
	E->line_cap = cap;
}

static void line_reserve(Line *L, size_t need_cap) {
	if (need_cap <= L->cap) return;
	size_t cap = L->cap ? L->cap : 16;
	while (cap < need_cap) cap *= 2;
	char *nd = (char*)kcalloc(cap, 1);
	if (L->data && L->len) memcpy(nd, L->data, L->len);
	if (L->data) kfree(L->data);
	L->data = nd; L->cap = cap;
}

static void buf_insert_char(Editor *E, int r, int c, char ch) {
	if (r < 0 || r >= E->line_count) return;
	Line *L = &E->lines[r];
	if (c < 0) c = 0; if ((size_t)c > L->len) c = (int)L->len;
	line_reserve(L, L->len + 2);
	// shift right
	memmove(L->data + c + 1, L->data + c, L->len - (size_t)c);
	L->data[c] = ch;
	L->len++;
	E->modified = 1;
}

static void buf_overwrite_char(Editor *E, int r, int c, char ch) {
	if (r < 0 || r >= E->line_count) return;
	Line *L = &E->lines[r];
	if (c < 0) c = 0;
	if ((size_t)c < L->len) {
		L->data[c] = ch;
		E->modified = 1;
	} else {
		// extend with spaces
		line_reserve(L, (size_t)c + 2);
		while ((size_t)c > L->len) { L->data[L->len++] = ' '; }
		L->data[L->len++] = ch;
		E->modified = 1;
	}
}

static void buf_delete_char(Editor *E, int r, int c) {
	if (r < 0 || r >= E->line_count) return;
	Line *L = &E->lines[r];
	if ((size_t)c >= L->len) {
		// join with next line if exists
		if (r + 1 < E->line_count) buf_join_with_next(E, r);
		return;
	}
	memmove(L->data + c, L->data + c + 1, L->len - (size_t)c - 1);
	L->len--;
	E->modified = 1;
}

static void buf_backspace(Editor *E) {
	if (E->cursor_col > 0) {
		buf_delete_char(E, E->cursor_row, E->cursor_col - 1);
		E->cursor_col--;
	} else if (E->cursor_row > 0) {
		int prev_len = (int)E->lines[E->cursor_row - 1].len;
		buf_join_with_next(E, E->cursor_row - 1);
		E->cursor_row--; E->cursor_col = prev_len;
	}
}

static void buf_newline(Editor *E) {
	// split current line at cursor
	if (E->cursor_row < 0 || E->cursor_row >= E->line_count) return;
	Line *L = &E->lines[E->cursor_row];
	int c = E->cursor_col; if (c < 0) c = 0; if ((size_t)c > L->len) c = (int)L->len;
	Line newL = {0};
	newL.cap = (L->len - (size_t)c) + 16; newL.data = (char*)kcalloc(newL.cap, 1);
	newL.len = L->len - (size_t)c;
	if (newL.len) memcpy(newL.data, L->data + c, newL.len);
	L->len = (size_t)c;
	// insert new line after current
	buf_ensure_lines(E, E->line_count + 1);
	for (int i = E->line_count; i > E->cursor_row + 1; i--) E->lines[i] = E->lines[i - 1];
	E->lines[E->cursor_row + 1] = newL;
	E->line_count++;
	E->cursor_row++; E->cursor_col = 0;
	E->modified = 1;
}

static void buf_join_with_next(Editor *E, int r) {
	if (r < 0 || r + 1 >= E->line_count) return;
	Line *A = &E->lines[r];
	Line *B = &E->lines[r + 1];
	line_reserve(A, A->len + B->len + 1);
	memcpy(A->data + A->len, B->data, B->len);
	A->len += B->len;
	// remove B
	kfree(B->data); B->data = 0;
	for (int i = r + 1; i + 1 < E->line_count; i++) E->lines[i] = E->lines[i + 1];
	E->line_count--;
	E->modified = 1;
}

// ---- UI ----
static void ui_draw_menu(void) {
	// верхняя панель с пунктами File / View
	vga_fill_rect(0, 0, MAX_COLS, 1, ' ', g_attr_menu);
	char line[128];
	const char *name = THEMES[g_theme_index].name;
	strcpy(line, " AxonEdit v1 |  Ctrl+O Open  Ctrl+S Save  Ctrl+N New  Ctrl+G Goto  Ctrl+X Quit  Ctrl+T Theme: ");
	strcat(line, name);
	vga_write_str_xy(2, 0, line, g_attr_menu);
}

static void ui_draw_status(Editor *E, const char *msg) {
	char buf[128];
	vga_fill_rect(0, MAX_ROWS - 1, MAX_COLS, 1, ' ', g_attr_status);
	// left: file name and modified flag
	buf[0] = '\0';
	if (E->filename[0]) { strncpy(buf, E->filename, sizeof(buf)-1); buf[sizeof(buf)-1]='\0'; }
	else { strcpy(buf, "[No Name]"); }
	if (E->modified) strcat(buf, " *");
	vga_write_str_xy(1, MAX_ROWS - 1, buf, g_attr_status);
	// right: Ln/Col and mode
	char rbuf[64];
	int ln = E->cursor_row + 1; int col = E->cursor_col + 1;
	char num[16]; num[0]=0; // small itoa inline
	{
		int v=ln,n=0,s[10]; if(v==0){num[0]='0';num[1]='\0';} else {while(v){s[n++]=v%10; v/=10;} for(int i=0;i<n;i++) num[i]=(char)('0'+s[n-1-i]); num[n]='\0';}
	}
	strcpy(rbuf, "Ln "); strcat(rbuf, num); strcat(rbuf, ", Col ");
	{
		int v=col,n=0,s[10]; char nn[16]; if(v==0){nn[0]='0';nn[1]='\0';} else {while(v){s[n++]=v%10; v/=10;} for(int i=0;i<n;i++) nn[i]=(char)('0'+s[n-1-i]); nn[n]='\0';}
		strcat(rbuf, nn);
	}
	strcat(rbuf, "  "); strcat(rbuf, E->insert_mode?"INS":"OVR");
	int x = MAX_COLS - (int)strlen(rbuf) - 2; if (x < 0) x = 0;
	vga_write_str_xy((uint32_t)x, MAX_ROWS - 1, rbuf, g_attr_status);
	// center message if any
	if (msg && msg[0]) {
		int cx = (MAX_COLS - (int)strlen(msg)) / 2; if (cx < 0) cx = 0;
		vga_write_str_xy((uint32_t)cx, MAX_ROWS - 1, msg, g_attr_status);
	}
}

static void draw_line_text(uint32_t y, const char *s, size_t len) {
	// draw line content truncated/padded to width
	uint32_t x = 0;
	for (; x < VIEW_W && x < (uint32_t)len; x++) vga_putch_xy(x, y, (uint8_t)s[x], g_attr_text);
	for (; x < VIEW_W; x++) vga_putch_xy(x, y, ' ', g_attr_text);
}

static void ui_draw_view(Editor *E) {
	// clear view area
	for (uint32_t i = 0; i < VIEW_H; i++) draw_line_text(VIEW_Y0 + i, "", 0);
	// draw visible buffer lines
	for (int i = 0; i < VIEW_H; i++) {
		int r = E->view_top + i;
		if (r >= 0 && r < E->line_count) {
			Line *L = &E->lines[r];
			draw_line_text(VIEW_Y0 + (uint32_t)i, L->data ? L->data : "", L->len);
		} else {
			// beyond EOF -> dim tildes like vim
			if (r >= E->line_count) {
				vga_putch_xy(0, VIEW_Y0 + (uint32_t)i, '~', g_attr_text_dim);
				for (uint32_t x=1;x<VIEW_W;x++) vga_putch_xy(x, VIEW_Y0 + (uint32_t)i, ' ', g_attr_text);
			}
		}
	}
}

static void ensure_cursor_visible(Editor *E) {
	if (E->cursor_row < E->view_top) E->view_top = E->cursor_row;
	if (E->cursor_row >= E->view_top + VIEW_H) E->view_top = E->cursor_row - VIEW_H + 1;
	if (E->view_top < 0) E->view_top = 0;
}

static void ui_place_cursor(Editor *E) {
	int scr_y = VIEW_Y0 + (E->cursor_row - E->view_top);
	int scr_x = E->cursor_col;
	if (scr_y < VIEW_Y0) scr_y = VIEW_Y0;
	if (scr_y >= VIEW_Y0 + VIEW_H) scr_y = VIEW_Y0 + VIEW_H - 1;
	if (scr_x < 0) scr_x = 0;
	if (scr_x >= (int)VIEW_W) scr_x = (int)VIEW_W - 1;
	vga_set_cursor((uint32_t)scr_x, (uint32_t)scr_y);
}

// ---- ESC top menu ----
static void draw_menu_panel_header(void) {
	vga_fill_rect(0, 0, MAX_COLS, 1, ' ', g_attr_menu);
	vga_write_str_xy(2, 0, "Menu  (Esc: close, Enter: select)", g_attr_menu);
}

static void draw_menu_items(int sel) {
	const char *items[] = {"Open", "Save", "Exit", "About"};
	const int n = 4;
	// clear area under header
	for (uint32_t y = 1; y < 6; y++) vga_fill_rect(0, y, MAX_COLS, 1, ' ', g_attr_status);
	for (int i = 0; i < n; i++) {
		uint32_t y = 1 + (uint32_t)i;
		if (i == sel) {
			vga_write_str_xy(2, y, "> ", g_attr_status);
		} else {
			vga_write_str_xy(2, y, "  ", g_attr_status);
		}
		vga_write_str_xy(4, y, items[i], g_attr_status);
	}
	// hint
	vga_write_str_xy(2, 6, "Open/Save work with current buffer; Exit asks if modified", g_attr_status);
}

static void show_about_screen(void) {
	// simple about text overlaying top area
	for (uint32_t y = 1; y < 6; y++) vga_fill_rect(0, y, MAX_COLS, 1, ' ', g_attr_status);
	vga_write_str_xy(4, 2, "AxonEdit - text editor for AxonOS", g_attr_status);
	vga_write_str_xy(4, 3, "ESC: close menu, Ctrl+O/S/N/G/X, Ctrl+T: themes", g_attr_status);
	vga_write_str_xy(4, 4, "Press any key to return", g_attr_status);
	(void)kgetc();
}

// returns 1 if should quit editor
static int menu_show(Editor *E) {
	int sel = 0;
	draw_menu_panel_header();
	draw_menu_items(sel);
	for (;;) {
		char c = kgetc();
		if ((unsigned char)c == KEY_UP) { if (sel > 0) sel--; draw_menu_items(sel); continue; }
		if ((unsigned char)c == KEY_DOWN) { if (sel < 3) sel++; draw_menu_items(sel); continue; }
		if (c == 27) {
			// close menu
			return 0;
		}
		if (c == '\n' || c == '\r') {
			if (sel == 0) {
				char name[256];
				if (prompt_input("Open: ", name, sizeof(name), 0)) {
					if (file_load(E, name) == 0) { strncpy(E->filename, name, sizeof(E->filename)-1); E->filename[sizeof(E->filename)-1] = '\0'; ui_draw_status(E, "Opened."); }
					else { ui_draw_status(E, "Open failed!"); }
				}
				return 0;
			} else if (sel == 1) {
				if (!E->filename[0]) {
					char name[256];
					if (prompt_input("Save as: ", name, sizeof(name), 0)) { strncpy(E->filename, name, sizeof(E->filename)-1); E->filename[sizeof(E->filename)-1] = '\0'; }
				}
				if (E->filename[0]) {
					int rc = file_save(E, E->filename);
					ui_draw_status(E, rc==0?"Saved.":"Save failed!");
				}
				return 0;
			} else if (sel == 2) {
				if (E->modified) {
					char ans[8]; if (prompt_input("Unsaved changes. Quit? (y/N): ", ans, sizeof(ans), 0) && (ans[0]=='y'||ans[0]=='Y')) return 1; else return 0;
				} else return 1;
			} else if (sel == 3) {
				show_about_screen();
				draw_menu_panel_header();
				draw_menu_items(sel);
			}
		}
		// printable shortcuts inside menu
		if (c == 'o' || c == 'O') sel = 0;
		else if (c == 's' || c == 'S') sel = 1;
		else if (c == 'x' || c == 'X' || c == 'q' || c == 'Q') sel = 2;
		else if (c == 'a' || c == 'A') sel = 3;
		draw_menu_items(sel);
	}
}

// ---- Prompt input on status bar ----
static int prompt_input(const char *title, char *out, int out_size, const char *initial) {
	if (!out || out_size <= 1) return 0;
	int len = 0;
	if (initial) { len = (int)strnlen(initial, (size_t)(out_size - 1)); memcpy(out, initial, (size_t)len); }
	out[len] = '\0';
	for (;;) {
		// draw prompt on status line
		vga_fill_rect(0, MAX_ROWS - 1, MAX_COLS, 1, ' ', g_attr_status);
		vga_write_str_xy(1, MAX_ROWS - 1, title, g_attr_status);
		vga_write_str_xy((uint32_t)(1 + (int)strlen(title)), MAX_ROWS - 1, out, g_attr_status);
		vga_set_cursor((uint32_t)(1 + (int)strlen(title) + len), MAX_ROWS - 1);
		char c = kgetc();
		if (c == '\n' || c == '\r') { out[len] = '\0'; return len > 0; }
		if (c == 27) { return 0; } // ESC cancel
		if (c == 8 || c == 127) { if (len > 0) { len--; out[len] = '\0'; } continue; }
		if (c >= 32 && c < 127) {
			if (len < out_size - 1) { out[len++] = c; out[len] = '\0'; }
		}
	}
}

// ---- File I/O ----
static int file_load(Editor *E, const char *path) {
	struct fs_file *f = fs_open(path);
	if (!f) return -1;
	char *buf = 0;
	size_t sz = f->size;
	if (sz > 0) {
		buf = (char*)kmalloc(sz + 1);
		if (!buf) { fs_file_free(f); return -1; }
		ssize_t rd = fs_read(f, buf, sz, 0);
		if (rd < 0) { kfree(buf); fs_file_free(f); return -1; }
		buf[rd] = '\0'; sz = (size_t)rd;
	}
	fs_file_free(f);
	// parse into lines
	buf_clear(E);
	if (!buf || sz == 0) { if (buf) kfree(buf); return 0; }
	// count lines
	int lines = 1; for (size_t i=0;i<sz;i++) if (buf[i]=='\n') lines++;
	buf_ensure_lines(E, lines);
	E->line_count = 0;
	size_t i = 0; while (i < sz) {
		// extract line up to \n (handle CRLF)
		size_t start = i; while (i < sz && buf[i] != '\n') i++;
		size_t end = i; // [start,end)
		if (end > start && buf[end-1] == '\r') end--;
		Line L = {0}; L.cap = (end - start) + 16; L.data = (char*)kcalloc(L.cap, 1); L.len = (end - start);
		if (L.len) memcpy(L.data, buf + start, L.len);
		E->lines[E->line_count++] = L;
		if (i < sz && buf[i] == '\n') i++;
	}
	if (E->line_count == 0) { E->line_count = 1; E->lines[0].data = (char*)kcalloc(16,1); E->lines[0].len=0; E->lines[0].cap=16; }
	E->cursor_row = 0; E->cursor_col = 0; E->view_top = 0; E->modified = 0;
	kfree(buf);
	return 0;
}

static int file_save(Editor *E, const char *path) {
	// join all lines with \n
	// compute size
	size_t total = 0;
	for (int i=0;i<E->line_count;i++) total += E->lines[i].len + (i+1<E->line_count ? 1 : 0);
	char *buf = (char*)kmalloc(total ? total : 1);
	if (!buf) return -1;
	size_t off = 0;
	for (int i=0;i<E->line_count;i++) {
		if (E->lines[i].len) { memcpy(buf + off, E->lines[i].data, E->lines[i].len); off += E->lines[i].len; }
		if (i + 1 < E->line_count) buf[off++] = '\n';
	}
	struct fs_file *f = fs_open(path);
	if (!f) f = fs_create_file(path);
	if (!f) { kfree(buf); return -1; }
	ssize_t wr = fs_write(f, buf, total, 0);
	fs_file_free(f);
	kfree(buf);
	if (wr < 0 || (size_t)wr != total) return -1;
	E->modified = 0;
	return 0;
}

// ---- Key handling helpers ----
static void move_left(Editor *E) {
	if (E->cursor_col > 0) { E->cursor_col--; }
	else if (E->cursor_row > 0) { E->cursor_row--; E->cursor_col = (int)E->lines[E->cursor_row].len; }
}

static void move_right(Editor *E) {
	Line *L = &E->lines[E->cursor_row];
	if (E->cursor_col < (int)L->len) { E->cursor_col++; }
	else if (E->cursor_row + 1 < E->line_count) { E->cursor_row++; E->cursor_col = 0; }
}

static void move_up(Editor *E) {
	if (E->cursor_row > 0) E->cursor_row--;
	int len = (int)E->lines[E->cursor_row].len; if (E->cursor_col > len) E->cursor_col = len;
}

static void move_down(Editor *E) {
	if (E->cursor_row + 1 < E->line_count) E->cursor_row++;
	int len = (int)E->lines[E->cursor_row].len; if (E->cursor_col > len) E->cursor_col = len;
}

static void move_home(Editor *E) { E->cursor_col = 0; }
static void move_end(Editor *E) { E->cursor_col = (int)E->lines[E->cursor_row].len; }

static void page_up(Editor *E) {
	E->cursor_row -= VIEW_H; if (E->cursor_row < 0) E->cursor_row = 0; ensure_cursor_visible(E);
}
static void page_down(Editor *E) {
	E->cursor_row += VIEW_H; if (E->cursor_row >= E->line_count) E->cursor_row = E->line_count - 1; ensure_cursor_visible(E);
}

static void insert_tab(Editor *E) {
	int col = E->cursor_col;
	int spaces = TAB_WIDTH - (col % TAB_WIDTH);
	for (int i = 0; i < spaces; i++) {
		buf_insert_char(E, E->cursor_row, E->cursor_col, ' ');
		E->cursor_col++;
	}
}

// ---- Public entry ----
void editor_run(const char *path) {
	Editor E; buf_init(&E);
	apply_theme(0);
	if (path && path[0]) {
		if (file_load(&E, path) == 0) { strncpy(E.filename, path, sizeof(E.filename)-1); E.filename[sizeof(E.filename)-1]='\0'; }
		else { strncpy(E.filename, path, sizeof(E.filename)-1); E.filename[sizeof(E.filename)-1]='\0'; }
	}
	// initial draw
	kclear_col(0x08);
	ui_draw_menu();
	ui_draw_view(&E);
	ui_draw_status(&E, "Ctrl+O open, Ctrl+S save, Ctrl+X quit");
	ui_place_cursor(&E);

	int running = 1;
	while (running) {
		char c = kgetc();
		int redraw = 0, restatus = 0;
		int old_view_top = E.view_top;
		if (c == 27) { // ESC -> открыть верхнюю панель меню
			int quit = menu_show(&E);
			// после закрытия меню перерисовать всё
			ui_draw_menu();
			ui_draw_view(&E);
			ui_draw_status(&E, 0);
			ui_place_cursor(&E);
			if (quit) { running = 0; break; }
			continue;
		}
		if ((unsigned char)c == KEY_LEFT) { move_left(&E); redraw = 0; }
		else if ((unsigned char)c == KEY_RIGHT) { move_right(&E); redraw = 0; }
		else if ((unsigned char)c == KEY_UP) { move_up(&E); redraw = 0; }
		else if ((unsigned char)c == KEY_DOWN) { move_down(&E); redraw = 0; }
		else if ((unsigned char)c == KEY_HOME) { move_home(&E); }
		else if ((unsigned char)c == KEY_END) { move_end(&E); }
		else if ((unsigned char)c == KEY_PGUP) { page_up(&E); redraw = 1; }
		else if ((unsigned char)c == KEY_PGDN) { page_down(&E); redraw = 1; }
		else if ((unsigned char)c == KEY_INSERT) { E.insert_mode = !E.insert_mode; restatus = 1; }
		else if ((unsigned char)c == KEY_DELETE) { buf_delete_char(&E, E.cursor_row, E.cursor_col); redraw = 1; }
		else if (c == '\n' || c == '\r') { buf_newline(&E); redraw = 1; }
		else if (c == 8 || c == 127) { buf_backspace(&E); redraw = 1; }
		else if (c == 0x13) { // Ctrl+S
			if (!E.filename[0]) {
				char name[256];
				if (prompt_input("Save as: ", name, sizeof(name), 0)) { strncpy(E.filename, name, sizeof(E.filename)-1); E.filename[sizeof(E.filename)-1]='\0'; }
			}
			if (E.filename[0]) {
				int rc = file_save(&E, E.filename);
				ui_draw_status(&E, rc==0?"Saved.":"Save failed!");
				restatus = 1;
			}
		}
		else if (c == 0x0F) { // Ctrl+O
			char name[256];
			if (prompt_input("Open: ", name, sizeof(name), 0)) {
				if (file_load(&E, name) == 0) { strncpy(E.filename, name, sizeof(E.filename)-1); E.filename[sizeof(E.filename)-1]='\0'; redraw = 1; ui_draw_status(&E, "Opened."); }
				else { ui_draw_status(&E, "Open failed!"); }
				restatus = 1;
			}
		}
		else if (c == 0x0E) { // Ctrl+N
			buf_clear(&E); E.filename[0]='\0'; redraw = 1; ui_draw_status(&E, "New buffer."); restatus = 1;
		}
		else if (c == 0x07) { // Ctrl+G goto line
			char s[16];
			if (prompt_input("Goto line: ", s, sizeof(s), 0)) {
				int n = atoi(s); if (n < 1) n = 1; if (n > E.line_count) n = E.line_count;
				E.cursor_row = n - 1; int len = (int)E.lines[E.cursor_row].len; if (E.cursor_col > len) E.cursor_col = len; ensure_cursor_visible(&E); redraw = 1;
			}
		}
		else if (c == 0x18) { // Ctrl+X
			if (E.modified) {
				char ans[8]; if (prompt_input("Unsaved changes. Quit? (y/N): ", ans, sizeof(ans), 0) && (ans[0]=='y'||ans[0]=='Y')) running = 0;
			} else running = 0;
		}
		else if (c == 0x14) { // Ctrl+T -> смена темы (View)
			cycle_theme();
			ui_draw_menu();
			ui_draw_view(&E);
			restatus = 1; // обновить статус, чтобы перерисовать цвет
			redraw = 0;
		}
		else if ((unsigned char)c == KEY_TAB) { // Tab -> до следующей таб-стопы
			insert_tab(&E);
			redraw = 1;
		}
		else if (c >= 32 && c < 127) {
			if (E.insert_mode) buf_insert_char(&E, E.cursor_row, E.cursor_col, c);
			else buf_overwrite_char(&E, E.cursor_row, E.cursor_col, c);
			E.cursor_col++;
			redraw = 1;
		}

		ensure_cursor_visible(&E);
		if (E.view_top != old_view_top) redraw = 1;
		if (redraw) { ui_draw_view(&E); ui_draw_menu(); }
		if (redraw || restatus) { ui_draw_status(&E, 0); }
		ui_place_cursor(&E);
	}

	// leave screen clean
	kclear();
	buf_free(&E);
}


