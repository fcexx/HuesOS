// AxonOS shell (osh): bash-like minimal interpreter with pipes and redirections
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../inc/axosh.h"
#include "../inc/vga.h"
#include "../inc/keyboard.h"
#include "../inc/heap.h"
#include "../inc/fs.h"
#include "../inc/ext2.h"
#include "../inc/ramfs.h"
#include <stdint.h> // for rtc types forward-declared below
#include "../inc/thread.h"
#include "../inc/editor.h"
#include "../inc/snake.h"
#include "../inc/tetris.h"
#include "../inc/clock.h"
#include "../inc/neofetch.h"

typedef long ssize_t;

// forward declare minimal RTC API to avoid include issues
typedef struct { uint8_t second, minute, hour, day, month; uint16_t year; } rtc_datetime_t;
extern volatile uint64_t rtc_ticks;
void rtc_read_datetime(rtc_datetime_t* dt);

// -------- util --------
static char g_cwd[256] = "/";

static void osh_write(char **out, size_t *len, size_t *cap, const char *s) {
    if (!s) return;
    size_t add = strlen(s);
    if (add == 0) return;
    size_t need = *len + add + 1;
    if (need > *cap) {
        size_t ncap = (*cap ? *cap : 128);
        while (ncap < need) ncap *= 2;
        char *nb = (char*)kmalloc(ncap);
        if (!nb) return;
        if (*out && *len) memcpy(nb, *out, *len);
        if (*out) kfree(*out);
        *out = nb; *cap = ncap;
    }
    memcpy(*out + *len, s, add);
    *len += add; (*out)[*len] = '\0';
}

// very simple join: if arg starts with '/', copy; else cwd + '/' + arg (no normalization)
static void join_cwd(const char* cwd, const char* arg, char* out, size_t outsz) {
    if (!arg || !arg[0]) { strncpy(out, cwd, outsz-1); out[outsz-1]='\0'; return; }
    if (arg[0] == '/') { strncpy(out, arg, outsz-1); out[outsz-1]='\0'; return; }
    size_t cl = strlen(cwd);
    if (cl == 1 && cwd[0] == '/') {
        size_t al = strlen(arg); size_t copy = (al+2 < outsz) ? al+2 : outsz; if (outsz) { out[0]='/'; if (copy>1) memcpy(out+1, arg, copy-2); out[copy-1]='\0'; }
        return;
    }
    // strip trailing '/'
    char base[512]; strncpy(base, cwd, sizeof(base)-1); base[sizeof(base)-1]='\0';
    while (cl>1 && base[cl-1]=='/') { base[--cl]='\0'; }
    size_t al = strlen(arg);
    size_t need = cl + 1 + al + 1;
    if (outsz) {
        size_t pos=0; size_t copy = (need < outsz) ? need : outsz; memcpy(out+pos, base, cl); pos+=cl; out[pos++]='/';
        size_t rem = copy - pos - 1; if ((int)rem < 0) rem = 0; if (rem > al) rem = al; if (rem) memcpy(out+pos, arg, rem); pos += rem; out[pos]='\0';
    }
}

