#include "../inc/osh_line.h"
#include "../inc/vga.h"
#include "../inc/keyboard.h"
#include "../inc/fs.h"
#include "../inc/string.h"
#include "../inc/ext2.h"
#include "../inc/heap.h"
#include "../inc/axosh.h"
#include <stdint.h>

#define OSH_MAX_HISTORY 32
#define OSH_MAX_LINE 512

static char g_hist[OSH_MAX_HISTORY][OSH_MAX_LINE];
static int g_hist_count = 0;
static int g_hist_pos = 0;

void osh_history_init(void) { g_hist_count = 0; g_hist_pos = 0; }

void osh_history_add(const char* line) {
    if (!line || !line[0]) return;
    // skip duplicate of last
    if (g_hist_count > 0 && strncmp(g_hist[g_hist_count-1], line, OSH_MAX_LINE)==0) return;
    if (g_hist_count < OSH_MAX_HISTORY) {
        strncpy(g_hist[g_hist_count++], line, OSH_MAX_LINE-1);
        g_hist[g_hist_count-1][OSH_MAX_LINE-1] = '\0';
    } else {
        // shift up
        for (int i=1;i<OSH_MAX_HISTORY;i++) strncpy(g_hist[i-1], g_hist[i], OSH_MAX_LINE);
        strncpy(g_hist[OSH_MAX_HISTORY-1], line, OSH_MAX_LINE-1);
        g_hist[OSH_MAX_HISTORY-1][OSH_MAX_LINE-1] = '\0';
    }
    g_hist_pos = g_hist_count;
}

// helpers
static void redraw_line_xy(uint32_t sx, uint32_t sy, const char* prompt, const char* buf, int len, int cur, const char* sugg, int sugg_len) {
    // перерисовать строку в текущем месте курсора
    // очистим строку справа от начала промпта
    for (uint32_t x = sx; x < MAX_COLS; x++) vga_putch_xy(x, sy, ' ', GRAY_ON_BLACK);
    vga_write_str_xy(sx, sy, prompt, GRAY_ON_BLACK);
    uint32_t px = (uint32_t)(sx + strlen(prompt));
    vga_write_str_xy(px, sy, buf, GRAY_ON_BLACK);
    // рисуем подсказки на той же строке после введённого текста
    if (sugg && sugg_len > 0) {
        uint32_t start = px + (uint32_t)len + 1;
        for (uint32_t x = start; x < MAX_COLS; x++) vga_putch_xy(x, sy, ' ', GRAY_ON_BLACK);
        // выводим, пока помещается
        for (int i=0; i<sugg_len && (start + (uint32_t)i) < MAX_COLS; i++) {
            vga_putch_xy(start + (uint32_t)i, sy, (uint8_t)sugg[i], GRAY_ON_BLACK);
        }
    }
    // минимизируем обновления курсора
    static uint32_t last_x = 0xFFFFFFFFu, last_y = 0xFFFFFFFFu;
    uint32_t cx = (uint32_t)(px + cur), cy = sy;
    if (cx != last_x || cy != last_y) {
        vga_set_cursor(cx, cy);
        last_x = cx; last_y = cy;
    }
}

static int list_dir_entries(const char* path, const char*** out_names, int* out_count) {
    *out_names = NULL; *out_count = 0;
    struct fs_file* f = fs_open(path);
    if (!f) return -1;
    if (f->type != FS_TYPE_DIR) { fs_file_free(f); return -1; }
    size_t want = f->size ? f->size : 4096;
    uint8_t* buf = (uint8_t*)kmalloc(want+1);
    if (!buf) { fs_file_free(f); return -1; }
    ssize_t r = fs_read(f, buf, want, 0);
    fs_file_free(f);
    if (r <= 0) { kfree(buf); return -1; }
    // грубо посчитаем кол-во записей
    int cap = 64, cnt = 0;
    const char** names = (const char**)kmalloc(sizeof(char*) * cap);
    uint32_t off = 0;
    while ((size_t)off + sizeof(struct ext2_dir_entry) < (size_t)r) {
        struct ext2_dir_entry* de = (struct ext2_dir_entry*)(buf + off);
        if (de->inode == 0 || de->rec_len == 0) break;
        int nlen = de->name_len; if (nlen <= 0 || nlen > 255) { off += de->rec_len; continue; }
        if (cnt >= cap) {
            int ncap = cap * 2; const char** nn = (const char**)kmalloc(sizeof(char*)*ncap);
            for (int i=0;i<cnt;i++) nn[i]=names[i]; kfree(names); names = nn; cap = ncap;
        }
        // создаём временные C-строки поверх нового буфера
        char* s = (char*)kmalloc(nlen+1); if (!s) { off += de->rec_len; continue; }
        memcpy(s, buf + off + sizeof(*de), (size_t)nlen); s[nlen]='\0';
        names[cnt++] = s;
        off += de->rec_len;
    }
    kfree(buf);
    *out_names = names; *out_count = cnt;
    return 0;
}

