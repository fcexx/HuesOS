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
#include "../inc/osh_line.h"
// local prototype for kprintf
void kprintf(const char* fmt, ...);
// forward decls for optional chipset commands
void intel_print_chipset_info(void);
void intel_chipset_reset(void);
#include "../inc/thread.h"
#include "../inc/editor.h"
#include "../inc/snake.h"
#include "../inc/tetris.h"
#include "../inc/clock.h"
#include "../inc/neofetch.h"
#include "../inc/sysinfo.h"

typedef long ssize_t;

// forward declare minimal RTC API to avoid include issues
typedef struct { uint8_t second, minute, hour, day, month; uint16_t year; } rtc_datetime_t;
extern volatile uint64_t rtc_ticks;
void rtc_read_datetime(rtc_datetime_t* dt);

// -------- util --------
static char g_cwd[256] = "/";

// -------- variables (simple key->string store) --------
typedef struct { char name[32]; char* value; } osh_var;
static osh_var g_vars[128];
static int g_var_count = 0;

static int is_var_name_char1(char c){ return (c=='_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z')); }
static int is_var_name_char(char c){ return (c=='_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9')); }

static int is_valid_varname(const char* s){
    if (!s||!s[0]) return 0;
    if (!is_var_name_char1(s[0])) return 0;
    for (int i=1;s[i];i++) if (!is_var_name_char(s[i])) return 0;
    return 1;
}

static int var_lookup(const char* name, const char** out_val){
    if (!name) return 0;
    for (int i=0;i<g_var_count;i++){
        if (strcmp(g_vars[i].name, name)==0){
            if (out_val) *out_val = g_vars[i].value ? g_vars[i].value : "";
            return 1;
        }
    }
    if (out_val) *out_val = "";
    return 0;
}

static const char* var_get(const char* name){
    const char* val = "";
    (void)var_lookup(name, &val);
    return val;
}

static void var_set(const char* name, const char* value){
    if (!name) return;
    for (int i=0;i<g_var_count;i++){
        if (strcmp(g_vars[i].name, name)==0){
            if (g_vars[i].value) kfree(g_vars[i].value);
            size_t n = value ? strlen(value) : 0;
            g_vars[i].value = (char*)kcalloc(n+1,1);
            if (g_vars[i].value && value) memcpy(g_vars[i].value, value, n);
            return;
        }
    }
    if (g_var_count < (int)(sizeof(g_vars)/sizeof(g_vars[0]))){
        strncpy(g_vars[g_var_count].name, name, sizeof(g_vars[g_var_count].name)-1);
        g_vars[g_var_count].name[sizeof(g_vars[g_var_count].name)-1]='\0';
        size_t n = value ? strlen(value) : 0;
        g_vars[g_var_count].value = (char*)kcalloc(n+1,1);
        if (g_vars[g_var_count].value && value) memcpy(g_vars[g_var_count].value, value, n);
        g_var_count++;
    }
}

static char* expand_vars(const char* in) {
    if (!in) return NULL;
    size_t n = strlen(in);
    size_t cap = n + 1;
    char* out = (char*)kmalloc(cap);
    if (!out) return NULL;
    size_t oi=0;
    for (size_t i=0;i<n;i++){
        if (in[i] == '$' && i+1 < n && is_var_name_char1(in[i+1])) {
            size_t j = i+1;
            char name[32]; int k=0;
            while (j<n && is_var_name_char(in[j]) && k < (int)sizeof(name)-1) name[k++] = in[j++];
            name[k]='\0';
            const char* val = var_get(name);
            size_t vl = strlen(val);
            // ensure capacity
            if (oi + vl + 1 > cap) {
                size_t ncap = cap; while (oi + vl + 1 > ncap) ncap *= 2;
                char* nb = (char*)kmalloc(ncap);
                if (!nb) { kfree(out); return NULL; }
                if (oi) memcpy(nb, out, oi);
                kfree(out); out = nb; cap = ncap;
            }
            if (vl) { memcpy(out+oi, val, vl); oi += vl; }
            i = j - 1;
            continue;
        }
        if (oi + 2 > cap) {
            size_t ncap = cap*2; char* nb=(char*)kmalloc(ncap); if (!nb){ kfree(out); return NULL; }
            if (oi) memcpy(nb, out, oi); kfree(out); out=nb; cap=ncap;
        }
        out[oi++] = in[i];
    }
    if (oi < cap) out[oi] = '\0'; else { char* nb=(char*)kmalloc(cap+1); if(nb){ memcpy(nb,out,cap); nb[cap]='\0'; kfree(out); out=nb; } }
    return out;
}

// Replace bare identifiers with their variable values (for conditions like "a == 2")
static char* osh_expand_idents(const char* in) {
    if (!in) return NULL;
    size_t cap = strlen(in) + 1;
    char* out = (char*)kmalloc(cap);
    if (!out) return NULL;
    size_t oi = 0;
    for (size_t i=0; in[i]; ) {
        char c = in[i];
        if (is_var_name_char1(c)) {
            // collect identifier
            char name[32]; int ni=0;
            size_t j=i;
            while (in[j] && is_var_name_char(in[j]) && ni < (int)sizeof(name)-1) { name[ni++] = in[j++]; }
            name[ni]='\0';
            const char* val = NULL;
            if (var_lookup(name, &val)) {
                size_t vl = val ? strlen(val) : 0;
                if (oi + vl + 1 > cap) {
                    size_t ncap = cap; while (oi + vl + 1 > ncap) ncap *= 2;
                    char* nb = (char*)kmalloc(ncap);
                    if (!nb) { kfree(out); return NULL; }
                    if (oi) memcpy(nb, out, oi);
                    kfree(out); out = nb; cap = ncap;
                }
                if (vl) { memcpy(out+oi, val, vl); oi += vl; }
                i = j;
                continue;
            } else {
                size_t vl = (size_t)ni;
                if (oi + vl + 1 > cap) {
                    size_t ncap = cap; while (oi + vl + 1 > ncap) ncap *= 2;
                    char* nb = (char*)kmalloc(ncap);
                    if (!nb) { kfree(out); return NULL; }
                    if (oi) memcpy(nb, out, oi);
                    kfree(out); out = nb; cap = ncap;
                }
                if (vl) { memcpy(out+oi, name, vl); oi += vl; }
                i = j;
                continue;
            }
        }
        // copy single char
        if (oi + 2 > cap) {
            size_t ncap = cap*2; char* nb=(char*)kmalloc(ncap); if (!nb){ kfree(out); return NULL; }
            if (oi) memcpy(nb, out, oi); kfree(out); out=nb; cap=ncap;
        }
        out[oi++] = c; i++;
    }
    if (oi < cap) out[oi] = '\0'; else { char* nb=(char*)kmalloc(cap+1); if(nb){ memcpy(nb,out,cap); nb[cap]='\0'; kfree(out); out=nb; } }
    return out;
}

static char* osh_dup_trim(const char* src) {
    if (!src) return (char*)kcalloc(1,1);
    while (*src==' '||*src=='\t') src++;
    size_t len = strlen(src);
    while (len>0 && (src[len-1]==' '||src[len-1]=='\t')) len--;
    char* out = (char*)kcalloc(len+1,1);
    if (!out) return NULL;
    if (len) memcpy(out, src, len);
    out[len]='\0';
    return out;
}