static void resolve_path(const char *cwd, const char *arg, char *out, size_t outlen) {
    if (!arg || arg[0] == '\0') { strncpy(out, cwd, outlen-1); out[outlen-1] = '\0'; return; }
    if (arg[0] == '/') { strncpy(out, arg, outlen-1); out[outlen-1] = '\0'; return; }
    const char *p = arg; if (p[0]=='.' && p[1]=='/') p+=2;
    char tmp[512];
    if (strcmp(cwd, "/") == 0) {
        tmp[0] = '/'; size_t n = strlen(p); if (n > sizeof(tmp)-2) n = sizeof(tmp)-2; memcpy(tmp+1, p, n); tmp[1+n]='\0';
    } else {
        size_t a = strlen(cwd); if (a > sizeof(tmp)-2) a = sizeof(tmp)-2; memcpy(tmp, cwd, a); tmp[a] = '/';
        size_t n = strlen(p); if (n > sizeof(tmp)-a-2) n = sizeof(tmp)-a-2; memcpy(tmp + a + 1, p, n); tmp[a+1+n] = '\0';
    }
    // normalize
    char *parts[64]; int pc=0; char *s = tmp; if (*s!='/') { parts[pc++] = s; }
    s++; while (*s) {
        char *seg = s; while (*s && *s!='/') s++; size_t L = (size_t)(s - seg);
        if (L>0) {
            char save = seg[L]; seg[L] = '\0';
            if (strcmp(seg, ".") == 0) {}
            else if (strcmp(seg, "..") == 0) { if (pc>0) pc--; }
            else { parts[pc++] = seg; }
            seg[L] = save;
        }
        if (*s) s++;
    }
    if (pc == 0) { strncpy(out, "/", outlen-1); out[outlen-1] = '\0'; return; }
    size_t pos=0; out[0]='\0';
    for (int i=0;i<pc;i++) {
        size_t need = pos + 1 + strlen(parts[i]) + 1; if (need > outlen) break;
        out[pos++] = '/'; size_t n = strlen(parts[i]); memcpy(out+pos, parts[i], n); pos += n; out[pos] = '\0';
    }
}

static int is_dir_path(const char *path) {
    struct fs_file *f = fs_open(path); if (!f) return 0; int dir = (f->type == FS_TYPE_DIR);
    if (!dir && f->type == FS_TYPE_UNKNOWN) {
        size_t want = f->size ? f->size : 512; if (want > 8192) want = 8192; void *buf = kmalloc(want+1);
        if (buf) { ssize_t r = fs_read(f, buf, want, 0); if (r > 0) { struct ext2_dir_entry *de = (struct ext2_dir_entry*)buf; if (de->rec_len) dir = 1; } kfree(buf);} }
    fs_file_free(f); return dir;
}

// -------- lexer --------
typedef enum { T_WORD, T_AND, T_OR, T_PIPE, T_BG, T_GT, T_LT } tok_t;
typedef struct { tok_t t; char *s; } token;

static token* lex(const char *line, int *out_n) {
    int cap = 16, n = 0; token *v = (token*)kcalloc((size_t)cap, sizeof(token));
    const char *p = line; while (*p) {
        while (*p==' '||*p=='\t') p++;
        if (!*p) break;
        // Treat color tag "<(..)>" as start of a word (not as '<' redirection)
        if (p[0]=='<' && p[1]=='(') {
            char buf[512]; int bi=0; const char* q=p; int inq=0;
            while (*q && !( !inq && (*q==' '||*q=='\t'||*q=='|'||*q=='&') )) {
                if (*q=='"') { inq = !inq; q++; continue; }
                // copy color tag verbatim
                if (!inq && *q=='<' && q[1]=='(') {
                    if (bi < (int)sizeof(buf)-1) buf[bi++]=*q++;
                    while (*q && *q != '>') { if (bi < (int)sizeof(buf)-1) buf[bi++]=*q++; }
                    if (*q=='>') { if (bi < (int)sizeof(buf)-1) buf[bi++]=*q++; }
                    continue;
                }
                if (bi < (int)sizeof(buf)-1) buf[bi++] = *q++;
            }
            buf[bi]='\0';
            char *ws = (char*)kmalloc((size_t)bi+1); memcpy(ws, buf, (size_t)bi+1);
            if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));}
            v[n++] = (token){T_WORD, ws};
            p = q; // advance input pointer
            continue;
        }
        if (p[0]=='&' && p[1]=='&') { if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));} v[n++] = (token){T_AND, NULL}; p+=2; continue; }
        if (p[0]=='|' && p[1]=='|') { if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));} v[n++] = (token){T_OR, NULL}; p+=2; continue; }
        if (*p=='|') { if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));} v[n++] = (token){T_PIPE, NULL}; p++; continue; }
        if (*p=='&') { if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));} v[n++] = (token){T_BG, NULL}; p++; continue; }
        // Disable parsing of '>' and '<' as operators (to avoid conflict with color tags)
        // word (support quotes and Axon color tags like <(f0)>)
        const char *start = p; char buf[512]; int bi=0; int inq=0;
        while (*p && !( !inq && (*p==' '||*p=='\t'||*p=='|'||*p=='&') )) {
            if (*p=='"') { inq = !inq; p++; continue; }
            // treat '<(' ... '>' as literal (color tag), not as redirection
            if (!inq && *p=='<' && p[1]=='(') {
                // copy until first '>' or end
                if (bi < (int)sizeof(buf)-1) buf[bi++] = *p; p++;
                while (*p && *p != '>') { if (bi < (int)sizeof(buf)-1) buf[bi++] = *p; p++; }
                if (*p == '>') { if (bi < (int)sizeof(buf)-1) buf[bi++] = *p; p++; }
                continue;
            }
            if (bi < (int)sizeof(buf)-1) buf[bi++] = *p; p++;
        }
        buf[bi] = '\0';
        char *ws = (char*)kmalloc((size_t)bi+1); memcpy(ws, buf, (size_t)bi+1);
        if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));}
        v[n++] = (token){T_WORD, ws}; (void)start;
    }
    *out_n = n; return v;
}