static void free_name_list(const char** names, int count) {
    for (int i=0;i<count;i++) if (names[i]) kfree((void*)names[i]);
    kfree((void*)names);
}

static int is_sep(char c){ return c==' ' || c=='\t'; }

static void complete_token(const char* cwd, char* buf, int* io_len, int* io_cur, char* sugg, int sugg_cap, int* sugg_len) {
    int len = *io_len, cur = *io_cur;
    if (sugg && sugg_cap>0) { sugg[0]='\0'; *sugg_len = 0; }
    // найдём начало токена
    int start = cur; while (start>0 && !is_sep(buf[start-1])) start--;
    // текущий токен
    char token[256]; int tlen = cur - start; if (tlen<0) tlen=0; if (tlen > 255) tlen = 255;
    memcpy(token, buf+start, (size_t)tlen); token[tlen]='\0';
    // определим каталог для поиска
    char dir[256], base[256]; dir[0]='\0'; base[0]='\0';
    const char* slash = NULL; for (int i=0;i<tlen;i++) if (token[i]=='/') slash = &token[i];
    if (slash) {
        int dlen = (int)(slash - token);
        memcpy(dir, token, (size_t)dlen); dir[dlen]='\0';
        strncpy(base, slash+1, sizeof(base)-1); base[sizeof(base)-1]='\0';
    } else {
        strcpy(dir, "."); strncpy(base, token, sizeof(base)-1); base[sizeof(base)-1]='\0';
    }
    // построим абсолютный путь для dir
    char abs[512];
    if (dir[0]=='/' ) { strncpy(abs, dir, sizeof(abs)-1); abs[sizeof(abs)-1]='\0'; }
    else {
        size_t cl = strlen(cwd);
        if (cl==1 && cwd[0]=='/') { abs[0]='/'; size_t dl=strlen(dir); size_t cp = (dl < sizeof(abs)-2)?dl:(sizeof(abs)-2); if (cp) memcpy(abs+1, dir, cp); abs[1+cp]='\0'; }
        else { size_t dl=strlen(dir), cp1 = (cl < sizeof(abs)-1)?cl:(sizeof(abs)-1); memcpy(abs, cwd, cp1); size_t pos = cp1; if (pos < sizeof(abs)-1) abs[pos++]='/'; size_t rem = sizeof(abs)-pos-1; size_t cp2 = (dl<rem)?dl:rem; if (cp2) memcpy(abs+pos, dir, cp2); abs[pos+cp2]='\0'; }
    }
    // получим список файлов
    const char** fs_names = NULL; int fs_count = 0;
    (void)list_dir_entries(abs, &fs_names, &fs_count); // игнорируем ошибку, просто 0 кандидатов
    // если токен первый (start==0) и нет '/' — добавим builtin команды
    const char** bnames = NULL; int bcount = 0;
    const char** builtin = NULL; int n_builtin = 0;
    if (start == 0 && !slash) {
        n_builtin = osh_get_builtin_names(&builtin);
        if (n_builtin > 0) { bnames = builtin; bcount = n_builtin; }
    }
    // теперь фильтруем по base
    int matches = 0; char common[256]; common[0]='\0';
    // 1) builtin
    for (int i=0;i<bcount;i++) {
        if (strncmp(bnames[i], base, strlen(base))==0) {
            if (matches==0) { strncpy(common, bnames[i], sizeof(common)-1); common[sizeof(common)-1]='\0'; }
            else {
                int k=0; while (common[k] && bnames[i][k] && common[k]==bnames[i][k]) k++;
                common[k]='\0';
            }
            matches++;
        }
    }
    // 2) файловая система
    for (int i=0;i<fs_count;i++) {
        if (strncmp(fs_names[i], base, strlen(base))==0) {
            if (matches==0) { strncpy(common, fs_names[i], sizeof(common)-1); common[sizeof(common)-1]='\0'; }
            else {
                int k=0; while (common[k] && fs_names[i][k] && common[k]==fs_names[i][k]) k++;
                common[k]='\0';
            }
            matches++;
        }
    }
    // отфильтруем по base и найдём общий префикс
    if (matches == 0) { if (fs_names) free_name_list(fs_names, fs_count); return; }
    // вставим недостающую часть общего префикса
    int add = (int)strlen(common) - (int)strlen(base);
    if (add > 0) {
        if (len + add < OSH_MAX_LINE-1) {
            memmove(buf + cur + add, buf + cur, (size_t)(len - cur + 1));
            memcpy(buf + cur, common + strlen(base), (size_t)add);
            cur += add; len += add;
        }
    } else if (matches > 1 && sugg && sugg_cap>0) {
        // собрать варианты в строку подсказок, вывести на той же строке
        int pos = 0;
        // builtin
        for (int i=0;i<bcount;i++) {
            if (strncmp(bnames[i], base, strlen(base))==0) {
                int need = (int)strlen(bnames[i]) + 2;
                if (pos + need >= sugg_cap) break;
                memcpy(sugg + pos, bnames[i], strlen(bnames[i])); pos += (int)strlen(bnames[i]);
                sugg[pos++] = ' '; sugg[pos++] = ' ';
            }
        }
        // fs
        for (int i=0;i<fs_count;i++) {
            if (strncmp(fs_names[i], base, strlen(base))==0) {
                int need = (int)strlen(fs_names[i]) + 2;
                if (pos + need >= sugg_cap) break;
                memcpy(sugg + pos, fs_names[i], strlen(fs_names[i])); pos += (int)strlen(fs_names[i]);
                sugg[pos++] = ' '; sugg[pos++] = ' ';
            }
        }
        sugg[pos] = '\0'; *sugg_len = pos;
    }
    *io_len = len; *io_cur = cur;
    if (fs_names) free_name_list(fs_names, fs_count);
}