static double osh_parse_expr(const char** ps);

static void trim_spaces(char* s) {
    if (!s) return;
    char* p = s;
    while (*p==' '||*p=='\t') p++;
    if (p != s) {
        size_t rem = strlen(p);
        memmove(s, p, rem + 1);
    }
    size_t len = strlen(s);
    while (len>0 && (s[len-1]==' '||s[len-1]=='\t')) s[--len]='\0';
}

static void strip_matching_quotes(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    if (len >= 2) {
        char first = s[0];
        char last = s[len-1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            memmove(s, s + 1, len - 2);
            s[len - 2] = '\0';
        }
    }
}

static int osh_try_parse_number(const char* text, double* out_val) {
    if (!text) return 0;
    const char* p = text;
    double v = osh_parse_expr(&p);
    while (*p==' '||*p=='\t') p++;
    if (*p != '\0') return 0;
    if (out_val) *out_val = v;
    return 1;
}

static int line_is_brace_only(const char* s) {
    if (!s) return 0;
    while (*s==' '||*s=='\t') s++;
    if (!*s) return 0;
    for (const char* p = s; *p; p++) {
        if (!(*p=='{' || *p=='}' || *p==' ' || *p=='\t')) return 0;
    }
    return 1;
}

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
            char *ws = expand_vars(buf); if (!ws) { ws = (char*)kmalloc((size_t)bi+1); memcpy(ws, buf, (size_t)bi+1); }
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
        char *ws = expand_vars(buf); if (!ws) { ws = (char*)kmalloc((size_t)bi+1); memcpy(ws, buf, (size_t)bi+1); }
        if (n==cap){cap*=2; v=(token*)krealloc(v, (size_t)cap*sizeof(token));}
        v[n++] = (token){T_WORD, ws}; (void)start;
    }
    *out_n = n; return v;
}

static void free_tokens(token *v, int n) { for (int i=0;i<n;i++) if (v[i].t==T_WORD && v[i].s) kfree(v[i].s); kfree(v); }

// Forward declaration for script engine to call command executor
static int exec_line(const char *line);

// -------- simple arithmetic parser for assignments (double) --------
static double osh_parse_expr(const char** ps);
static double osh_parse_number(const char** ps){
    double v=0.0;
    while (**ps==' '||**ps=='\t') (*ps)++;
    int neg = 0;
    if (**ps=='+' || **ps=='-') { if (**ps=='-') neg=1; (*ps)++; }
    // integer part
    while (**ps>='0'&&**ps<='9'){ v = v*10.0 + (double)(**ps - '0'); (*ps)++; }
    // fractional part
    if (**ps=='.'){ (*ps)++; double base = 0.1; while (**ps>='0'&&**ps<='9'){ v += base * (double)(**ps - '0'); base *= 0.1; (*ps)++; } }
    if (neg) v = -v;
    while (**ps==' '||**ps=='\t') (*ps)++;
    return v;
}
static double osh_parse_factor(const char** ps){
    while (**ps==' '||**ps=='\t') (*ps)++;
    if (**ps=='('){
        (*ps)++;
        double v=osh_parse_expr(ps);
        while (**ps==' '||**ps=='\t') (*ps)++;
        if (**ps==')') (*ps)++;
        while (**ps==' '||**ps=='\t') (*ps)++;
        return v;
    }
    return osh_parse_number(ps);
}
static double osh_parse_term(const char** ps){
    double v=osh_parse_factor(ps);
    for(;;){
        while (**ps==' '||**ps=='\t') (*ps)++;
        if (**ps=='*'){ (*ps)++; double r=osh_parse_factor(ps); v*=r; }
        else if (**ps=='/'){ (*ps)++; double r=osh_parse_factor(ps); if (r!=0.0) v/=r; }
        else break;
    }
    return v;
}
static double osh_parse_expr(const char** ps){
    double v=osh_parse_term(ps);
    for(;;){
        while (**ps==' '||**ps=='\t') (*ps)++;
        if (**ps=='+'){ (*ps)++; double r=osh_parse_term(ps); v+=r; }
        else if (**ps=='-'){ (*ps)++; double r=osh_parse_term(ps); v-=r; }
        else break;
    }
    return v;
}

static void osh_double_to_str(double val, char* out, size_t outsz){
    if (outsz==0) return;
    // handle NaN/Inf not expected; simple fixed 6 decimals, trim zeros
    long long ip = (long long)(val);
    double frac = val - (double)ip;
    if (frac < 0) frac = -frac;
    // convert integer part
    char ibuf[64]; int in=0; long long t = ip; int neg = 0;
    if (t==0){ ibuf[in++]='0'; }
    else {
        if (t<0){ neg=1; t = -t; }
        char tmp[32]; int k=0; while (t){ tmp[k++] = (char)('0' + (t%10)); t/=10; }
        if (neg) ibuf[in++]='-';
        for (int i=k-1;i>=0;i--) ibuf[in++]=tmp[i];
    }
    ibuf[in]='\0';
    // fractional with 6 digits
    int fd = (int)(frac*1000000.0 + 0.5);
    char fbuf[16]; int fn=0;
    if (fd>0){
        // write 6 digits with leading zeros
        char tmp[8]; for (int i=5;i>=0;i--){ tmp[i] = (char)('0' + (fd%10)); fd/=10; }
        int end=5; while (end>=0 && tmp[end]=='0') end--;
        if (end>=0){
            fbuf[fn++]='.';
            for (int i=0;i<=end;i++) fbuf[fn++]=tmp[i];
        }
    }
    fbuf[fn]='\0';
    // combine
    size_t need = (size_t)in + (size_t)fn + 1;
    size_t pos=0;
    if (outsz>0){
        size_t ci = (size_t)in < outsz-1 ? (size_t)in : outsz-1; if (ci){ memcpy(out+pos, ibuf, ci); pos+=ci; }
        size_t cf = (size_t)fn < (outsz-1-pos) ? (size_t)fn : (outsz-1-pos); if (cf){ memcpy(out+pos, fbuf, cf); pos+=cf; }
        out[pos]='\0';
    }
}

// ---- Simple script engine for bi_osh ----
typedef struct { char name[32]; char *params[8]; int pc; int header; int start; int end; } osh_func_def;
typedef struct {
    char **lines;
    int nlines;
    osh_func_def *funcs;
    int nfuncs;
} osh_script_ctx;

typedef struct {
    char* cond;        // NULL indicates unconditional else branch
    int body_start;    // inclusive (valid when inline_cmd == NULL)
    int body_end;      // exclusive (line index of closing brace)
    char* inline_cmd;  // non-NULL for single-line bodies
} osh_if_branch;

static int g_script_depth = 0;

#define OSH_SCRIPT_OK 0
#define OSH_SCRIPT_EXIT 100
#define OSH_SCRIPT_ABORT 101

static int osh_find_func(osh_script_ctx* C, const char* nm) {
    for (int i=0;i<C->nfuncs;i++) if (strcmp(C->funcs[i].name, nm)==0) return i; return -1;
}