static void free_tokens(token *v, int n) { for (int i=0;i<n;i++) if (v[i].t==T_WORD && v[i].s) kfree(v[i].s); kfree(v); }

// -------- builtins --------
typedef struct { char **argv; int argc; const char *in; char **out; size_t *out_len; size_t *out_cap; } cmd_ctx;

// forward to allow builtins to execute lines
static int exec_line(const char *line);

static int out_printf(cmd_ctx *c, const char *s) { osh_write(c->out, c->out_len, c->out_cap, s); return 0; }

static int bi_echo(cmd_ctx *c) {
    if (c->argc <= 1) { out_printf(c, "\n"); return 0; }
    for (int i=1;i<c->argc;i++) { out_printf(c, c->argv[i]); if (i+1<c->argc) out_printf(c, " "); }
    out_printf(c, "\n"); return 0;
}

static int bi_pwd(cmd_ctx *c) { (void)c; char tmp[300]; strncpy(tmp, g_cwd, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0'; osh_write(c->out, c->out_len, c->out_cap, tmp); osh_write(c->out, c->out_len, c->out_cap, "\n"); return 0; }

static int bi_cd(cmd_ctx *c) {
    const char *arg = c->argc>1 ? c->argv[1] : "/";
    char path[256]; resolve_path(g_cwd, arg, path, sizeof(path));
    if (!is_dir_path(path)) { osh_write(c->out, c->out_len, c->out_cap, "cd: not a directory\n"); return 1; }
    size_t l = strlen(path); if (l>1 && path[l-1]=='/') path[l-1]='\0'; strncpy(g_cwd, path, sizeof(g_cwd)-1); g_cwd[sizeof(g_cwd)-1]='\0'; return 0;
}

static int bi_cls(cmd_ctx *c) { (void)c; kclear(); return 0; }

static int bi_ls(cmd_ctx *c) {
    char path[256]; if (c->argc<2) resolve_path(g_cwd, "", path, sizeof(path)); else resolve_path(g_cwd, c->argv[1], path, sizeof(path));
    struct fs_file *f = fs_open(path); if (!f) { osh_write(c->out, c->out_len, c->out_cap, "ls: cannot access\n"); return 1; }
    if (f->type != FS_TYPE_DIR) { osh_write(c->out, c->out_len, c->out_cap, c->argv[1] ? c->argv[1] : path); osh_write(c->out, c->out_len, c->out_cap, "\n"); fs_file_free(f); return 0; }
    size_t want = f->size ? f->size : 4096; void *buf = kmalloc(want+1); ssize_t r = buf?fs_read(f, buf, want, 0):0; if (r>0) {
        uint32_t off=0; while ((size_t)off < (size_t)r) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry*)((uint8_t*)buf+off); if (de->inode==0 || de->rec_len==0) break; int nlen=de->name_len; if (nlen>255)nlen=255; char name[256]; memcpy(name, (uint8_t*)buf+off+sizeof(*de), (size_t)nlen); name[nlen]='\0';
            osh_write(c->out, c->out_len, c->out_cap, name); if (de->file_type==EXT2_FT_DIR) osh_write(c->out, c->out_len, c->out_cap, "/"); osh_write(c->out, c->out_len, c->out_cap, "\n"); off += de->rec_len;
        }
    }
    if (buf) kfree(buf); fs_file_free(f); return 0;
}