int osh_line_read(const char* prompt, const char* cwd, char* out, int out_size) {
    if (!out || out_size <= 1) return -1;
    char buf[OSH_MAX_LINE]; int len = 0, cur = 0;
    buf[0]='\0';
    uint32_t sx=0, sy=0; vga_get_cursor(&sx, &sy);
    char sugg[512]; int sugg_len = 0; sugg[0] = '\0';
    redraw_line_xy(sx, sy, prompt, buf, len, cur, sugg, sugg_len);
    for (;;) {
        char c = kgetc();
        if (c == '\n' || c == '\r') {
            buf[len]='\0'; strncpy(out, buf, (size_t)out_size-1); out[out_size-1]='\0';
            kprint((uint8_t*)"\n");
            return len;
        }
        if ((unsigned char)c == KEY_LEFT) { if (cur>0) cur--; }
        else if ((unsigned char)c == KEY_RIGHT) { if (cur<len) cur++; }
        else if ((unsigned char)c == KEY_HOME) { cur = 0; }
        else if ((unsigned char)c == KEY_END) { cur = len; }
        else if ((unsigned char)c == KEY_UP) {
            if (g_hist_count>0) { if (g_hist_pos>0) g_hist_pos--; strncpy(buf, g_hist[g_hist_pos], OSH_MAX_LINE-1); buf[OSH_MAX_LINE-1]='\0'; len = (int)strlen(buf); cur = len; }
        }
        else if ((unsigned char)c == KEY_DOWN) {
            if (g_hist_count>0) { if (g_hist_pos < g_hist_count-1) g_hist_pos++; strncpy(buf, g_hist[g_hist_pos], OSH_MAX_LINE-1); buf[OSH_MAX_LINE-1]='\0'; len = (int)strlen(buf); cur = len; }
        }
        else if ((unsigned char)c == KEY_DELETE) { if (cur < len) { memmove(buf+cur, buf+cur+1, (size_t)(len-cur)); len--; buf[len]='\0'; } }
        else if (c == 8 || c == 127) { if (cur>0) { memmove(buf+cur-1, buf+cur, (size_t)(len-cur+1)); cur--; len--; } }
        else if ((unsigned char)c == KEY_TAB) { complete_token(cwd, buf, &len, &cur, sugg, (int)sizeof(sugg), &sugg_len); }
        else if (c >= 32 && c < 127) {
            if (len+1 < OSH_MAX_LINE) {
                memmove(buf+cur+1, buf+cur, (size_t)(len-cur+1));
                buf[cur]=c; cur++; len++;
            }
            // любой обычный ввод — очистить подсказки
            sugg[0]='\0'; sugg_len=0;
        }
        redraw_line_xy(sx, sy, prompt, buf, len, cur, sugg, sugg_len);
    }
}