static int osh_eval_cond(const char* expr) {
    char *ex = expand_vars(expr);
    const char* e0 = ex ? ex : expr;
    char *ei = osh_expand_idents(e0);
    const char* e = ei ? ei : e0;

    int which = -1, pos = -1, oplen = 0;
    if (e) {
        for (int i = 0; e[i]; i++) {
            char c = e[i];
            char n = e[i+1];
            if (c=='=' && n=='=') { which = 0; pos = i; oplen = 2; break; }
            if (c=='!' && n=='=') { which = 1; pos = i; oplen = 2; break; }
            if (c=='<' && n=='=') { which = 2; pos = i; oplen = 2; break; }
            if (c=='>' && n=='=') { which = 3; pos = i; oplen = 2; break; }
            if (c=='<') { which = 4; pos = i; oplen = 1; break; }
            if (c=='>') { which = 5; pos = i; oplen = 1; break; }
        }
    }

    int res = 0;
    if (which >= 0) {
        char left[256], right[256];
        int ln = pos; if (ln > 255) ln = 255;
        memcpy(left, e, (size_t)ln); left[ln]='\0';
        const char* rp = e + pos + oplen;
        while (*rp==' '||*rp=='\t') rp++;
        strncpy(right, rp, sizeof(right)-1); right[sizeof(right)-1]='\0';
        trim_spaces(left);
        trim_spaces(right);

        double lv = 0.0, rv = 0.0;
        int left_is_num = osh_try_parse_number(left, &lv);
        int right_is_num = osh_try_parse_number(right, &rv);
        int both_numeric = left_is_num && right_is_num;

        if (which == 0 || which == 1) {
            if (both_numeric) {
                double eps = 1e-9;
                res = (which == 0)
                    ? ((lv - rv < eps) && (rv - lv < eps))
                    : !((lv - rv < eps) && (rv - lv < eps));
            } else {
                char ls[256], rs[256];
                strncpy(ls, left, sizeof(ls)-1); ls[sizeof(ls)-1]='\0';
                strncpy(rs, right, sizeof(rs)-1); rs[sizeof(rs)-1]='\0';
                strip_matching_quotes(ls);
                strip_matching_quotes(rs);
                int cmp = strcmp(ls, rs);
                res = (which == 0) ? (cmp == 0) : (cmp != 0);
            }
        } else {
            if (both_numeric) {
                double eps = 1e-9;
                switch (which) {
                    case 2: res = (lv <= rv + eps); break;
                    case 3: res = (lv + eps >= rv); break;
                    case 4: res = (lv < rv - eps); break;
                    case 5: res = (lv > rv + eps); break;
                }
            } else {
                char ls[256], rs[256];
                strncpy(ls, left, sizeof(ls)-1); ls[sizeof(ls)-1]='\0';
                strncpy(rs, right, sizeof(rs)-1); rs[sizeof(rs)-1]='\0';
                strip_matching_quotes(ls);
                strip_matching_quotes(rs);
                int cmp = strcmp(ls, rs);
                switch (which) {
                    case 2: res = (cmp <= 0); break;
                    case 3: res = (cmp >= 0); break;
                    case 4: res = (cmp < 0); break;
                    case 5: res = (cmp > 0); break;
                }
            }
        }
    } else {
        res = (e && *e);
    }

    if (ei) kfree(ei);
    if (ex) kfree(ex);
    return res;
}

static int osh_exec_range(osh_script_ctx* C, int L, int R);

// -------- block parsing helpers (robust, single-pass) --------
static int osh_find_block(osh_script_ctx* C, int from_line, int *out_body_start, int *out_end_line) {
    if (!C || from_line < 0 || from_line >= C->nlines) return -1;
    int depth = 0;
    int saw_open = 0;
    int body_start = -1;
    for (int l = from_line; l < C->nlines; l++) {
        char *s = C->lines[l];
        if (!s) continue;
        for (char *p = s; *p; p++) {
            if (*p == '{') {
                depth++;
                if (!saw_open) {
                    saw_open = 1;
                    body_start = l + 1;
                }
            } else if (*p == '}') {
                if (depth > 0) depth--;
                if (saw_open && depth == 0) {
                    if (out_body_start) *out_body_start = body_start;
                    if (out_end_line) *out_end_line = l;
                    return 0;
                }
            }
        }
    }
    return -1; // not found
}

static void osh_extract_condition(const char* src, char* out, size_t cap) {
    if (!out || cap == 0) return;
    size_t idx = 0;
    if (src) {
        const char *p = src;
        while (*p==' '||*p=='\t') p++;
        while (*p && *p!='{' && idx + 1 < cap) {
            out[idx++] = *p++;
        }
    }
    out[idx] = '\0';
    while (idx>0 && (out[idx-1]==' '||out[idx-1]=='\t')) out[--idx]='\0';
}

static char* osh_extract_inline_cmd(const char* line) {
    if (!line) return (char*)kcalloc(1,1);
    const char *open = strchr(line, '{');
    const char *close = open ? strchr(open + 1, '}') : NULL;
    if (!open || !close || close <= open + 1) return (char*)kcalloc(1,1);
    size_t len = (size_t)(close - (open + 1));
    char *tmp = (char*)kcalloc(len + 1, 1);
    if (!tmp) return NULL;
    memcpy(tmp, open + 1, len);
    tmp[len] = '\0';
    char *trimmed = osh_dup_trim(tmp);
    kfree(tmp);
    if (!trimmed) trimmed = (char*)kcalloc(1,1);
    return trimmed;
}