static int bi_cat(cmd_ctx *c) {
    if (c->argc <= 1) { if (c->in) { osh_write(c->out, c->out_len, c->out_cap, c->in); } return 0; }
    int rc = 0;
    for (int i=1;i<c->argc;i++) {
        char path[256]; join_cwd(g_cwd, c->argv[i], path, sizeof(path)); struct fs_file *f = fs_open(path);
        if (!f) { osh_write(c->out, c->out_len, c->out_cap, "cat: no such file\n"); rc=1; continue; }
        size_t want = f->size ? f->size : 0; char *buf = (char*)kmalloc(want+1); if (buf) { ssize_t r = fs_read(f, buf, want, 0); if (r>0) { buf[r]='\0'; osh_write(c->out, c->out_len, c->out_cap, buf); } kfree(buf);} fs_file_free(f);
    }
    return rc;
}

static int bi_mkdir(cmd_ctx *c) { if (c->argc<2) { osh_write(c->out, c->out_len, c->out_cap, "mkdir: missing operand\n"); return 1; } char path[256]; resolve_path(g_cwd, c->argv[1], path, sizeof(path)); int r=ramfs_mkdir(path); return r==0?0:1; }
static int bi_touch(cmd_ctx *c) { if (c->argc<2) { osh_write(c->out, c->out_len, c->out_cap, "touch: missing operand\n"); return 1; } char path[256]; resolve_path(g_cwd, c->argv[1], path, sizeof(path)); struct fs_file *f = fs_create_file(path); if (!f) return 1; fs_file_free(f); return 0; }
static int bi_rm(cmd_ctx *c) { if (c->argc<2) { osh_write(c->out, c->out_len, c->out_cap, "rm: missing operand\n"); return 1; } char path[256]; resolve_path(g_cwd, c->argv[1], path, sizeof(path)); int r=ramfs_remove(path); return r==0?0:1; }

static int bi_about(cmd_ctx *c) { osh_write(c->out,c->out_len,c->out_cap,"AxonOS shell (osh)\n"); return 0; }

static int bi_time(cmd_ctx *c) { rtc_datetime_t dt; rtc_read_datetime(&dt); char out[16]; int pos=0;
    int hh=dt.hour, mm=dt.minute, ss=dt.second;
    out[pos++] = (char)('0' + (hh/10)); out[pos++] = (char)('0' + (hh%10)); out[pos++] = ':';
    out[pos++] = (char)('0' + (mm/10)); out[pos++] = (char)('0' + (mm%10)); out[pos++] = ':';
    out[pos++] = (char)('0' + (ss/10)); out[pos++] = (char)('0' + (ss%10)); out[pos++]='\n'; out[pos]='\0';
    osh_write(c->out,c->out_len,c->out_cap,out); return 0; }

static int bi_date(cmd_ctx *c) { rtc_datetime_t dt; rtc_read_datetime(&dt); char out[64]; int pos=0; int d=dt.day,m=dt.month,y=dt.year; out[pos++]='0'+d/10; out[pos++]='0'+d%10; out[pos++]='/'; out[pos++]='0'+m/10; out[pos++]='0'+m%10; out[pos++]='/'; // year simplified
    // year 4 digits
    int yy = y; char tmp[8]; int n=0; if (yy==0){tmp[n++]='0';} else { int s[8],k=0; while(yy){ s[k++]=yy%10; yy/=10; } for(int i=k-1;i>=0;i--) tmp[n++]=(char)('0'+s[i]); }
    for (int i=0;i<n;i++) out[pos++]=tmp[i]; out[pos++]='\n'; out[pos]='\0'; osh_write(c->out,c->out_len,c->out_cap,out); return 0; }