static int osh_collect_if_branches(osh_script_ctx* C, int header_line, osh_if_branch* branches, int max_branches, int *out_next_line) {
    if (!C || !branches || max_branches <= 0) return 0;
    char cond_buf[256];
    const char *line = C->lines[header_line];
    const char *after_if = NULL;
    if (line) {
        const char *trim = line;
        while (*trim==' '||*trim=='\t') trim++;
        if (strncmp(trim, "if", 2) == 0) {
            after_if = trim + 2;
        }
    }
    osh_extract_condition(after_if, cond_buf, sizeof(cond_buf));
    char *cond_dup = osh_dup_trim(cond_buf);
    if (!cond_dup) cond_dup = (char*)kcalloc(1,1);

    int bstart=-1, bend=-1;
    if (osh_find_block(C, header_line, &bstart, &bend) != 0) {
        if (cond_dup) kfree(cond_dup);
        return 0;
    }

    int count = 0;
    char *inline_cmd = NULL;
    if (bstart > bend) inline_cmd = osh_extract_inline_cmd(C->lines[header_line]);
    branches[count++] = (osh_if_branch){cond_dup, bstart, bend, inline_cmd};

    int scan = bend + 1;
    while (scan < C->nlines && count < max_branches) {
        line = C->lines[scan];
        if (!line) { scan++; continue; }
        const char *trim = line;
        while (*trim==' '||*trim=='\t') trim++;
        // skip lines that are purely closing braces
        int only_closing = 1;
        for (const char *q=line; *q; q++) {
            if (*q!=' ' && *q!='\t' && *q!='}') { only_closing = 0; break; }
        }
        if (only_closing) { scan++; continue; }
        while (*trim=='}') trim++;
        while (*trim==' '||*trim=='\t') trim++;

        if (strncmp(trim, "else if", 7) == 0) {
            char cond_buf2[256];
            osh_extract_condition(trim + 7, cond_buf2, sizeof(cond_buf2));
            char *dup = osh_dup_trim(cond_buf2);
            if (!dup) dup = (char*)kcalloc(1,1);
            if (osh_find_block(C, scan, &bstart, &bend) != 0) { if (dup) kfree(dup); scan++; continue; }
            char *inline_if = NULL;
            if (bstart > bend) inline_if = osh_extract_inline_cmd(line);
            branches[count++] = (osh_if_branch){dup, bstart, bend, inline_if};
            scan = bend + 1;
            continue;
        } else if (strncmp(trim, "else", 4) == 0) {
            if (osh_find_block(C, scan, &bstart, &bend) != 0) { scan++; continue; }
            char *inline_else = NULL;
            if (bstart > bend) inline_else = osh_extract_inline_cmd(line);
            branches[count++] = (osh_if_branch){NULL, bstart, bend, inline_else};
            scan = bend + 1;
            break;
        } else {
            break;
        }
    }
    if (out_next_line) *out_next_line = scan;
    return count;
}
static int osh_call_func(osh_script_ctx* C, int fi, char** args, int ac) {
    const int pc = C->funcs[fi].pc;
    const char* saved_names[8];
    char* saved_vals[8];
    for (int i=0;i<pc && i<8;i++){
        const char* pname = C->funcs[fi].params[i];
        saved_names[i] = pname;
        char* copy = NULL;
        const char* old = var_get(pname);
        if (old) {
            size_t len = strlen(old);
            copy = (char*)kcalloc(len+1,1);
            if (copy) memcpy(copy, old, len+1);
        }
        saved_vals[i] = copy;
        var_set(pname, (i<ac && args[i]) ? args[i] : "");
    }
    int rc = osh_exec_range(C, C->funcs[fi].start, C->funcs[fi].end);
    for (int i=0;i<pc && i<8;i++){
        const char* pname = saved_names[i];
        if (saved_vals[i]) { var_set(pname, saved_vals[i]); kfree(saved_vals[i]); }
        else var_set(pname, "");
    }
    return rc;
}