static int bi_uptime(cmd_ctx *c) { uint64_t seconds = rtc_ticks / 2; uint64_t minutes = seconds / 60; uint64_t hours = minutes / 60; seconds%=60; minutes%=60; char out[64]; int pos=0; // Hh Mm Ss
    int h=(int)hours,m=(int)minutes,s=(int)seconds; char buf[32]; int n=0; // naive int to str
    int v=h; int st[16],k=0; if(v==0) buf[n++]='0'; else { while(v){ st[k++]=v%10; v/=10;} for(int i=k-1;i>=0;i--) buf[n++]=(char)('0'+st[i]); }
    memcpy(out+pos, buf, (size_t)n); pos+=n; out[pos++]='h'; out[pos++]=' ';
    n=0;k=0; v=m; if(v==0) buf[n++]='0'; else { while(v){ st[k++]=v%10; v/=10;} for(int i=k-1;i>=0;i--) buf[n++]=(char)('0'+st[i]); }
    memcpy(out+pos, buf, (size_t)n); pos+=n; out[pos++]='m'; out[pos++]=' ';
    n=0;k=0; v=s; if(v==0) buf[n++]='0'; else { while(v){ st[k++]=v%10; v/=10;} for(int i=k-1;i>=0;i--) buf[n++]=(char)('0'+st[i]); }
    memcpy(out+pos, buf, (size_t)n); pos+=n; out[pos++]='s'; out[pos++]='\n'; out[pos]='\0'; osh_write(c->out,c->out_len,c->out_cap,out); return 0; }

static int bi_edit(cmd_ctx *c) {
    char path[256];
    if (c->argc < 2) {
        join_cwd(g_cwd, "untitled", path, sizeof(path));
    } else {
        join_cwd(g_cwd, c->argv[1], path, sizeof(path));
    }
    editor_run(path);
    return 0;
}
static int bi_snake(cmd_ctx *c){ (void)c; snake_run(); return 0; }
static int bi_tetris(cmd_ctx *c){ (void)c; tetris_run(); return 0; }
static int bi_clock(cmd_ctx *c){ (void)c; clock_run(); return 0; }
extern void reboot_system(); extern void shutdown_system();
static int bi_reboot(cmd_ctx *c){ (void)c; reboot_system(); return 0; }
static int bi_shutdown(cmd_ctx *c){ (void)c; shutdown_system(); return 0; }
static int bi_neofetch(cmd_ctx *c){ (void)c; neofetch_run(); return 0; }

// Run script file: osh <script>
static int bi_osh(cmd_ctx *c) {
    if (c->argc < 2) { osh_write(c->out, c->out_len, c->out_cap, "usage: osh <script>\n"); return 1; }
    char path[256]; resolve_path(g_cwd, c->argv[1], path, sizeof(path));
    struct fs_file *f = fs_open(path); if (!f) { osh_write(c->out, c->out_len, c->out_cap, "osh: cannot open script\n"); return 1; }
    size_t want = f->size ? f->size : 0; char *buf = (char*)kmalloc(want + 1);
    if (!buf) { fs_file_free(f); return 1; }
    ssize_t r = fs_read(f, buf, want, 0); fs_file_free(f);
    if (r < 0) { kfree(buf); osh_write(c->out, c->out_len, c->out_cap, "osh: read error\n"); return 1; }
    buf[r] = '\0';
    // strip UTF-8 BOM if present
    size_t start_off = 0;
    if ((size_t)r >= 3 && (unsigned char)buf[0]==0xEF && (unsigned char)buf[1]==0xBB && (unsigned char)buf[2]==0xBF) {
        start_off = 3;
    }
    // iterate lines (support CRLF/LF), skip comments
    int status = 0; char *line = buf + start_off;
    for (ssize_t i = (ssize_t)start_off; i < r; i++) {
        if (buf[i] == '\r') { buf[i] = '\0'; }
        else if (buf[i] == '\n') { buf[i] = '\0';
            char *s = line; while (*s==' '||*s=='\t') s++;
            if (*s && *s != '#') {
                int rc = exec_line(s);
                if (rc == 2) { status = 0; kfree(buf); return 0; }
                status = rc;
            }
            line = buf + i + 1;
        }
    }
    // last line if not newline-terminated
    if (*line) {
        char *s = line; while (*s==' '||*s=='\t') s++;
        if (*s && *s != '#') { int rc = exec_line(s); if (rc == 2) { status = 0; kfree(buf); return 0; } status = rc; }
    }
    kfree(buf);
    return status;
}
static int bi_pause(cmd_ctx *c){ (void)c; kprintf("Press any key to continue...\n"); kgetc(); return 0;}
static int bi_chipset(cmd_ctx *c) {
    if (c->argc < 2) {
        kprintf("usage: chipset <command>\n");
        kprintf("commands:\n");
        kprintf("  info - print chipset information\n");
        kprintf("  reset - reset chipset\n");
        return 1;
    }
    if (strcmp(c->argv[1], "info") == 0) {
        intel_print_chipset_info();
    } else if (strcmp(c->argv[1], "reset") == 0) {
        intel_chipset_reset();
    } else {
        kprintf("<(0c)>chipset: unknown command: %s\n", c->argv[1]);
        return 1;
    }
    return 0;
}

extern void ascii_art(void);
typedef int (*builtin_fn)(cmd_ctx*);
typedef struct { const char* name; builtin_fn fn; } builtin;
static const builtin builtin_table[] = {
    {"echo", bi_echo}, {"pwd", bi_pwd}, {"cd", bi_cd}, {"clear", bi_cls}, {"cls", bi_cls},
    {"ls", bi_ls}, {"cat", bi_cat}, {"mkdir", bi_mkdir}, {"touch", bi_touch}, {"rm", bi_rm},
    {"about", bi_about}, {"time", bi_time}, {"date", bi_date}, {"uptime", bi_uptime},
    {"edit", bi_edit}, {"snake", bi_snake}, {"tetris", bi_tetris}, {"clock", bi_clock},
    {"reboot", bi_reboot}, {"shutdown", bi_shutdown}, {"neofetch", bi_neofetch},
    {"osh", bi_osh}, {"art", ascii_art}, {"pause", bi_pause}, {"chipset", bi_chipset},
};

static builtin_fn find_builtin(const char* name) {
    for (size_t i=0;i<sizeof(builtin_table)/sizeof(builtin_table[0]);i++) if (strcmp(builtin_table[i].name, name)==0) return builtin_table[i].fn;
    return NULL;
}

// -------- executor --------
static int exec_simple(char **argv, int argc, const char *in, char **out, size_t *out_len, size_t *out_cap) {
    if (argc==0) return 0;
    if (strcmp(argv[0], "exit") == 0) return 2; // special code to exit shell loop
    builtin_fn fn = find_builtin(argv[0]);
    if (!fn) { kprintf("<(0c)>osh: %s: command not found\n", argv[0]); return 1; }
    cmd_ctx c = { .argv=argv, .argc=argc, .in=in, .out=out, .out_len=out_len, .out_cap=out_cap };
    return fn(&c);
}