static int osh_exec_range(osh_script_ctx* C, int L, int R) {
    int li = L;
    while (li < R) {
        if (keyboard_ctrlc_pending()) {
            keyboard_consume_ctrlc();
            return OSH_SCRIPT_ABORT;
        }
        int prev_li = li;
        int skipped_def = 0;
        for (int fi=0; fi<C->nfuncs; fi++) {
            if (li == C->funcs[fi].header) {
                li = C->funcs[fi].end + 1;
                skipped_def = 1;
                break;
            }
        }
        if (skipped_def) continue;
        char *s0 = C->lines[li];
        while (*s0==' '||*s0=='\t') s0++;
        if (!*s0 || *s0=='#') { li++; continue; }
        if (line_is_brace_only(s0)) { li++; continue; }

        if (strncmp(s0, "if ", 3)==0) {
            osh_if_branch branches[16];
            int next_line = li + 1;
            int count = osh_collect_if_branches(C, li, branches, 16, &next_line);
            if (count <= 0) { li++; continue; }
            int exec_rc = OSH_SCRIPT_OK;
            for (int bi=0; bi<count; bi++) {
                int cond_true = (!branches[bi].cond) ? 1 : osh_eval_cond(branches[bi].cond);
                if (cond_true) {
                    if (branches[bi].inline_cmd && branches[bi].inline_cmd[0]) {
                        int rc_line = exec_line(branches[bi].inline_cmd);
                        if (rc_line == OSH_SCRIPT_EXIT) exec_rc = OSH_SCRIPT_EXIT;
                        else if (rc_line == 2) exec_rc = OSH_SCRIPT_EXIT;
                        else if (rc_line == OSH_SCRIPT_ABORT) exec_rc = OSH_SCRIPT_ABORT;
                    } else if (!branches[bi].inline_cmd) {
                        exec_rc = osh_exec_range(C, branches[bi].body_start, branches[bi].body_end);
                    }
                    break;
                }
            }
            for (int bi=0; bi<count; bi++) {
                if (branches[bi].cond) kfree(branches[bi].cond);
                if (branches[bi].inline_cmd) kfree(branches[bi].inline_cmd);
            }
            li = next_line;
            if (exec_rc != OSH_SCRIPT_OK) return exec_rc;
            continue;
        }

        if (strncmp(s0, "while ", 6)==0) {
            char *cond = s0 + 6;
            char condw_buf[256]; int wi=0; char *pwc=cond;
            while (*pwc && *pwc!='{' && wi<255) { condw_buf[wi++]=*pwc++; }
            condw_buf[wi]='\0';
            int wl = (int)strlen(condw_buf);
            while (wl>0 && (condw_buf[wl-1]==' '||condw_buf[wl-1]=='\t')) condw_buf[--wl]='\0';
            int woff = 0; while (condw_buf[woff]==' '||condw_buf[woff]=='\t') woff++;
            char *openb = strchr(s0, '{');
            char *closeb = openb ? strchr(openb, '}') : NULL;
            if (openb && closeb && closeb > openb) {
                int guard = 0;
                while (osh_eval_cond(condw_buf + woff)) {
                    if (keyboard_ctrlc_pending()) {
                        keyboard_consume_ctrlc();
                        return OSH_SCRIPT_ABORT;
                    }
                    char inner[512];
                    int ilen = (int)(closeb - (openb + 1));
                    if (ilen >= (int)sizeof(inner)) ilen = (int)sizeof(inner) - 1;
                    if (ilen > 0) {
                        memcpy(inner, openb + 1, (size_t)ilen);
                        inner[ilen] = '\0';
                        int rc_line = exec_line(inner);
                        if (rc_line == OSH_SCRIPT_EXIT) return OSH_SCRIPT_EXIT;
                        if (rc_line == 2) return OSH_SCRIPT_EXIT;
                        if (rc_line == OSH_SCRIPT_ABORT) return OSH_SCRIPT_ABORT;
                    }
                    if (++guard > 100000) break;
                }
                li++;
                continue;
            }
            int depth=0, bstart=-1, bend=-1, cur=li, started=0;
            for (; cur<R; cur++) {
                char *t = C->lines[cur]; while (*t==' '||*t=='\t') t++;
                for (char* q=t; *q; q++){
                    if(*q=='{'){
                        depth++;
                        if (!started) { started=1; bstart=cur+1; }
                    } else if(*q=='}'){
                        if (depth>0) {
                            depth--;
                            if (started && depth==0){ bend=cur; goto got_while_block; }
                        }
                    }
                }
            }
        got_while_block:
            if (bstart < 0 || bend < 0) { li++; continue; }
            int iter = 0;
            while (osh_eval_cond(condw_buf + woff)) {
                if (keyboard_ctrlc_pending()) {
                    keyboard_consume_ctrlc();
                    return OSH_SCRIPT_ABORT;
                }
                int sub = osh_exec_range(C, bstart, bend);
                if (sub != OSH_SCRIPT_OK) return sub;
                if (++iter > 100000) break;
            }
            li = bend + 1;
            continue;
        }

        if (strncmp(s0, "else if ", 8)==0) {
            char *cond = s0 + 8;
            int depth=0, bstart=-1, bend=-1, cur=li, started=0;
            for (; cur<R; cur++) {
                char *t = C->lines[cur]; while (*t==' '||*t=='\t') t++;
                for (char* q=t; *q; q++){
                    if(*q=='{'){ depth++; if (!started){ started=1; bstart=cur+1; } }
                    else if(*q=='}'){ if (depth>0){ depth--; if(started && depth==0){ bend=cur; goto got_else_if_block; } } }
                }
            }
        got_else_if_block:
            if (bstart >= 0 && bend >= 0) {
                if (osh_eval_cond(cond)) {
                    int sub = osh_exec_range(C, bstart, bend);
                    if (sub != OSH_SCRIPT_OK) return sub;
                }
                li = bend + 1;
                continue;
            } else {
                li++;
                continue;
            }
        }

        if (strncmp(s0, "else", 4)==0) {
            int depth=0, bstart=-1, bend=-1, cur=li, started=0;
            for (; cur<R; cur++) {
                char *t = C->lines[cur]; while (*t==' '||*t=='\t') t++;
                for (char* q=t; *q; q++){
                    if(*q=='{'){ depth++; if (!started){ started=1; bstart=cur+1; } }
                    else if(*q=='}'){ if (depth>0){ depth--; if(started && depth==0){ bend=cur; goto got_else_block; } } }
                }
            }
        got_else_block:
            if (bstart >= 0 && bend >= 0) {
                int sub = osh_exec_range(C, bstart, bend);
                if (sub != OSH_SCRIPT_OK) return sub;
                li = bend + 1;
                continue;
            } else {
                li++;
                continue;
            }
        }

        {
            char name[32]; int ni=0; const char* p=s0;
            while ((*p=='_'||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')) && ni<31) { name[ni++]=*p++; }
            name[ni]='\0';
            if (ni>0 && *p=='(') {
                int fi = osh_find_func(C, name);
                if (fi>=0) {
                    p++;
                    char *args[8]; int ac=0; char token[256]; int ti=0; int inq=0; int had_quote=0;
                    while (*p && *p != ')') {
                        if (!inq && (*p==',')) {
                            token[ti]='\0';
                            char *trimmed = osh_dup_trim(token);
                            if (trimmed) {
                                if ((trimmed[0] || had_quote || ti>0) && ac < 8) args[ac++] = trimmed; else kfree(trimmed);
                            }
                            had_quote = 0;
                            ti=0; p++; continue;
                        }
                        if (*p=='"'){ inq=!inq; had_quote=1; p++; continue; }
                        if (ti < (int)sizeof(token)-1) token[ti++]=*p++; else p++;
                    }
                    token[ti]='\0';
                    char *trimmed = osh_dup_trim(token);
                    if (trimmed) {
                        if ((trimmed[0] || had_quote || ti>0 || ac>0) && ac < 8) args[ac++] = trimmed; else kfree(trimmed);
                    }
                    int call_rc = osh_call_func(C, fi, args, ac);
                    for (int i2=0;i2<ac;i2++) if (args[i2]) kfree(args[i2]);
                    if (call_rc != OSH_SCRIPT_OK) return call_rc;
                    li++; continue;
                }
            }
        }

        int rc = exec_line(s0);
        if (rc == OSH_SCRIPT_EXIT) return OSH_SCRIPT_EXIT;
        if (rc == 2) return 2;
        if (rc == OSH_SCRIPT_ABORT) return OSH_SCRIPT_ABORT;
        li++;
        if (li == prev_li) li++;
    }
    return OSH_SCRIPT_OK;
}

// -------- builtins --------
typedef struct { char **argv; int argc; const char *in; char **out; size_t *out_len; size_t *out_cap; } cmd_ctx;

// forward to allow builtins to execute lines
static int exec_line(const char *line);

static int out_printf(cmd_ctx *c, const char *s) { osh_write(c->out, c->out_len, c->out_cap, s); return 0; }

static int bi_echo(cmd_ctx *c) {
    if (c->argc <= 1) { out_printf(c, "\n"); return 0; }
    for (int i=1;i<c->argc;i++) { out_printf(c, c->argv[i]); if (i+1<c->argc) out_printf(c, " "); }
    return 0;
}

static int bi_pwd(cmd_ctx *c) { (void)c; char tmp[300]; strncpy(tmp, g_cwd, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0'; osh_write(c->out, c->out_len, c->out_cap, tmp); osh_write(c->out, c->out_len, c->out_cap, "\n"); return 0; }

static int bi_cd(cmd_ctx *c) {
    const char *arg = c->argc>1 ? c->argv[1] : "/";
    char path[256]; join_cwd(g_cwd, arg, path, sizeof(path));
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

static int bi_mkdir(cmd_ctx *c) { if (c->argc<2) { osh_write(c->out, c->out_len, c->out_cap, "mkdir: missing operand\n"); return 1; } char path[256]; join_cwd(g_cwd, c->argv[1], path, sizeof(path)); int r=ramfs_mkdir(path); return r==0?0:1; }
static int bi_touch(cmd_ctx *c) { if (c->argc<2) { osh_write(c->out, c->out_len, c->out_cap, "touch: missing operand\n"); return 1; } char path[256]; join_cwd(g_cwd, c->argv[1], path, sizeof(path)); struct fs_file *f = fs_create_file(path); if (!f) return 1; fs_file_free(f); return 0; }
static int bi_rm(cmd_ctx *c) { if (c->argc<2) { osh_write(c->out, c->out_len, c->out_cap, "rm: missing operand\n"); return 1; } char path[256]; join_cwd(g_cwd, c->argv[1], path, sizeof(path)); int r=ramfs_remove(path); return r==0?0:1; }

#include "../inc/axonos.h"

static int bi_about(cmd_ctx *c) { 
    (void)c;
    kprintf("%s v%s\n", OS_NAME, OS_VERSION);
    kprintf("Copyright (c) 2025 %s Team\n", OS_AUTHORS);
    kprintf("fcexx, kotazz, neosporimy, dasteldi\n");
    kprintf("<(09)>The operating system is licensed under the MIT license.\n");
    kprintf("<(0f)>GitHub: <(0b)>https://github.com/fcexx/AxonOS\n");
    kprintf("<(0f)>Website: <(0b)>https://wh27961.web4.maze-tech.ru\n");
    return 0;
    }

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

static int bi_mem(cmd_ctx *c){
    (void)c;
    int ram = sysinfo_ram_mb();
    size_t htot = heap_total_bytes();
    size_t huse = heap_used_bytes();
    size_t hpeak = heap_peak_bytes();
    if (ram >= 0) kprintf("RAM total: %d MB\n", ram);
    else kprintf("RAM total: unknown\n");
    kprintf("Heap: used %u KB / total %u KB (peak %u KB)\n",
        (unsigned)(huse/1024u), (unsigned)(htot/1024u), (unsigned)(hpeak/1024u));
    return 0;
}

// Run script file: osh <script>
static int bi_osh(cmd_ctx *c) {
    if (c->argc < 2) { osh_write(c->out, c->out_len, c->out_cap, "usage: osh <script>\n"); return 1; }
    // Use the same path join logic as cat/ls to avoid intermittent resolution issues
    char path[256]; join_cwd(g_cwd, c->argv[1], path, sizeof(path));
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
    // normalize patterns like "} else" to break onto new line to simplify parsing
    for (ssize_t idx = (ssize_t)start_off; idx < r - 4; idx++) {
        if (buf[idx] == '}') {
            ssize_t j = idx + 1;
            // skip single space/tabs; replace the first whitespace before 'else' with newline
            if (j < r && (buf[j] == ' ' || buf[j] == '\t')) {
                ssize_t k = j;
                while (k < r && (buf[k] == ' ' || buf[k] == '\t')) k++;
                if (k < r && strncmp(&buf[k], "else", 4) == 0) {
                    buf[j] = '\n';
                    for (ssize_t m = j + 1; m < k; m++) buf[m] = ' ';
                }
            }
        }
    }
    // split into lines
    int nlines = 0; for (ssize_t i=(ssize_t)start_off; i<r; i++) if (buf[i]=='\n') nlines++;
    nlines += 1;
    char **lines = (char**)kcalloc((size_t)nlines, sizeof(char*));
    int *line_len = (int*)kcalloc((size_t)nlines, sizeof(int));
    int idx = 0; char *line = buf + start_off; ssize_t i = (ssize_t)start_off;
    for (; i < r; i++) {
        if (buf[i] == '\r') { buf[i] = '\0'; }
        else if (buf[i] == '\n') { buf[i] = '\0'; lines[idx] = line; line_len[idx] = (int)strlen(line); idx++; line = buf + i + 1; }
    }
    if (*line) { lines[idx] = line; line_len[idx] = (int)strlen(line); idx++; }
    nlines = idx;

    // --- simple function table (pass 1) ---
    osh_func_def funcs[32]; int nfuncs = 0;
    for (int li=0; li<nlines; li++) {
        char *s = lines[li]; while (*s==' '||*s=='\t') s++;
        if (!*s || *s=='#') continue;
        // function header: name(arg1, arg2) {
        char name[32]; int ni=0;
        const char* p = s;
        while ((*p=='_'||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')) && ni<31) { name[ni++]=*p++; }
        name[ni]='\0';
        if (ni>0 && *p=='(') {
            p++;
            // parse params
            char *params[8]; int pc=0;
            char token[32]; int ti=0;
            int ok = 0;
            while (*p && *p != ')') {
                if (*p==' '||*p=='\t') { p++; continue; }
                if (*p==',') { p++; continue; }
                // ident
                ti=0;
                if ((*p=='_'||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))) {
                    while ((*p=='_'||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')) && ti<31) token[ti++]=*p++;
                    token[ti]='\0';
                    if (pc<8) { params[pc]=(char*)kcalloc(strlen(token)+1,1); memcpy(params[pc], token, strlen(token)+1); pc++; }
                } else { break; }
            }
            if (*p==')') { p++; while (*p==' '||*p=='\t') p++; if (*p=='{') { ok=1; } }
            if (ok) {
                // find matching }
                int depth = 0; int start = li+1; int end = nlines;
                for (int lj=li; lj<nlines; lj++) {
                    char *t = lines[lj]; while (*t==' '||*t=='\t') t++;
                    for (char *q=t; *q; q++) { if (*q=='{') depth++; else if (*q=='}') { depth--; if (depth==0) { end = lj; goto found_end; } } }
                }
            found_end:
                if (nfuncs < 32) {
                    strncpy(funcs[nfuncs].name, name, sizeof(funcs[nfuncs].name)-1);
                    funcs[nfuncs].pc = pc;
                    funcs[nfuncs].header = li;
                    for (int k=0;k<pc;k++) funcs[nfuncs].params[k]=params[k];
                    funcs[nfuncs].start = start;
                    funcs[nfuncs].end   = end;
                    nfuncs++;
                }
                li = end; // skip body
            }
        }
    }

    // Execute using top-level script engine
    osh_script_ctx CTX = { .lines = lines, .nlines = nlines, .funcs = funcs, .nfuncs = nfuncs };
    {
        g_script_depth++;
        int status = osh_exec_range(&CTX, 0, nlines);
        g_script_depth--;
        for (int j=0;j<nfuncs;j++){ for(int k=0;k<funcs[j].pc;k++) if(funcs[j].params[k]) kfree(funcs[j].params[k]); }
        kfree(lines); kfree(line_len); kfree(buf);
        if (status == OSH_SCRIPT_EXIT) status = 0;
        if (status == OSH_SCRIPT_ABORT) {
            status = 130;
        }
        return status;
    }

#if 0
    // helpers
    auto int find_func(const char* nm) -> int {
        for (int i2=0;i2<nfuncs;i2++) if (strcmp(funcs[i2].name, nm)==0) return i2; return -1;
    }
    auto int exec_range(int L, int R) -> int; // fwd
    auto int eval_cond(const char* expr) -> int {
        // support: a op b, where op in == != <= >= < >
        // Expand vars in expr
        char *ex = expand_vars(expr);
        const char* e = ex ? ex : expr;
        const char* ops[] = {"==","!=", "<=", ">=", "<", ">"};
        int which = -1; int pos = -1;
        for (int k=0;k<6;k++) {
            const char* o = ops[k];
            const char* p = strstr(e, o);
            if (p) { which=k; pos=(int)(p - e); break; }
        }
        int res = 0;
        if (which >= 0) {
            char left[256], right[256];
            int ln = pos; if (ln>255) ln=255; memcpy(left, e, ln); left[ln]='\0';
            const char* rp = e + pos + (ops[which][1] ? 2 : 1);
            while (*rp==' ') rp++;
            strncpy(right, rp, sizeof(right)-1); right[sizeof(right)-1]='\0';
            // trim left trailing spaces
            int ll=(int)strlen(left); while (ll>0 && (left[ll-1]==' '||left[ll-1]=='\t')) left[--ll]='\0';
            // try numeric
            const char* pl = left; const char* pr = right;
            double lv = osh_parse_expr(&pl);
            double rv = osh_parse_expr(&pr);
            // If parsers consumed nothing, fallback to string
            int numeric = 1;
            // basic compare with tolerance
            double eps = 1e-9;
            switch (which) {
                case 0: res = (lv - rv < eps && rv - lv < eps); break; // ==
                case 1: res = ! (lv - rv < eps && rv - lv < eps); break; // !=
                case 2: res = (lv <= rv + eps); break; // <=
                case 3: res = (lv + eps >= rv); break; // >=
                case 4: res = (lv < rv - eps); break; // <
                case 5: res = (lv > rv + eps); break; // >
            }
        } else {
            // Non-empty after expansion -> true
            res = (e && *e);
        }
        if (ex) kfree(ex);
        return res;
    }
    auto void call_func(int fi, char** args, int ac) -> void {
        // save old values and set params
        const int pc = funcs[fi].pc;
        char* saved_names[8]; char* saved_vals[8];
        for (int i2=0;i2<pc && i2<8;i2++){
            const char* pname = funcs[fi].params[i2];
            // save
            saved_names[i2] = (char*)pname;
            const char* old = var_get(pname);
            saved_vals[i2] = old ? (char*)old : NULL;
            // set new
            var_set(pname, (i2<ac && args[i2]) ? args[i2] : "");
        }
        (void)exec_range(funcs[fi].start, funcs[fi].end);
        // restore: no need to free saved_vals (pointed to internal storage)
        for (int i2=0;i2<pc && i2<8;i2++){
            const char* pname = saved_names[i2];
            if (saved_vals[i2]) var_set(pname, saved_vals[i2]);
            else var_set(pname, "");
        }
    }
    auto int exec_range(int L, int R) -> int {
        int li=L;
        while (li<R) {
            char *s0 = lines[li]; while (*s0==' '||*s0=='\t') s0++;
            if (!*s0 || *s0=='#') { li++; continue; }
            // if / else-if / else chain
            if (strncmp(s0, "if ", 3)==0) {
                // expect {...}
                char *cond = s0 + 3;
                // find block
                int depth = 0; int bstart = -1; int bend = -1; int cur = li;
                for (; cur<R; cur++) {
                    char *t = lines[cur]; while (*t==' '||*t=='\t') t++;
                    for (char* q=t; *q; q++){ if (*q=='{'){ depth++; if (depth==1) bstart = cur+1; } else if(*q=='}'){ depth--; if(depth==0){ bend = cur; goto got_if_block; } } }
                }
            got_if_block:
                int taken = eval_cond(cond);
                int after = bend + 1;
                if (taken) { (void)exec_range(bstart, bend); }
                else {
                    // check else if / else
                    int consumed = 0;
                    while (after < R) {
                        char *t = lines[after]; while (*t==' '||*t=='\t') t++;
                        if (strncmp(t, "else if ", 8)==0) {
                            // parse its block
                            char *c2 = t + 8;
                            int d2=0, s2=-1, e2=-1, w=after;
                            for (; w<R; w++){ char *u=lines[w]; while(*u==' '||*u=='\t') u++; for(char* q=u;*q;q++){ if(*q=='{'){ d2++; if(d2==1) s2=w+1; } else if(*q=='}'){ d2--; if(d2==0){ e2=w; goto got_elseif; } } } }
                        got_elseif:
                            if (eval_cond(c2)) { (void)exec_range(s2, e2); consumed = e2 - li + 1; after = e2 + 1; break; }
                            else { after = e2 + 1; }
                        } else if (strncmp(t, "else", 4)==0) {
                            // expect { block }
                            int d3=0, s3=-1, e3=-1, w=after;
                            for (; w<R; w++){ char *u=lines[w]; while(*u==' '||*u=='\t') u++; for(char* q=u;*q;q++){ if(*q=='{'){ d3++; if(d3==1) s3=w+1; } else if(*q=='}'){ d3--; if(d3==0){ e3=w; goto got_else; } } } }
                        got_else:
                            (void)exec_range(s3, e3); consumed = e3 - li + 1; after = e3 + 1; break;
                        } else break;
                    }
                    if (!consumed) { /* nothing executed */ }
                }
                li = after;
                continue;
            }
            // while <cond> { ... }
            if (strncmp(s0, "while ", 6)==0) {
                char *cond = s0 + 6;
                int depth=0, bstart=-1, bend=-1, cur=li;
                for (; cur<R; cur++) {
                    char *t = lines[cur]; while (*t==' '||*t=='\t') t++;
                    for (char* q=t; *q; q++){ if(*q=='{'){ depth++; if(depth==1) bstart=cur+1; } else if(*q=='}'){ depth--; if(depth==0){ bend=cur; goto got_while; } } }
                }
            got_while:
                // naive loop guard to avoid hanging
                int iter = 0;
                while (eval_cond(cond)) {
                    (void)exec_range(bstart, bend);
                    if (++iter > 100000) break;
                }
                li = bend + 1;
                continue;
            }
            // function call: name(arg,...)
            {
                char name[32]; int ni2=0; const char* p2=s0;
                while ((*p2=='_'||(*p2>='a'&&*p2<='z')||(*p2>='A'&&*p2<='Z')) && ni2<31) { name[ni2++]=*p2++; }
                name[ni2]='\0';
                if (ni2>0 && *p2=='(') {
                    int fi = find_func(name);
                    if (fi >= 0) {
                        p2++;
                        char *args[8]; int ac=0;
                        char token[256]; int ti=0; int inq=0;
                        while (*p2 && *p2 != ')') {
                            if (!inq && (*p2==',')) {
                                token[ti]='\0'; args[ac]=(char*)kcalloc(strlen(token)+1,1); memcpy(args[ac], token, strlen(token)+1); ac++; ti=0; p2++; continue;
                            }
                            if (*p2=='"') { inq = !inq; p2++; continue; }
                            if (ti < (int)sizeof(token)-1) token[ti++]=*p2++;
                            else p2++;
                        }
                        token[ti]='\0'; if (ti||ac) { args[ac]=(char*)kcalloc(strlen(token)+1,1); memcpy(args[ac], token, strlen(token)+1); ac++; }
                        call_func(fi, args, ac);
                        for (int i3=0;i3<ac;i3++) if (args[i3]) kfree(args[i3]);
                        li++; continue;
                    }
                }
            }
            // plain command
            int rc = exec_line(s0);
            if (rc == 2) { /* exit shell */ for (int j=0;j<nfuncs;j++){ for(int k=0;k<funcs[j].pc;k++) if(funcs[j].params[k]) kfree(funcs[j].params[k]); } kfree(lines); kfree(line_len); kfree(buf); return 0; }
            li++;
        }
        return 0;
    }
    int status = exec_range(0, nlines);
    for (int j=0;j<nfuncs;j++){ for(int k=0;k<funcs[j].pc;k++) if(funcs[j].params[k]) kfree(funcs[j].params[k]); }
    kfree(lines); kfree(line_len); kfree(buf);
    return status;
#endif
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

static int bi_help(cmd_ctx *c) {
    (void)c;
    kprint("OSH v0.1 (axosh)\n");
    kprint("Available commands:\n");
    kprint("help - show available commands\n");
    kprint("clear, cls - clear the screen\n");
    kprint("reboot - reboot the system\n");
    kprint("shutdown - shutdown the system\n");
    kprint("echo <text> - print text\n");
    kprint("snake - run the snake game\n");
    kprint("tetris - run the tetris game\n");
    kprint("clock - run the analog clock\n");
    kprint("time - show current time from RTC\n");
    kprint("date - show current date from RTC\n");
    kprint("uptime - show system uptime based on RTC ticks\n");
    kprint("about - show information about authors and system\n");
    kprint("ls - list directory contents\n");
    kprint("cat - print file contents\n");
    kprint("mkdir - create a directory\n");
    kprint("touch - create an empty file\n");
    kprint("rm - remove a file\n");
    kprint("edit - edit a file\n");
    kprint("pause - pause the shell and wait for a key press\n");
    kprint("chipset info - print chipset information\n");
    kprint("chipset reset - reset chipset\n");
    kprint("neofetch - show system information\n");
    kprint("osh - run a script file\n");
    kprint("art - show ASCII art\n");
    kprint("exit - exit the shell\n");
    return 0;
}

extern void ascii_art(void);
static int bi_art(cmd_ctx *c){ (void)c; ascii_art(); return 0; }
typedef int (*builtin_fn)(cmd_ctx*);
typedef struct { const char* name; builtin_fn fn; } builtin;
static const builtin builtin_table[] = {
    {"echo", bi_echo}, {"pwd", bi_pwd}, {"cd", bi_cd}, {"clear", bi_cls}, {"cls", bi_cls},
    {"ls", bi_ls}, {"cat", bi_cat}, {"mkdir", bi_mkdir}, {"touch", bi_touch}, {"rm", bi_rm},
    {"about", bi_about}, {"time", bi_time}, {"date", bi_date}, {"uptime", bi_uptime},
    {"edit", bi_edit}, {"snake", bi_snake}, {"tetris", bi_tetris}, {"clock", bi_clock},
    {"reboot", bi_reboot}, {"shutdown", bi_shutdown}, {"neofetch", bi_neofetch}, {"mem", bi_mem},
    {"osh", bi_osh}, {"art", bi_art}, {"pause", bi_pause}, {"chipset", bi_chipset}, {"help", bi_help},
};

static builtin_fn find_builtin(const char* name) {
    for (size_t i=0;i<sizeof(builtin_table)/sizeof(builtin_table[0]);i++) if (strcmp(builtin_table[i].name, name)==0) return builtin_table[i].fn;
    return NULL;
}

// export builtin names for completion
int osh_get_builtin_names(const char*** out_names) {
    static const char* names[64];
    size_t n = sizeof(builtin_table)/sizeof(builtin_table[0]);
    for (size_t i=0;i<n && i<64;i++) names[i] = builtin_table[i].name;
    *out_names = names;
    return (int)n;
}

// -------- executor --------
static int exec_simple(char **argv, int argc, const char *in, char **out, size_t *out_len, size_t *out_cap) {
    if (argc==0) return 0;
    // handle assignment: name = rhs ...  OR  name=rhs (single word)
    if (argc >= 3 && strcmp(argv[1], "=") == 0 && is_valid_varname(argv[0])) {
        // join rhs with spaces
        char rhs[512]; size_t pos=0; rhs[0]='\0';
        for (int i=2;i<argc;i++) {
            size_t L = strlen(argv[i]); if (pos + L + 2 >= sizeof(rhs)) break;
            if (i>2) rhs[pos++] = ' ';
            memcpy(rhs+pos, argv[i], L); pos += L; rhs[pos]='\0';
        }
        // special: %(readline)
        if (strcmp(rhs, "%(readline)") == 0) {
            char cwd[256]; osh_get_cwd(cwd, sizeof(cwd));
            char linebuf[512]; int n = osh_line_read("", cwd, linebuf, (int)sizeof(linebuf));
            if (n < 0) {
                if (osh_line_was_ctrlc()) return OSH_SCRIPT_ABORT;
                linebuf[0]='\0';
            } else {
                linebuf[n]='\0';
            }
            var_set(argv[0], linebuf);
            return 0;
        }
        // detect arithmetic expression (optionally after expanding $vars)
        char *rhs_exp = expand_vars(rhs); const char* R = rhs_exp ? rhs_exp : rhs;
        int arith=1; for (const char* q=R; *q; q++) { char c=*q; if (!(c==' '||c=='\t'||c=='+'||c=='-'||c=='*'||c=='/'||c=='('||c==')'||c=='.'||(c>='0'&&c<='9'))) { arith=0; break; } }
        if (arith) {
            const char* s = R;
            double val = osh_parse_expr(&s);
            char buf[64]; osh_double_to_str(val, buf, sizeof(buf));
            var_set(argv[0], buf);
            if (rhs_exp) kfree(rhs_exp);
            return 0;
        } else {
            var_set(argv[0], R);
            if (rhs_exp) kfree(rhs_exp);
            return 0;
        }
    } else if (argc == 1) {
        // compact form: name=rhs
        char* eq = NULL; for (char* p=argv[0]; *p; p++) if (*p=='='){ eq=p; break; }
        if (eq) {
            *eq = '\0'; const char* name = argv[0]; const char* R = eq+1;
            if (is_valid_varname(name)) {
                if (strcmp(R, "%(readline)") == 0) {
                    char cwd[256]; osh_get_cwd(cwd, sizeof(cwd));
                    char linebuf[512]; int n = osh_line_read("", cwd, linebuf, (int)sizeof(linebuf));
                    if (n < 0) {
                        if (osh_line_was_ctrlc()) return OSH_SCRIPT_ABORT;
                        linebuf[0]='\0';
                    } else {
                        linebuf[n]='\0';
                    }
                    var_set(name, linebuf);
                    return 0;
                }
                char *rhs_exp = expand_vars(R); const char* RS = rhs_exp ? rhs_exp : R;
                int arith=1; for (const char* q=RS; *q; q++) { char c=*q; if (!(c==' '||c=='\t'||c=='+'||c=='-'||c=='*'||c=='/'||c=='('||c==')'||c=='.'||(c>='0'&&c<='9'))) { arith=0; break; } }
                if (arith) {
                    const char* s = RS;
                    double val = osh_parse_expr(&s);
                    char buf[64]; osh_double_to_str(val, buf, sizeof(buf));
                    var_set(name, buf);
                    if (rhs_exp) kfree(rhs_exp);
                    return 0;
                } else {
                    var_set(name, RS);
                    if (rhs_exp) kfree(rhs_exp);
                    return 0;
                }
            } else {
                *eq = '='; // restore and fallthrough to normal exec
            }
        }
    }
    if (strcmp(argv[0], "exit") == 0) {
        if (g_script_depth > 0) return OSH_SCRIPT_EXIT;
        return 2; // exit interactive shell
    }
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
        if (rc == OSH_SCRIPT_EXIT) {
            if (stage_out) kfree(stage_out);
            if (cur_out) kfree(cur_out);
            return OSH_SCRIPT_EXIT;
        }
        if (rc == OSH_SCRIPT_ABORT) {
            if (stage_out) kfree(stage_out);
            if (cur_out) kfree(cur_out);
            return OSH_SCRIPT_ABORT;
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
        if (rc == OSH_SCRIPT_EXIT) { free_tokens(t, tn); if (out) kfree(out); return OSH_SCRIPT_EXIT; }
        if (rc == OSH_SCRIPT_ABORT) { free_tokens(t, tn); if (out) kfree(out); return OSH_SCRIPT_ABORT; }
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
    osh_history_init();
    for (;;) {
        /* приглашение osh + строковый редактор */
        char prompt[128]; prompt[0]='\0';
        // показать cwd в приглашении
        strncpy(prompt, g_cwd, sizeof(prompt)-3); prompt[sizeof(prompt)-3]='\0';
        strncat(prompt, "> ", sizeof(prompt)-1 - strlen(prompt));
        int n = osh_line_read(prompt, g_cwd, buf, (int)sizeof(buf));
        if (n < 0) continue;
        char *line = buf;
        // Detect trailing background '&' at top level quickly; if present -> spawn job thread
        int tn=0; token *t = lex(line, &tn); if (tn==0) { if (t) kfree(t); continue; }
        int bg = (t[tn-1].t == T_BG);
        free_tokens(t, tn);
        if (!bg) osh_history_add(line);
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