static int exec_pipeline(token *toks, int l, int r, const char *stdin_data, char **out_buf, size_t *out_len, size_t *out_cap) {
    // collect redirections > and < at end or anywhere: last > wins, single < for first stage
    char *redir_out = NULL; char *redir_in = NULL; int last_pipe = -1; int bg = 0;
    // find & at end
    if (r>l && toks[r-1].t==T_BG) { bg = 1; r--; }
    // scan for redirs and pipes
    int parts_idx[16]; int parts_count=0; int i=l; int start=l;
    while (i<r) {
        if (toks[i].t==T_PIPE) { parts_idx[parts_count++] = start; parts_idx[parts_count++] = i; start = i+1; }
        else if (toks[i].t==T_GT) { if (i+1<r && toks[i+1].t==T_WORD) { redir_out = toks[i+1].s; } i+=1; }
        else if (toks[i].t==T_LT) { if (!redir_in && i+1<r && toks[i+1].t==T_WORD) { redir_in = toks[i+1].s; } i+=1; }
        i++;
    }
    parts_idx[parts_count++] = start; parts_idx[parts_count++] = r;
    // initial stdin
    char *stage_in = NULL; size_t stage_in_len = 0;
    if (redir_in) {
        char path[256]; resolve_path(g_cwd, redir_in, path, sizeof(path)); struct fs_file *f = fs_open(path);
        if (f) { size_t want = f->size ? f->size : 0; stage_in = (char*)kmalloc(want+1); if (stage_in) { ssize_t rd = fs_read(f, stage_in, want, 0); if (rd>0){ stage_in[rd]='\0'; stage_in_len=(size_t)rd;} else { stage_in[0]='\0'; stage_in_len=0; } } fs_file_free(f); }
    } else if (stdin_data) {
        size_t n = strlen(stdin_data); stage_in = (char*)kmalloc(n+1); if (stage_in){ memcpy(stage_in, stdin_data, n+1); stage_in_len=n; }
    }
    // Execute stages left-to-right
    char *cur_out = NULL; size_t cur_len=0, cur_cap=0;
    for (int pi=0; pi<parts_count; pi+=2) {
        int pl = parts_idx[pi], pr = parts_idx[pi+1];
        // build argv from [pl,pr)
        char *argv[32]; int argc=0;
        for (int j=pl; j<pr; j++) if (toks[j].t==T_WORD) { argv[argc++] = toks[j].s; if (argc>=31) break; }
        argv[argc] = NULL;
        // prepare output buffer for this stage
        char *stage_out = NULL; size_t stage_out_len=0, stage_out_cap=0;
        const char *use_in = stage_in ? stage_in : NULL;
        int rc = exec_simple(argv, argc, use_in, &stage_out, &stage_out_len, &stage_out_cap);
        if (stage_in) { kfree(stage_in); stage_in=NULL; }
        if (rc == 2) { // exit shell
            if (stage_out) kfree(stage_out);
            if (cur_out) kfree(cur_out);
            return 2;
        }
        // Next stage input becomes this output
        stage_in = stage_out; stage_in_len = stage_out_len;
        if (pi+2 >= parts_count) { cur_out = stage_out; cur_len = stage_out_len; cur_cap = stage_out_cap; }
    }
    // If printing to screen (no redirection), and final output contains color tags,
    // print immediately in color and suppress returning text to caller.
    if (!redir_out && cur_out && cur_len > 0) {
        int has_color = 0; for (size_t i=0;i+1<cur_len;i++){ if (cur_out[i]=='<' && cur_out[i+1]=='('){ has_color=1; break; } }
        if (has_color) {
            size_t need = cur_len + 8; // for parser safety
            char *tmp = (char*)kmalloc(need);
            if (tmp) { memcpy(tmp, cur_out, cur_len); memset(tmp+cur_len, 0, 8); kprint_colorized(tmp); kprint((uint8_t*)"\n"); kfree(tmp); }
            else { // fallback: print plainly
                char *plain = (char*)kmalloc(cur_len+1); if (plain) { memcpy(plain, cur_out, cur_len); plain[cur_len]='\0'; kprint((uint8_t*)plain); kprint((uint8_t*)"\n"); kfree(plain); }
            }
        suppress_return:
            kfree(cur_out); cur_out=NULL; cur_len=0; cur_cap=0;
        }
    }

    // output redirection or return text
    if (redir_out) {
        char path[256]; resolve_path(g_cwd, redir_out, path, sizeof(path)); struct fs_file *f = fs_open(path); if (!f) f = fs_create_file(path);
        if (f) { if (cur_out) fs_write(f, cur_out, cur_len, 0); fs_file_free(f); }
        if (cur_out) { kfree(cur_out); cur_out=NULL; cur_len=0; cur_cap=0; }
    } else {
        // pass back to caller
        *out_buf = cur_out; *out_len = cur_len; *out_cap = cur_cap; cur_out=NULL;
    }
    return 0;
}

static int exec_line(const char *line) {
    int tn=0; token *t = lex(line, &tn); if (tn==0) { if (t) kfree(t); return 0; }
    int i=0; int status=0; // 0 success, non-zero fail; exit=2
    while (i < tn) {
        int j = i;
        while (j < tn && t[j].t != T_AND && t[j].t != T_OR) j++;
        // execute segment [i,j)
        char *out=NULL; size_t out_len=0,out_cap=0;
        int rc = exec_pipeline(t, i, j, NULL, &out, &out_len, &out_cap);
        if (rc == 2) { free_tokens(t, tn); if (out) kfree(out); return 2; }
        status = rc;
        // print to screen if there is output (and not redirected)
        if (out && out_len > 0) {
            // ensure 0-termination using a small temporary buffer to avoid heap resizing stalls
            char *plain_tmp = (char*)kmalloc(out_len + 1);
            if (plain_tmp) { memcpy(plain_tmp, out, out_len); plain_tmp[out_len] = '\0'; }
            // If output contains Axon color tags <(...)>, use colorized printer;
            // otherwise use plain kprint. Do not interpret when redirected/ piped.
            int use_color = 0;
            for (size_t ci = 0; ci + 1 < out_len; ci++) {
                if (out[ci] == '<' && out[ci+1] == '(') { use_color = 1; break; }
            }
            if (use_color) {
                // print from temporary padded buffer (avoid heap resizing)
                char *tmp = (char*)kmalloc(out_len + 8);
                if (tmp) { memcpy(tmp, out, out_len); memset(tmp + out_len, 0, 8); kprint_colorized(tmp); kfree(tmp); }
                else if (plain_tmp) { kprint((uint8_t*)plain_tmp); }
            } else {
                if (plain_tmp) { kprint((uint8_t*)plain_tmp); }
            }
            if (out[out_len-1] != '\n') kprint((uint8_t*)"\n");
            if (plain_tmp) kfree(plain_tmp);
            kfree(out); out=NULL;
        }
        if (j == tn) break;
        if (t[j].t == T_AND) { if (status != 0) { // skip next until next AND/OR
                i = j + 1; // move over operator; but still execute next; AND means execute only if success; we already failed => skip one segment?
                // Skip one segment
                int k = j+1; while (k<tn && t[k].t != T_AND && t[k].t != T_OR) k++; i = k; continue; } }
        else if (t[j].t == T_OR) { if (status == 0) { int k = j+1; while (k<tn && t[k].t != T_AND && t[k].t != T_OR) k++; i = k; continue; } }
        i = j + 1;
    }
    free_tokens(t, tn);
    return status;
}

// -------- background jobs (minimal) --------
typedef struct job { char *line; struct job *next; } job;
static job *jobs_head = NULL;
static void job_push(const char *line) { job *j=(job*)kmalloc(sizeof(job)); size_t n=strlen(line); j->line=(char*)kmalloc(n+1); memcpy(j->line,line,n+1); j->next=jobs_head; jobs_head=j; }
static job* job_pop(void) { job *j=jobs_head; if (j) jobs_head=j->next; return j; }

static void bg_thread_entry(void) {
    job *j = job_pop(); if (!j) return; (void)exec_line(j->line); kfree(j->line); kfree(j);
}

// -------- public loop --------
void osh_run(void) {
    static char buf[512];
    for (;;) {
        char prompt[300]; prompt[0]='\0';
        strncat(prompt, g_cwd, sizeof(prompt)-1);
        strncat(prompt, "> ", sizeof(prompt)-1 - strlen(prompt));
        kprint((uint8_t*)prompt);
        char *line = kgets(buf, (int)sizeof(buf)); if (!line) continue;
        // Detect trailing background '&' at top level quickly; if present -> spawn job thread
        int tn=0; token *t = lex(line, &tn); if (tn==0) { if (t) kfree(t); continue; }
        int bg = (t[tn-1].t == T_BG);
        free_tokens(t, tn);
        if (bg) { job_push(line); thread_create(bg_thread_entry, "bg"); continue; }
        int rc = exec_line(line); if (rc == 2) break; // exit
    }
}

// ---- small helpers for other subsystems ----
void osh_get_cwd(char* out, unsigned long outlen) {
    if (!out || outlen==0) return; if (outlen<2) { out[0]='\0'; return; }
    strncpy(out, g_cwd, (size_t)outlen-1); out[outlen-1] = '\0';
}

void osh_resolve_path(const char* base, const char* arg, char* out, unsigned long outlen) {
    if (!out || outlen==0) return; if (!arg) { out[0]='\0'; return; }
    if (arg[0]=='/') { strncpy(out, arg, (size_t)outlen-1); out[outlen-1]='\0'; return; }
    const char* b = base ? base : g_cwd; resolve_path(b, arg, out, (size_t)outlen);
}


