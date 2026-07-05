/**
 * dao_core.c — Ku 字节码虚拟机 (C 实现)
 * 
 * 执行 compiler.ku 产生的 JSON 字节码。
 * 目标：脱离 Python，让 Ku 语言独立运行。
 * 
 * 编译：gcc -o dao_core dao_core.c -lm -Wall -O2
 * 用法：
 *   echo '{"constants":[1,2],"instructions":[["LOAD_CONST",0],["LOAD_CONST",1],["BINARY_OP","+"],["RETURN"]]}' | ./dao_core
 *   ./dao_core bytecode.kub.json
 *   ./dao_core --bootstrap frontend_bootstrap.kub.json [module.ku ...] program.ku
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define DAO_GETCWD _getcwd
#define DAO_MKDIR(path) _mkdir(path)
#define DAO_POPEN _popen
#define DAO_PCLOSE _pclose
#define strdup _strdup
#else
#include <dirent.h>
#include <unistd.h>
#define DAO_GETCWD getcwd
#define DAO_MKDIR(path) mkdir(path, 0755)
#define DAO_POPEN popen
#define DAO_PCLOSE pclose
#endif

#include "../vendor/sqlite3.h"

#define MAX_DB_CONNS 64
static sqlite3 *db_conns[MAX_DB_CONNS];
static char g_exe_dir[4096] = {0};

/* ═══════════════════════════════════════════
 *  M4: 内存竞技场 + 分配计数
 *  目标：常驻 runtime 可在一次执行结束后批量回收，
 *  并提供可测量的分配/释放计数（杜绝“零泄漏”靠口说）。
 *  竞技场用扁平列表回收，天然免疫 env<->closure 环。
 * ═══════════════════════════════════════════ */
struct Val;
struct Env;
typedef struct {
    void **items;
    long count;
    long cap;
} PtrArena;

static PtrArena g_val_arena = {0};
static PtrArena g_env_arena = {0};
static PtrArena g_frame_arena = {0};
static PtrArena g_instr_arena = {0};
static PtrArena g_env_aux_arena = {0};   /* env_set 的 names/vals 数组 */
static PtrArena g_dict_entry_arena = {0}; /* dict_set 的 DictEntry */
static long g_val_allocated = 0;
static long g_val_freed = 0;
static long g_env_allocated = 0;
static long g_env_freed = 0;
static long g_frame_allocated = 0;
static long g_frame_freed = 0;
static long g_instr_allocated = 0;
static long g_instr_freed = 0;
static int g_arena_enabled = 1;  /* 默认开启竞技场登记；teardown 仅在 main 末尾触发 */

static void arena_register(PtrArena *a, void *p) {
    if (!g_arena_enabled || !p) return;
    if (a->count >= a->cap) {
        long ncap = a->cap ? a->cap * 2 : 256;
        void **ni = realloc(a->items, ncap * sizeof(void *));
        if (!ni) return;  /* 登记失败不致命：退化为不回收，绝不崩 */
        a->items = ni;
        a->cap = ncap;
    }
    a->items[a->count++] = p;
}

static void init_exe_dir(const char *argv0) {
    if (g_exe_dir[0]) return;
    if (!argv0 || !argv0[0]) {
        strcpy(g_exe_dir, ".");
        return;
    }
    strncpy(g_exe_dir, argv0, sizeof(g_exe_dir) - 1);
    g_exe_dir[sizeof(g_exe_dir) - 1] = '\0';
    char *last_sep = NULL;
    for (char *p = g_exe_dir; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (last_sep) *last_sep = '\0';
    else strcpy(g_exe_dir, ".");
}

/* ═══════════════════════════════════════════
 *  值类型
 * ═══════════════════════════════════════════ */

typedef enum { V_NIL, V_NUM, V_STR, V_BOOL, V_LIST, V_DICT, V_FN } ValType;

typedef struct Val Val;
typedef struct DictEntry DictEntry;
typedef struct Instr Instr;

struct DictEntry {
    char *key;
    Val *val;
    DictEntry *next;
};

struct Instr {
    char *op;
    char *str_arg;
    double num_arg;
    int has_str;
    int has_num;
};

struct Val {
    ValType type;
    double num;
    char *str;
    int bool_val;
    /* list */
    Val **items;
    int len;
    int cap;
    /* dict */
    DictEntry *entries;
    /* function */
    char **params;
    int param_count;
    /* instruction list (bytecode block) */
    struct Instr *body;
    int body_len;
    Val **constants;
    int const_count;
    /* closure env */
    struct Env *closure;
};

Val *val_new(ValType t) {
    Val *v = calloc(1, sizeof(Val));
    v->type = t;
    arena_register(&g_val_arena, v);
    g_val_allocated++;
    return v;
}

Val *val_num(double x) {
    Val *v = val_new(V_NUM);
    v->num = x;
    return v;
}

Val *val_str(const char *s) {
    Val *v = val_new(V_STR);
    v->str = strdup(s);
    return v;
}

Val *val_bool(int b) {
    Val *v = val_new(V_BOOL);
    v->bool_val = b;
    return v;
}

Val *val_nil(void) { return val_new(V_NIL); }

Val *val_list(int cap) {
    Val *v = val_new(V_LIST);
    v->cap = cap > 0 ? cap : 8;
    v->items = calloc(v->cap, sizeof(Val *));
    return v;
}

void list_push(Val *v, Val *item) {
    if (v->len >= v->cap) {
        v->cap *= 2;
        v->items = realloc(v->items, v->cap * sizeof(Val *));
    }
    v->items[v->len++] = item;
}

Val *val_dict(void) { return val_new(V_DICT); }

Val *val_fn(char **params, int param_count, struct Instr *body, int body_len, Val **constants, int const_count, struct Env *closure) {
    Val *v = val_new(V_FN);
    v->params = params;
    v->param_count = param_count;
    v->body = body;
    v->body_len = body_len;
    v->constants = constants;
    v->const_count = const_count;
    v->closure = closure;
    return v;
}

int val_truthy(Val *v) {
    if (!v) return 0;
    switch (v->type) {
        case V_NIL: return 0;
        case V_BOOL: return v->bool_val;
        case V_NUM: return v->num != 0;
        case V_STR: return v->str && v->str[0] != '\0';
        case V_LIST: return v->len > 0;
        case V_DICT: {
            int n = 0;
            for (DictEntry *e = v->entries; e; e = e->next) n++;
            return n > 0;
        }
        case V_FN: return 1;
    }
    return 0;
}

typedef struct {
    char *data;
    int len;
    int cap;
} StrBuf;

void sb_init(StrBuf *sb) {
    sb->cap = 64;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    int n = strlen(s);
    while (sb->len + n + 1 > sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, s, n + 1);
    sb->len += n;
}

void sb_append_n(StrBuf *sb, const char *s, int n) {
    while (sb->len + n + 1 > sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static int utf8_char_width(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_strlen_chars(const char *s) {
    int count = 0;
    int i = 0;
    while (s && s[i]) {
        i += utf8_char_width((unsigned char)s[i]);
        count++;
    }
    return count;
}

static int utf8_byte_offset(const char *s, int char_index) {
    int i = 0;
    int count = 0;
    while (s && s[i] && count < char_index) {
        i += utf8_char_width((unsigned char)s[i]);
        count++;
    }
    return i;
}

static char *utf8_substr_chars(const char *s, int start, int end) {
    int char_len = utf8_strlen_chars(s);
    if (start < 0) start = 0;
    if (end > char_len) end = char_len;
    if (end < start) end = start;
    int byte_start = utf8_byte_offset(s, start);
    int byte_end = utf8_byte_offset(s, end);
    int out_len = byte_end - byte_start;
    char *buf = malloc(out_len + 1);
    memcpy(buf, s + byte_start, out_len);
    buf[out_len] = '\0';
    return buf;
}

static int utf8_codepoint(const char *s) {
    if (!s || !s[0]) return 0;
    unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0) {
        return ((c0 & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0) {
        return ((c0 & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0) {
        return ((c0 & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
    }
    return c0;
}

static char *utf8_from_codepoint(int cp) {
    char *buf = calloc(5, 1);
    if (cp < 0x80) {
        buf[0] = (char)cp;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
    }
    return buf;
}

void val_append_string(StrBuf *sb, Val *v) {
    char buf[64];
    if (!v || v->type == V_NIL) { sb_append(sb, "null"); return; }
    switch (v->type) {
        case V_NIL:
            sb_append(sb, "null");
            break;
        case V_STR:
            sb_append(sb, v->str);
            break;
        case V_BOOL:
            sb_append(sb, v->bool_val ? "true" : "false");
            break;
        case V_NUM:
            if (v->num == (int)v->num) snprintf(buf, sizeof(buf), "%d", (int)v->num);
            else snprintf(buf, sizeof(buf), "%g", v->num);
            sb_append(sb, buf);
            break;
        case V_LIST:
            sb_append(sb, "[");
            for (int i = 0; i < v->len; i++) {
                if (i) sb_append(sb, ", ");
                val_append_string(sb, v->items[i]);
            }
            sb_append(sb, "]");
            break;
        case V_DICT: {
            sb_append(sb, "{");
            int first = 1;
            for (DictEntry *e = v->entries; e; e = e->next) {
                if (!first) sb_append(sb, ", ");
                sb_append(sb, "\"");
                sb_append(sb, e->key);
                sb_append(sb, "\": ");
                val_append_string(sb, e->val);
                first = 0;
            }
            sb_append(sb, "}");
            break;
        }
        case V_FN:
            sb_append(sb, "<fn>");
            break;
    }
}

void sb_append_char(StrBuf *sb, char c) {
    char tmp[2] = {c, '\0'};
    sb_append(sb, tmp);
}

void sb_append_json_escaped(StrBuf *sb, const char *s) {
    char buf[8];
    sb_append(sb, "\"");
    for (int i = 0; s && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') sb_append(sb, "\\\"");
        else if (c == '\\') sb_append(sb, "\\\\");
        else if (c == '\n') sb_append(sb, "\\n");
        else if (c == '\r') sb_append(sb, "\\r");
        else if (c == '\t') sb_append(sb, "\\t");
        else if (c == '\b') sb_append(sb, "\\b");
        else if (c == '\f') sb_append(sb, "\\f");
        else if (c < 0x20) {
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            sb_append(sb, buf);
        } else {
            sb_append_char(sb, (char)c);
        }
    }
    sb_append(sb, "\"");
}

char *val_to_string(Val *v) {
    StrBuf sb;
    sb_init(&sb);
    val_append_string(&sb, v);
    return sb.data;
}

void val_append_json(StrBuf *sb, Val *v) {
    char buf[64];
    if (!v || v->type == V_NIL) { sb_append(sb, "null"); return; }
    switch (v->type) {
        case V_NIL:
            sb_append(sb, "null");
            break;
        case V_STR:
            sb_append_json_escaped(sb, v->str);
            break;
        case V_BOOL:
            sb_append(sb, v->bool_val ? "true" : "false");
            break;
        case V_NUM:
            if (v->num == (int)v->num) snprintf(buf, sizeof(buf), "%d", (int)v->num);
            else snprintf(buf, sizeof(buf), "%g", v->num);
            sb_append(sb, buf);
            break;
        case V_LIST:
            sb_append(sb, "[");
            for (int i = 0; i < v->len; i++) {
                if (i) sb_append(sb, ",");
                val_append_json(sb, v->items[i]);
            }
            sb_append(sb, "]");
            break;
        case V_DICT: {
            sb_append(sb, "{");
            int first = 1;
            for (DictEntry *e = v->entries; e; e = e->next) {
                if (!first) sb_append(sb, ",");
                sb_append_json_escaped(sb, e->key);
                sb_append(sb, ":");
                val_append_json(sb, e->val);
                first = 0;
            }
            sb_append(sb, "}");
            break;
        }
        case V_FN:
            sb_append_json_escaped(sb, "<fn>");
            break;
    }
}

char *val_to_json_string(Val *v) {
    StrBuf sb;
    sb_init(&sb);
    val_append_json(&sb, v);
    return sb.data;
}

int val_equal(Val *left, Val *right) {
    if (!left || !right) return left == right;
    if (left->type != right->type) return 0;
    switch (left->type) {
        case V_NIL: return 1;
        case V_NUM: return left->num == right->num;
        case V_STR: return strcmp(left->str, right->str) == 0;
        case V_BOOL: return left->bool_val == right->bool_val;
        case V_LIST:
            if (left->len != right->len) return 0;
            for (int i = 0; i < left->len; i++) {
                if (!val_equal(left->items[i], right->items[i])) return 0;
            }
            return 1;
        case V_DICT: {
            int left_count = 0;
            int right_count = 0;
            for (DictEntry *e = left->entries; e; e = e->next) left_count++;
            for (DictEntry *e = right->entries; e; e = e->next) right_count++;
            if (left_count != right_count) return 0;
            for (DictEntry *e = left->entries; e; e = e->next) {
                Val *rv = NULL;
                for (DictEntry *r = right->entries; r; r = r->next) {
                    if (strcmp(r->key, e->key) == 0) {
                        rv = r->val;
                        break;
                    }
                }
                if (!rv || !val_equal(e->val, rv)) return 0;
            }
            return 1;
        }
        case V_FN:
            return left == right;
    }
    return 0;
}

void val_print(Val *v) {
    if (!v) { printf("null"); return; }
    switch (v->type) {
        case V_NIL: printf("null"); break;
        case V_NUM: {
            if (v->num == (int)v->num) printf("%d", (int)v->num);
            else printf("%g", v->num);
            break;
        }
        case V_STR: {
            StrBuf sb;
            sb_init(&sb);
            sb_append_json_escaped(&sb, v->str);
            printf("%s", sb.data);
            free(sb.data);
            break;
        }
        case V_BOOL: printf(v->bool_val ? "true" : "false"); break;
        case V_LIST:
            printf("[");
            for (int i = 0; i < v->len; i++) {
                if (i) printf(", ");
                val_print(v->items[i]);
            }
            printf("]");
            break;
        case V_DICT: {
            printf("{");
            int first = 1;
            for (DictEntry *e = v->entries; e; e = e->next) {
                if (!first) printf(", ");
                StrBuf key_sb;
                sb_init(&key_sb);
                sb_append_json_escaped(&key_sb, e->key);
                printf("%s: ", key_sb.data);
                free(key_sb.data);
                val_print(e->val);
                first = 0;
            }
            printf("}");
            break;
        }
        case V_FN: printf("<fn>"); break;
    }
}

void dict_set(Val *d, const char *key, Val *val) {
    for (DictEntry *e = d->entries; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            e->val = val;
            return;
        }
    }
    DictEntry *e = malloc(sizeof(DictEntry));
    e->key = strdup(key);
    e->val = val;
    e->next = NULL;
    if (!d->entries) {
        d->entries = e;
        return;
    }
    DictEntry *tail = d->entries;
    while (tail->next) tail = tail->next;
    tail->next = e;
}

Val *dict_get(Val *d, const char *key) {
    for (DictEntry *e = d->entries; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->val;
    }
    return NULL;
}

void print_json_escaped_string(const char *s) {
    StrBuf sb;
    sb_init(&sb);
    sb_append_json_escaped(&sb, s);
    printf("%s", sb.data);
    free(sb.data);
}

/* ═══════════════════════════════════════════
 *  环境
 * ═══════════════════════════════════════════ */

typedef struct Env Env;
struct Env {
    Env *parent;
    int count;
    char **names;
    Val **vals;
};

Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->parent = parent;
    arena_register(&g_env_arena, e);
    g_env_allocated++;
    return e;
}

void env_set(Env *e, const char *name, Val *val) {
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->names[i], name) == 0) {
            e->vals[i] = val;
            return;
        }
    }
    e->count++;
    e->names = realloc(e->names, e->count * sizeof(char *));
    e->vals = realloc(e->vals, e->count * sizeof(Val *));
    e->names[e->count - 1] = strdup(name);
    e->vals[e->count - 1] = val;
}

Val *env_get(Env *e, const char *name) {
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->names[i], name) == 0) return e->vals[i];
    }
    if (e->parent) return env_get(e->parent, name);
    return NULL;
}

/* ═══════════════════════════════════════════
 *  栈帧
 * ═══════════════════════════════════════════ */

#define STACK_MAX 1024
#define TRY_MAX 64

typedef struct Frame {
    Instr *instrs;
    int instr_count;
    Val **constants;
    int const_count;
    Env *env;
    Val *stack[STACK_MAX];
    int sp;
    int pc;
    int try_stack[TRY_MAX];
    int try_sp;
    struct Frame *parent;
} Frame;

Frame *frame_new(Instr *instrs, int instr_count, Val **constants, int const_count, Env *env, Frame *parent) {
    Frame *f = calloc(1, sizeof(Frame));
    f->instrs = instrs;
    f->instr_count = instr_count;
    f->constants = constants;
    f->const_count = const_count;
    f->env = env;
    f->parent = parent;
    arena_register(&g_frame_arena, f);
    g_frame_allocated++;
    return f;
}

void frame_push(Frame *f, Val *v) { f->stack[f->sp++] = v; }
Val *frame_pop(Frame *f) { return f->stack[--f->sp]; }
Val *frame_top(Frame *f) { return f->stack[f->sp - 1]; }

/* ReturnSignal / ErrorSignal: 用特殊值表示 return 和可传播异常 */
typedef struct { int is_return; int is_error; Val *val; char *error; } ExecResult;

static ExecResult exec_frame(Frame *f);
static void register_builtins(Env *global);

static ExecResult exec_error(const char *message) {
    ExecResult r = {0, 1, NULL, strdup(message)};
    return r;
}

int frame_raise(Frame *f, const char *message) {
    if (f->try_sp <= 0) return 0;
    int catch_addr = f->try_stack[--f->try_sp];
    frame_push(f, val_str(message));
    f->pc = catch_addr;
    return 1;
}

static ExecResult call_value(Val *func, Val **args, int argc, Frame *caller) {
    ExecResult r = {0, 0, NULL, NULL};
    if (!func || func->type != V_FN) {
        r.is_error = 1;
        r.error = strdup("value is not callable");
        return r;
    }

    Env *call_env = env_new(func->closure);
    for (int i = 0; i < func->param_count && i < argc; i++) {
        env_set(call_env, func->params[i], args[i]);
    }

    Frame *call_frame = frame_new(
        func->body, func->body_len,
        func->constants, func->const_count,
        call_env, caller
    );
    return exec_frame(call_frame);
}

/* ═══════════════════════════════════════════
 *  JSON 解析器 (极简)
 * ═══════════════════════════════════════════ */

typedef struct { const char *s; int p; } JSParse;

static void js_skip(JSParse *j) {
    while (j->s[j->p] == ' ' || j->s[j->p] == '\t' || j->s[j->p] == '\n' || j->s[j->p] == '\r')
        j->p++;
}

Val *js_parse_val(JSParse *j);
Instr *js_parse_instrs(JSParse *j, int *out_count);
Val **js_parse_const_arr(JSParse *j, int *out_count);

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int js_parse_hex4(JSParse *j) {
    int cp = 0;
    for (int i = 0; i < 4; i++) {
        int v = hex_digit_value(j->s[j->p + i]);
        if (v < 0) return -1;
        cp = (cp << 4) | v;
    }
    j->p += 4;
    return cp;
}

static char *js_parse_string_raw(JSParse *j) {
    js_skip(j);
    if (j->s[j->p] != '"') return NULL;
    j->p++;
    int cap = 32;
    int len = 0;
    char *s = malloc(cap);
    while (j->s[j->p] && j->s[j->p] != '"') {
        char ch = j->s[j->p++];
        if (ch == '\\' && j->s[j->p]) {
            char esc = j->s[j->p++];
            if (esc == 'n') ch = '\n';
            else if (esc == 't') ch = '\t';
            else if (esc == 'r') ch = '\r';
            else if (esc == 'b') ch = '\b';
            else if (esc == 'f') ch = '\f';
            else if (esc == '"' || esc == '\\' || esc == '/') ch = esc;
            else if (esc == 'u') {
                int cp = js_parse_hex4(j);
                if (cp >= 0xD800 && cp <= 0xDBFF && j->s[j->p] == '\\' && j->s[j->p + 1] == 'u') {
                    j->p += 2;
                    int low = js_parse_hex4(j);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                }
                if (cp >= 0) {
                    char *utf8 = utf8_from_codepoint(cp);
                    int utf8_len = strlen(utf8);
                    while (len + utf8_len + 1 >= cap) { cap *= 2; s = realloc(s, cap); }
                    memcpy(s + len, utf8, utf8_len);
                    len += utf8_len;
                    free(utf8);
                    continue;
                } else {
                    ch = 'u';
                }
            }
            else {
                if (len + 1 >= cap) { cap *= 2; s = realloc(s, cap); }
                s[len++] = '\\';
                ch = esc;
            }
        }
        if (len + 1 >= cap) { cap *= 2; s = realloc(s, cap); }
        s[len++] = ch;
    }
    s[len] = '\0';
    if (j->s[j->p] == '"') j->p++;
    return s;
}

Val *js_parse_val(JSParse *j) {
    js_skip(j);
    char c = j->s[j->p];

    /* null */
    if (strncmp(j->s + j->p, "null", 4) == 0) { j->p += 4; return val_nil(); }
    /* true */
    if (strncmp(j->s + j->p, "true", 4) == 0) { j->p += 4; return val_bool(1); }
    /* false */
    if (strncmp(j->s + j->p, "false", 5) == 0) { j->p += 5; return val_bool(0); }

    /* string */
    if (c == '"') {
        char *s = js_parse_string_raw(j);
        return s ? val_str(s) : val_nil();
    }

    /* number */
    if (c == '-' || isdigit(c)) {
        char *end;
        double v = strtod(j->s + j->p, &end);
        j->p = end - j->s;
        return val_num(v);
    }

    /* array */
    if (c == '[') {
        j->p++;
        Val *list = val_list(8);
        js_skip(j);
        while (j->s[j->p] != ']') {
            list_push(list, js_parse_val(j));
            js_skip(j);
            if (j->s[j->p] == ',') j->p++;
            js_skip(j);
        }
        j->p++; /* skip ] */
        return list;
    }

    /* object */
    if (c == '{') {
        j->p++;
        Val *dict = val_dict();
        js_skip(j);
        while (j->s[j->p] != '}') {
            char *key = js_parse_string_raw(j);
            js_skip(j);
            j->p++; /* skip : */
            js_skip(j);
            Val *val = js_parse_val(j);
            dict_set(dict, key, val);
            free(key);
            js_skip(j);
            if (j->s[j->p] == ',') j->p++;
            js_skip(j);
        }
        j->p++; /* skip } */
        return dict;
    }

    return val_nil();
}

/* 解析指令数组: [["OP", arg], ["OP"], ...] */
Instr *js_parse_instrs(JSParse *j, int *out_count) {
    js_skip(j);
    if (j->s[j->p] != '[') { *out_count = 0; return NULL; }
    j->p++; /* skip [ */

    int cap = 64;
    Instr *instrs = calloc(cap, sizeof(Instr));
    int count = 0;

    js_skip(j);
    while (j->s[j->p] == '[') {
        j->p++; /* skip [ */
        js_skip(j);

        /* opcode string */
        char *op = js_parse_string_raw(j);
        if (count >= cap) { cap *= 2; instrs = realloc(instrs, cap * sizeof(Instr)); }
        instrs[count].op = op;
        instrs[count].has_str = 0;
        instrs[count].has_num = 0;

        js_skip(j);
        if (j->s[j->p] == ']') {
            j->p++;
            count++;
            js_skip(j);
            if (j->s[j->p] == ',') { j->p++; js_skip(j); }
            continue;
        }

        /* arg: string or number (handle 3-element format: ["OP", null, 0]) */
        js_skip(j);
        if (j->s[j->p] == '"') {
            instrs[count].str_arg = js_parse_string_raw(j);
            instrs[count].has_str = 1;
        } else if (j->s[j->p] == '-' || isdigit(j->s[j->p])) {
            char *end;
            instrs[count].num_arg = strtod(j->s + j->p, &end);
            j->p = end - j->s;
            instrs[count].has_num = 1;
        } else if (j->s[j->p] == 'n' && strncmp(j->s + j->p, "null", 4) == 0) {
            j->p += 4;
            /* null arg - check for second arg (3-element format) */
            js_skip(j);
            if (j->s[j->p] != ']' && j->s[j->p] != ',') {
                if (j->s[j->p] == '"') {
                    instrs[count].str_arg = js_parse_string_raw(j);
                    instrs[count].has_str = 1;
                } else if (j->s[j->p] == '-' || isdigit(j->s[j->p])) {
                    char *end;
                    instrs[count].num_arg = strtod(j->s + j->p, &end);
                    j->p = end - j->s;
                    instrs[count].has_num = 1;
                }
            }
        } else {
            /* skip unknown */
            js_skip(j);
        }

        j->p++; /* skip ] */
        count++;
        js_skip(j);
        if (j->s[j->p] == ',') { j->p++; js_skip(j); }
    }
    j->p++; /* skip final ] */
    *out_count = count;
    return instrs;
}

/* ═══════════════════════════════════════════
 *  内置函数
 * ═══════════════════════════════════════════ */

static char *read_text_file(const char *path, size_t *out_len);

Val *builtin_len(Val **args, int argc) {
    if (argc < 1) return val_num(0);
    Val *a = args[0];
    if (a->type == V_LIST) return val_num(a->len);
    if (a->type == V_STR) return val_num(utf8_strlen_chars(a->str));
    if (a->type == V_DICT) {
        int n = 0;
        for (DictEntry *e = a->entries; e; e = e->next) n++;
        return val_num(n);
    }
    return val_num(0);
}

Val *builtin_push(Val **args, int argc) {
    if (argc < 2) return val_nil();
    Val *list = args[0];
    if (list->type != V_LIST) return val_nil();
    list_push(list, args[1]);
    return list;
}

Val *builtin_str(Val **args, int argc) {
    if (argc < 1) return val_str("");
    char *s = val_to_string(args[0]);
    Val *result = val_str(s);
    free(s);
    return result;
}

Val *builtin_str_split(Val **args, int argc) {
    Val *result = val_list(8);
    if (argc < 2 || args[0]->type != V_STR || args[1]->type != V_STR) return result;
    const char *s = args[0]->str;
    const char *delimiter = args[1]->str;
    int delimiter_len = strlen(delimiter);
    if (delimiter_len == 0) {
        list_push(result, val_str(s));
        return result;
    }

    const char *start = s;
    const char *match = strstr(start, delimiter);
    while (match) {
        int part_len = match - start;
        char *part = malloc(part_len + 1);
        memcpy(part, start, part_len);
        part[part_len] = '\0';
        list_push(result, val_str(part));
        free(part);
        start = match + delimiter_len;
        match = strstr(start, delimiter);
    }
    list_push(result, val_str(start));
    return result;
}

Val *builtin_str_contains(Val **args, int argc) {
    if (argc < 2 || args[0]->type != V_STR || args[1]->type != V_STR) return val_bool(0);
    return val_bool(strstr(args[0]->str, args[1]->str) != NULL);
}

Val *builtin_str_trim(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_str("");
    const char *s = args[0]->str;
    const char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    const char *end = s + strlen(s);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    int len = (int)(end - start);
    char *buf = malloc(len + 1);
    memcpy(buf, start, len);
    buf[len] = '\0';
    Val *result = val_str(buf);
    free(buf);
    return result;
}

Val *builtin_str_is_empty(Val **args, int argc) {
    if (argc < 1 || !args[0]) return val_bool(1);
    if (args[0]->type != V_STR) return val_bool(0);
    const char *p = args[0]->str;
    while (*p) {
        if (!isspace((unsigned char)*p)) return val_bool(0);
        p++;
    }
    return val_bool(1);
}

Val *builtin_str_replace(Val **args, int argc) {
    if (argc < 3 || args[0]->type != V_STR || args[1]->type != V_STR || args[2]->type != V_STR) return val_str("");
    const char *s = args[0]->str;
    const char *old = args[1]->str;
    const char *new_s = args[2]->str;
    int old_len = strlen(old);
    if (old_len == 0) return val_str(s);
    StrBuf sb;
    sb_init(&sb);
    const char *p = s;
    const char *match = strstr(p, old);
    while (match) {
        int prefix_len = (int)(match - p);
        char *prefix = malloc(prefix_len + 1);
        memcpy(prefix, p, prefix_len);
        prefix[prefix_len] = '\0';
        sb_append(&sb, prefix);
        sb_append(&sb, new_s);
        free(prefix);
        p = match + old_len;
        match = strstr(p, old);
    }
    sb_append(&sb, p);
    Val *result = val_str(sb.data);
    free(sb.data);
    return result;
}

Val *builtin_str_starts_with(Val **args, int argc) {
    if (argc < 2 || args[0]->type != V_STR || args[1]->type != V_STR) return val_bool(0);
    size_t prefix_len = strlen(args[1]->str);
    return val_bool(strncmp(args[0]->str, args[1]->str, prefix_len) == 0);
}

Val *builtin_str_ends_with(Val **args, int argc) {
    if (argc < 2 || args[0]->type != V_STR || args[1]->type != V_STR) return val_bool(0);
    size_t s_len = strlen(args[0]->str);
    size_t suffix_len = strlen(args[1]->str);
    if (suffix_len > s_len) return val_bool(0);
    return val_bool(strcmp(args[0]->str + s_len - suffix_len, args[1]->str) == 0);
}

Val *builtin_str_upper(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_str("");
    char *buf = strdup(args[0]->str);
    for (int i = 0; buf[i]; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x80) buf[i] = (char)toupper(c);
    }
    Val *result = val_str(buf);
    free(buf);
    return result;
}

Val *builtin_str_lower(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_str("");
    char *buf = strdup(args[0]->str);
    for (int i = 0; buf[i]; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x80) buf[i] = (char)tolower(c);
    }
    Val *result = val_str(buf);
    free(buf);
    return result;
}

Val *builtin_int(Val **args, int argc) {
    if (argc < 1) return val_num(0);
    if (args[0]->type == V_NUM) return val_num((long long)args[0]->num);
    if (args[0]->type == V_BOOL) return val_num(args[0]->bool_val ? 1 : 0);
    if (args[0]->type == V_STR) return val_num(strtoll(args[0]->str, NULL, 10));
    return val_num(0);
}

Val *builtin_float(Val **args, int argc) {
    if (argc < 1) return val_num(0);
    if (args[0]->type == V_NUM) return val_num(args[0]->num);
    if (args[0]->type == V_BOOL) return val_num(args[0]->bool_val ? 1 : 0);
    if (args[0]->type == V_STR) return val_num(strtod(args[0]->str, NULL));
    return val_num(0);
}

Val *builtin_abs(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_NUM) return val_num(0);
    return val_num(fabs(args[0]->num));
}

Val *builtin_floor(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_NUM) return val_num(0);
    return val_num(floor(args[0]->num));
}

Val *builtin_slice(Val **args, int argc) {
    if (argc < 3) return val_nil();
    Val *target = args[0];
    int start = (int)args[1]->num;
    int end = (int)args[2]->num;
    if (target->type == V_LIST) {
        if (start < 0) start = 0;
        if (end > target->len) end = target->len;
        if (end < start) end = start;
        Val *result = val_list(end - start);
        for (int i = start; i < end; i++) list_push(result, target->items[i]);
        return result;
    }
    if (target->type == V_STR) {
        char *buf = utf8_substr_chars(target->str, start, end);
        Val *result = val_str(buf);
        free(buf);
        return result;
    }
    return val_nil();
}

Val *builtin_ord(Val **args, int argc) {
    if (argc < 1 || !args[0] || args[0]->type != V_STR) return val_num(0);
    return val_num(utf8_codepoint(args[0]->str));
}

Val *builtin_chr(Val **args, int argc) {
    if (argc < 1) return val_str("");
    char *buf = utf8_from_codepoint((int)args[0]->num);
    Val *result = val_str(buf);
    free(buf);
    return result;
}

Val *builtin_is_str(Val **args, int argc) {
    if (argc < 1) return val_bool(0);
    return val_bool(args[0]->type == V_STR);
}

Val *builtin_is_list(Val **args, int argc) {
    if (argc < 1) return val_bool(0);
    return val_bool(args[0]->type == V_LIST);
}

Val *builtin_is_dict(Val **args, int argc) {
    if (argc < 1) return val_bool(0);
    return val_bool(args[0]->type == V_DICT);
}

Val *builtin_is_none(Val **args, int argc) {
    return val_bool(argc < 1 || !args[0] || args[0]->type == V_NIL);
}

Val *builtin_has(Val **args, int argc) {
    if (argc < 2 || args[0]->type != V_DICT || args[1]->type != V_STR)
        return val_bool(0);
    return val_bool(dict_get(args[0], args[1]->str) != NULL);
}

Val *builtin_keys(Val **args, int argc) {
    Val *result = val_list(8);
    if (argc < 1 || args[0]->type != V_DICT) return result;
    for (DictEntry *e = args[0]->entries; e; e = e->next) {
        list_push(result, val_str(e->key));
    }
    return result;
}

Val *builtin_items(Val **args, int argc) {
    Val *result = val_list(8);
    if (argc < 1 || args[0]->type != V_DICT) return result;
    for (DictEntry *e = args[0]->entries; e; e = e->next) {
        Val *pair = val_list(2);
        list_push(pair, val_str(e->key));
        list_push(pair, e->val);
        list_push(result, pair);
    }
    return result;
}

Val *builtin_type(Val **args, int argc) {
    if (argc < 1) return val_str("nil");
    switch (args[0]->type) {
        case V_NIL: return val_str("nil");
        case V_NUM: return val_str("num");
        case V_STR: return val_str("str");
        case V_BOOL: return val_str("bool");
        case V_LIST: return val_str("list");
        case V_DICT: return val_str("dict");
        case V_FN: return val_str("fn");
    }
    return val_str("unknown");
}

Val *builtin_print(Val **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i) printf(" ");
        val_print(args[i]);
    }
    printf("\n");
    return val_nil();
}

Val *builtin_and(Val **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (!val_truthy(args[i])) return val_bool(0);
    }
    return val_bool(1);
}

Val *builtin_or(Val **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (val_truthy(args[i])) return val_bool(1);
    }
    return val_bool(0);
}

Val *builtin_not(Val **args, int argc) {
    if (argc < 1) return val_bool(1);
    return val_bool(!val_truthy(args[0]));
}

Val *builtin_path_exists(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_bool(0);
    struct stat st;
    return val_bool(stat(args[0]->str, &st) == 0);
}

Val *builtin_read_file(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_str("");
    size_t len = 0;
    char *data = read_text_file(args[0]->str, &len);
    if (!data) return val_str("");
    Val *result = val_str(data);
    free(data);
    return result;
}

Val *builtin_write_file(Val **args, int argc) {
    if (argc < 2 || args[0]->type != V_STR) return val_bool(0);
    char *content = args[1]->type == V_STR ? strdup(args[1]->str) : val_to_string(args[1]);
    FILE *f = fopen(args[0]->str, "wb");
    if (!f) {
        free(content);
        return val_bool(0);
    }
    size_t len = strlen(content);
    size_t n = fwrite(content, 1, len, f);
    int ok = n == len && fclose(f) == 0;
    free(content);
    return val_bool(ok);
}

Val *builtin_delete_file(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_bool(0);
    return val_bool(remove(args[0]->str) == 0);
}

Val *builtin_mkdir(Val **args, int argc) {
    if (argc < 1 || args[0]->type != V_STR) return val_bool(0);
    if (DAO_MKDIR(args[0]->str) == 0) return val_bool(1);
    if (errno == EEXIST) return val_bool(1);
    return val_bool(0);
}

Val *builtin_list_dir(Val **args, int argc) {
    Val *result = val_list(8);
    if (argc < 1 || args[0]->type != V_STR) return result;
#ifdef _WIN32
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", args[0]->str);
    struct _finddata_t entry;
    intptr_t handle = _findfirst(pattern, &entry);
    if (handle == -1) return result;
    do {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) continue;
        list_push(result, val_str(entry.name));
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);
#else
    DIR *dir = opendir(args[0]->str);
    if (!dir) return result;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        list_push(result, val_str(entry->d_name));
    }
    closedir(dir);
#endif
    return result;
}

Val *builtin_now(Val **args, int argc) {
    (void)args;
    (void)argc;
    return val_num((double)time(NULL));
}

Val *builtin_now_fmt(Val **args, int argc) {
    (void)args;
    (void)argc;
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buf[32];
    if (!tm_info || strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
        return val_str("");
    }
    return val_str(buf);
}

/* ── SQLite ──
 * 句柄用 1-based 整数 id 表示（ValType 无不透明指针类型）。
 * 错误语义保持 M3 最小够用：open 失败 nil、exec 失败 -1、query 失败 []、close nil。 */
static sqlite3 *db_conn_get(int idx) {
    if (idx < 1 || idx > MAX_DB_CONNS) return NULL;
    return db_conns[idx - 1];
}

static int db_conn_alloc(sqlite3 *db) {
    for (int i = 0; i < MAX_DB_CONNS; i++) {
        if (!db_conns[i]) {
            db_conns[i] = db;
            return i + 1;
        }
    }
    return 0;
}

static void sqlite_bind_params(sqlite3_stmt *stmt, Val *params_list) {
    if (!params_list || params_list->type != V_LIST) return;
    for (int i = 0; i < params_list->len; i++) {
        Val *p = params_list->items[i];
        int slot = i + 1;
        if (!p || p->type == V_NIL) {
            sqlite3_bind_null(stmt, slot);
        } else if (p->type == V_NUM) {
            sqlite3_bind_double(stmt, slot, p->num);
        } else if (p->type == V_BOOL) {
            sqlite3_bind_int(stmt, slot, p->bool_val);
        } else if (p->type == V_STR) {
            sqlite3_bind_text(stmt, slot, p->str, -1, SQLITE_TRANSIENT);
        } else {
            char *s = val_to_string(p);
            sqlite3_bind_text(stmt, slot, s, -1, SQLITE_TRANSIENT);
            free(s);
        }
    }
}

Val *builtin_sqlite_open(Val **args, int argc) {
    if (argc < 1 || !args[0] || args[0]->type != V_STR) return val_nil();
    sqlite3 *db = NULL;
    int rc = sqlite3_open(args[0]->str, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return val_nil();
    }
    int idx = db_conn_alloc(db);
    if (idx == 0) {
        sqlite3_close(db);
        return val_nil();
    }
    return val_num((double)idx);
}

/* sqlite_exec / sqlite_query 通过 ExecResult 暴露 SQL 错误，
 * 与 Python sqlite3 抛异常的语义对齐（而不是静默返回 -1）。
 * 错误文本采用 sqlite3_errmsg，使 "no such table: ..." 等可被上层捕获/上报。 */
static ExecResult sqlite_exec_result(const char *errmsg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "sqlite error: %s", errmsg ? errmsg : "unknown");
    ExecResult r = {0, 1, NULL, strdup(buf)};
    return r;
}

ExecResult builtin_sqlite_exec(Val **args, int argc) {
    ExecResult r = {0, 0, NULL, NULL};
    if (argc < 2 || !args[0] || args[0]->type != V_NUM || !args[1] || args[1]->type != V_STR) {
        r.val = val_num(-1);
        return r;
    }
    sqlite3 *db = db_conn_get((int)args[0]->num);
    if (!db) { r.val = val_num(-1); return r; }
    /* SQL text is trusted Dao program source from .ku literals, not
       model/user input; bound parameters use ? placeholders via
       sqlite_bind_params and are never interpolated into the query string. */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, args[1]->str, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return sqlite_exec_result(sqlite3_errmsg(db));
    if (argc >= 3) sqlite_bind_params(stmt, args[2]);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        ExecResult err = sqlite_exec_result(sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return err;
    }
    sqlite3_finalize(stmt);
    r.val = val_num((double)changes);
    return r;
}

ExecResult builtin_sqlite_query(Val **args, int argc) {
    ExecResult r = {0, 0, NULL, NULL};
    Val *result = val_list(8);
    if (argc < 2 || !args[0] || args[0]->type != V_NUM || !args[1] || args[1]->type != V_STR) {
        r.val = result;
        return r;
    }
    sqlite3 *db = db_conn_get((int)args[0]->num);
    if (!db) { r.val = result; return r; }
    /* SQL text is trusted Dao program source from .ku literals, not
       model/user input; bound parameters use ? placeholders via
       sqlite_bind_params and are never interpolated into the query string. */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, args[1]->str, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return sqlite_exec_result(sqlite3_errmsg(db));
    if (argc >= 3) sqlite_bind_params(stmt, args[2]);
    int col_count = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Val *row = val_dict();
        for (int c = 0; c < col_count; c++) {
            const char *col_name = sqlite3_column_name(stmt, c);
            int col_type = sqlite3_column_type(stmt, c);
            Val *cell;
            if (col_type == SQLITE_NULL) {
                cell = val_nil();
            } else if (col_type == SQLITE_INTEGER) {
                cell = val_num((double)sqlite3_column_int64(stmt, c));
            } else if (col_type == SQLITE_FLOAT) {
                cell = val_num(sqlite3_column_double(stmt, c));
            } else {
                const char *text = (const char *)sqlite3_column_text(stmt, c);
                cell = val_str(text ? text : "");
            }
            dict_set(row, col_name ? col_name : "", cell);
        }
        list_push(result, row);
    }
    sqlite3_finalize(stmt);
    r.val = result;
    return r;
}

Val *builtin_sqlite_close(Val **args, int argc) {
    if (argc < 1 || !args[0] || args[0]->type != V_NUM) return val_nil();
    int idx = (int)args[0]->num;
    if (idx < 1 || idx > MAX_DB_CONNS) return val_nil();
    sqlite3 *db = db_conns[idx - 1];
    if (db) {
        sqlite3_close(db);
        db_conns[idx - 1] = NULL;
    }
    return val_nil();
}

/* ── Dao 数据目录 ──
 * 复用 Python 侧语义：优先 DAO_DATA_DIR，否则回退 <exe_dir>/data。
 * dao_data_path 自动创建父目录，与 runtime.py 一致。 */
static int mkdir_p(const char *path) {
    char buf[4096];
    if (!path || !path[0]) return 0;
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int len = (int)strlen(buf);
    if (len == 0) return 0;
    if (buf[len - 1] == '/' || buf[len - 1] == '\\') buf[--len] = '\0';

    for (int i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            /* 跳过 Windows 盘符根，例如 "C:" */
            if (i == 2 && buf[1] == ':') continue;
            char save = buf[i];
            buf[i] = '\0';
            if (buf[0] != '\0') {
                if (DAO_MKDIR(buf) != 0 && errno != EEXIST) {
                    buf[i] = save;
                    return 0;
                }
            }
            buf[i] = save;
        }
    }
    return 1;
}

static void join_path_into(char *out, size_t cap, const char *base, const char *name) {
    if (cap == 0) return;
    out[0] = '\0';
    if (!base) base = ".";
    if (!name) name = "";
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);
    int need_sep = base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\';
    if (base_len + (need_sep ? 1 : 0) + name_len + 1 > cap) {
        name_len = cap > base_len + (need_sep ? 1 : 0) + 1 ? cap - base_len - (need_sep ? 1 : 0) - 1 : 0;
    }
    strncat(out, base, cap - 1);
    if (need_sep && strlen(out) + 1 < cap) strcat(out, "/");
    if (strlen(out) + name_len < cap) strncat(out, name, name_len);
}

static void dao_data_dir_into(char *dir, size_t cap) {
    const char *env_dir = getenv("DAO_DATA_DIR");
    if (env_dir && env_dir[0]) {
        strncpy(dir, env_dir, cap - 1);
        dir[cap - 1] = '\0';
    } else {
        join_path_into(dir, cap, g_exe_dir[0] ? g_exe_dir : ".", "data");
    }
}

Val *builtin_dao_data_dir(Val **args, int argc) {
    (void)args;
    (void)argc;
    char dir[4096];
    dao_data_dir_into(dir, sizeof(dir));
    mkdir_p(dir);
    return val_str(dir);
}

Val *builtin_dao_data_path(Val **args, int argc) {
    if (argc < 1 || !args[0]) return val_str("");
    char *name = args[0]->type == V_STR ? strdup(args[0]->str) : val_to_string(args[0]);
    char dir[4096];
    dao_data_dir_into(dir, sizeof(dir));

    char path[4096];
    join_path_into(path, sizeof(path), dir, name);
    free(name);

    /* 若 name 含子目录，创建父目录 */
    char parent[4096];
    strncpy(parent, path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char *last_sep = NULL;
    for (char *p = parent; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (last_sep) {
        *last_sep = '\0';
        mkdir_p(parent);
    }
    return val_str(path);
}

Val *builtin_system(Val **args, int argc) {
    Val *result = val_dict();
    dict_set(result, "stdout", val_str(""));
    dict_set(result, "stderr", val_str(""));
    dict_set(result, "code", val_num(1));
    /* Shell capability is off by default to keep model/agent-generated
       code from running arbitrary commands via ku_eval. Opt in with the
       DAO_ALLOW_SYSTEM env flag for trusted debugging/parity work. */
    const char *allow_system = getenv("DAO_ALLOW_SYSTEM");
    if (!(allow_system && (strcmp(allow_system, "1") == 0 ||
                           strcmp(allow_system, "true") == 0 ||
                           strcmp(allow_system, "yes") == 0 ||
                           strcmp(allow_system, "on") == 0))) {
        dict_set(result, "stderr", val_str("system builtin disabled: set DAO_ALLOW_SYSTEM=1 to run shell commands"));
        return result;
    }
    if (argc < 1) return result;
    char *cmd = args[0]->type == V_STR ? strdup(args[0]->str) : val_to_string(args[0]);
    size_t shell_len = strlen(cmd) + strlen(" 2>&1") + 1;
    char *shell_cmd = malloc(shell_len);
    snprintf(shell_cmd, shell_len, "%s 2>&1", cmd);
    FILE *pipe = DAO_POPEN(shell_cmd, "r");
    free(shell_cmd);
    if (!pipe) {
        free(cmd);
        return result;
    }

    StrBuf out;
    sb_init(&out);
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        sb_append(&out, buf);
    }
    int status = DAO_PCLOSE(pipe);
    dict_set(result, "stdout", val_str(out.data));
    dict_set(result, "stderr", val_str(""));
    dict_set(result, "code", val_num(status));
    free(out.data);
    free(cmd);
    return result;
}

ExecResult builtin_parse(Val **args, int argc, Frame *caller) {
    ExecResult r = {0, 0, NULL, NULL};
    if (argc < 1 || !args[0] || args[0]->type != V_STR) {
        r.val = val_nil();
        return r;
    }

    Val *lex = env_get(caller->env, "lex");
    Val *parse_tokens = env_get(caller->env, "parse_tokens");
    if (!lex || !parse_tokens) {
        r.is_error = 1;
        r.error = strdup("parse requires bootstrap frontend: lex/parse_tokens not loaded");
        return r;
    }

    Val *lex_args[1] = {args[0]};
    ExecResult lexed = call_value(lex, lex_args, 1, caller);
    if (lexed.is_error) return lexed;

    Val *parse_args[1] = {lexed.val};
    ExecResult parsed = call_value(parse_tokens, parse_args, 1, caller);
    if (parsed.is_error) return parsed;

    Val *ast = parsed.val;
    if (ast && ast->type == V_DICT) {
        Val *type_val = dict_get(ast, "type");
        Val *children = dict_get(ast, "children");
        if (type_val && type_val->type == V_STR && strcmp(type_val->str, "block") == 0 &&
            children && children->type == V_LIST && children->len == 1) {
            parsed.val = children->items[0];
        }
    }
    return parsed;
}

Val *builtin_list_thoughts(Env *env) {
    Val *result = val_list(32);
    for (Env *cur = env; cur; cur = cur->parent) {
        for (int i = 0; i < cur->count; i++) {
            int seen = 0;
            for (int j = 0; j < result->len; j++) {
                if (result->items[j]->type == V_STR && strcmp(result->items[j]->str, cur->names[i]) == 0) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) list_push(result, val_str(cur->names[i]));
        }
    }
    return result;
}

static Val *parse_json_value(const char *input);

Val *builtin_json_parse(Val **args, int argc) {
    if (argc < 1 || !args[0] || args[0]->type != V_STR) return val_nil();
    return parse_json_value(args[0]->str);
}

Val *builtin_json_stringify(Val **args, int argc) {
    if (argc < 1) return val_str("null");
    char *json = val_to_json_string(args[0]);
    Val *result = val_str(json);
    free(json);
    return result;
}

static int is_url_safe_byte(unsigned char c) {
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
}

Val *builtin_url_encode(Val **args, int argc) {
    if (argc < 1) return val_str("");
    char *text = args[0]->type == V_STR ? strdup(args[0]->str) : val_to_string(args[0]);
    StrBuf sb;
    sb_init(&sb);
    char buf[4];
    for (int i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (is_url_safe_byte(c)) {
            sb_append_char(&sb, (char)c);
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            sb_append(&sb, buf);
        }
    }
    Val *result = val_str(sb.data);
    free(text);
    free(sb.data);
    return result;
}

Val *builtin_url_decode(Val **args, int argc) {
    if (argc < 1) return val_str("");
    char *text = args[0]->type == V_STR ? strdup(args[0]->str) : val_to_string(args[0]);
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; text[i]; i++) {
        if (text[i] == '%' && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
            int hi = hex_digit_value(text[i + 1]);
            int lo = hex_digit_value(text[i + 2]);
            sb_append_char(&sb, (char)((hi << 4) | lo));
            i += 2;
        } else {
            sb_append_char(&sb, text[i]);
        }
    }
    Val *result = val_str(sb.data);
    free(text);
    free(sb.data);
    return result;
}

Val *builtin_http_response(const char *method, Val **args, int argc) {
    const char *url = (argc >= 1 && args[0] && args[0]->type == V_STR) ? args[0]->str : "";
    Val *body_obj = val_dict();
    dict_set(body_obj, "method", val_str(method));
    dict_set(body_obj, "url", val_str(url));
    char *body = val_to_json_string(body_obj);

    Val *result = val_dict();
    dict_set(result, "ok", val_bool(1));
    dict_set(result, "是否成功", val_bool(1));
    dict_set(result, "status", val_num(200));
    dict_set(result, "状态码", val_num(200));
    dict_set(result, "body", val_str(body));
    dict_set(result, "内容", val_str(body));
    dict_set(result, "error", val_str(""));
    dict_set(result, "错误", val_str(""));
    free(body);
    return result;
}

Val *builtin_http_get(Val **args, int argc) {
    return builtin_http_response("GET", args, argc);
}

Val *builtin_http_post(Val **args, int argc) {
    return builtin_http_response("POST", args, argc);
}

Val *builtin_http_put(Val **args, int argc) {
    return builtin_http_response("PUT", args, argc);
}

Val *builtin_http_delete(Val **args, int argc) {
    return builtin_http_response("DELETE", args, argc);
}

typedef struct {
    Instr *instrs;
    int instr_count;
    Val **constants;
    int const_count;
} BytecodeParts;

BytecodeParts bytecode_from_val(Val *bc) {
    BytecodeParts parts = {0};
    if (!bc || bc->type != V_DICT) return parts;

    Val *constants_val = dict_get(bc, "constants");
    Val *instructions_val = dict_get(bc, "instructions");

    parts.const_count = constants_val && constants_val->type == V_LIST ? constants_val->len : 0;
    parts.constants = calloc(parts.const_count, sizeof(Val *));
    for (int i = 0; i < parts.const_count; i++) {
        parts.constants[i] = constants_val->items[i];
    }

    parts.instr_count = instructions_val && instructions_val->type == V_LIST ? instructions_val->len : 0;
    parts.instrs = calloc(parts.instr_count, sizeof(Instr));
    for (int i = 0; i < parts.instr_count; i++) {
        Val *item = instructions_val->items[i];
        if (item->type == V_LIST && item->len >= 1) {
            parts.instrs[i].op = strdup(item->items[0]->str);
            parts.instrs[i].has_str = 0;
            parts.instrs[i].has_num = 0;
            Val *arg = NULL;
            if (item->len >= 3 && item->items[1]->type == V_NIL) {
                arg = item->items[2];
            } else if (item->len >= 2) {
                arg = item->items[1];
            }
            if (arg && arg->type == V_STR) {
                parts.instrs[i].str_arg = strdup(arg->str);
                parts.instrs[i].has_str = 1;
            } else if (arg && arg->type == V_NUM) {
                parts.instrs[i].num_arg = arg->num;
                parts.instrs[i].has_num = 1;
            }
        }
    }
    return parts;
}

Val *builtin_run_bytecode(Val **args, int argc, Env *global) {
    if (argc < 1) return val_nil();
    BytecodeParts parts = bytecode_from_val(args[0]);
    if (!global) {
        global = env_new(NULL);
        register_builtins(global);
    }
    Frame *frame = frame_new(parts.instrs, parts.instr_count, parts.constants, parts.const_count, global, NULL);
    ExecResult result = exec_frame(frame);
    /* parts.constants 被 MAKE_FUNCTION 持有的 val_fn 共享，不可释放 */
    for (int i = 0; i < parts.instr_count; i++) {
        free(parts.instrs[i].op);
        free(parts.instrs[i].str_arg);
    }
    free(parts.instrs);
    if (result.is_error) return val_str(result.error);
    return result.val ? result.val : val_nil();
}

static void register_builtins(Env *global) {
    env_set(global, "len", val_str("len"));
    env_set(global, "push", val_str("push"));
    env_set(global, "str", val_str("str"));
    env_set(global, "str_split", val_str("str_split"));
    env_set(global, "str_contains", val_str("str_contains"));
    env_set(global, "str_trim", val_str("str_trim"));
    env_set(global, "str_is_empty", val_str("str_is_empty"));
    env_set(global, "str_replace", val_str("str_replace"));
    env_set(global, "str_starts_with", val_str("str_starts_with"));
    env_set(global, "str_ends_with", val_str("str_ends_with"));
    env_set(global, "str_upper", val_str("str_upper"));
    env_set(global, "str_lower", val_str("str_lower"));
    env_set(global, "int", val_str("int"));
    env_set(global, "float", val_str("float"));
    env_set(global, "abs", val_str("abs"));
    env_set(global, "floor", val_str("floor"));
    env_set(global, "slice", val_str("slice"));
    env_set(global, "ord", val_str("ord"));
    env_set(global, "chr", val_str("chr"));
    env_set(global, "is_str", val_str("is_str"));
    env_set(global, "is_list", val_str("is_list"));
    env_set(global, "is_dict", val_str("is_dict"));
    env_set(global, "is_none", val_str("is_none"));
    env_set(global, "has", val_str("has"));
    env_set(global, "keys", val_str("keys"));
    env_set(global, "items", val_str("items"));
    env_set(global, "type", val_str("type"));
    env_set(global, "print", val_str("print"));
    env_set(global, "and", val_str("and"));
    env_set(global, "or", val_str("or"));
    env_set(global, "not", val_str("not"));
    env_set(global, "且", val_str("且"));
    env_set(global, "或", val_str("或"));
    env_set(global, "非", val_str("非"));
    env_set(global, "path_exists", val_str("path_exists"));
    env_set(global, "read_file", val_str("read_file"));
    env_set(global, "write_file", val_str("write_file"));
    env_set(global, "delete_file", val_str("delete_file"));
    env_set(global, "mkdir", val_str("mkdir"));
    env_set(global, "list_dir", val_str("list_dir"));
    env_set(global, "now", val_str("now"));
    env_set(global, "now_fmt", val_str("now_fmt"));
    env_set(global, "sqlite_open", val_str("sqlite_open"));
    env_set(global, "sqlite_exec", val_str("sqlite_exec"));
    env_set(global, "sqlite_query", val_str("sqlite_query"));
    env_set(global, "sqlite_close", val_str("sqlite_close"));
    env_set(global, "dao_data_dir", val_str("dao_data_dir"));
    env_set(global, "dao_data_path", val_str("dao_data_path"));
    env_set(global, "system", val_str("system"));
    env_set(global, "parse", val_str("parse"));
    env_set(global, "list_thoughts", val_str("list_thoughts"));
    env_set(global, "json_parse", val_str("json_parse"));
    env_set(global, "json_stringify", val_str("json_stringify"));
    env_set(global, "url_encode", val_str("url_encode"));
    env_set(global, "url_decode", val_str("url_decode"));
    env_set(global, "http_get", val_str("http_get"));
    env_set(global, "http_post", val_str("http_post"));
    env_set(global, "http_put", val_str("http_put"));
    env_set(global, "http_delete", val_str("http_delete"));
    env_set(global, "run_bytecode", val_str("run_bytecode"));
}

static char *read_text_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "无法打开: %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "无法读取: %s\n", path);
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fprintf(stderr, "无法读取: %s\n", path);
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "无法读取: %s\n", path);
        fclose(f);
        return NULL;
    }
    char *data = malloc((size_t)len + 1);
    if (!data) {
        fprintf(stderr, "内存不足\n");
        fclose(f);
        return NULL;
    }
    size_t n = fread(data, 1, (size_t)len, f);
    data[n] = '\0';
    fclose(f);
    if (out_len) *out_len = n;
    return data;
}

/* Strip C-style comments from source: both // line comments
 * and star-block comments. Returns new heap string (caller frees).
 * Does not strip comments inside string literals. */
static char *strip_c_comments(const char *src) {
    if (!src) return NULL;
    size_t n = strlen(src);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    size_t j = 0;
    size_t i = 0;
    while (i < n) {
        // line comment
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') i++;
            continue;
        }
        // block comment
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) i++;
            if (i + 1 < n) i += 2;
            continue;
        }
        out[j++] = src[i++];
    }
    out[j] = '\0';
    return out;
}

static char *read_stdin_all(size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *data = malloc(cap);
    if (!data) {
        fprintf(stderr, "内存不足\n");
        return NULL;
    }
    int ch;
    while ((ch = getchar()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            data = realloc(data, cap);
            if (!data) {
                fprintf(stderr, "内存不足\n");
                return NULL;
            }
        }
        data[len++] = (char)ch;
    }
    data[len] = '\0';
    if (out_len) *out_len = len;
    return data;
}

typedef struct {
    char **items;
    int len;
} StringList;

typedef struct {
    StringList loaded_paths;
    StringList alias_exports;
} ModuleLoadContext;

static int string_list_contains(StringList *list, const char *value) {
    for (int i = 0; i < list->len; i++) {
        if (strcmp(list->items[i], value) == 0) return 1;
    }
    return 0;
}

static void string_list_add(StringList *list, const char *value) {
    list->len++;
    list->items = realloc(list->items, list->len * sizeof(char *));
    list->items[list->len - 1] = strdup(value);
}

static void string_list_free(StringList *list) {
    for (int i = 0; i < list->len; i++) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->len = 0;
}

static char *path_dirname(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash && backslash ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);
    if (!sep) return strdup(".");
    int len = sep - path;
    char *out = malloc(len + 1);
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash && backslash ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);
    return strdup(sep ? sep + 1 : path);
}

static char *repo_root_from_bootstrap(const char *bootstrap_path) {
    char *demo_dir = path_dirname(bootstrap_path);
    char *base = path_basename(demo_dir);
    if (strcmp(base, "demos") == 0) {
        char *root = path_dirname(demo_dir);
        free(base);
        free(demo_dir);
        return root;
    }
    free(base);
    free(demo_dir);
    char cwd[4096];
    if (DAO_GETCWD(cwd, sizeof(cwd))) return strdup(cwd);
    return strdup(".");
}

static int str_has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static int module_spec_has_parent_segment(const char *spec) {
    const char *p = spec;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        if (p - start == 2 && start[0] == '.' && start[1] == '.') return 1;
        if (*p == '/') p++;
    }
    return 0;
}

static char *normalize_module_spec(const char *spec, int spec_len) {
    char *clean = malloc(spec_len + 1);
    int out = 0;
    for (int i = 0; i < spec_len; i++) {
        char c = spec[i] == '\\' ? '/' : spec[i];
        clean[out++] = c;
    }
    clean[out] = '\0';

    if (out == 0 || clean[0] == '/' || strchr(clean, ':') || strstr(clean, "//") || module_spec_has_parent_segment(clean)) {
        fprintf(stderr, "ImportError: 非法模块路径: %s\n", clean);
        free(clean);
        return NULL;
    }
    return clean;
}

static char *resolve_import_path(const char *repo_root, const char *spec) {
    int needs_ext = !str_has_suffix(spec, ".ku");
    size_t len = strlen(repo_root) + strlen("/dao/") + strlen(spec) + (needs_ext ? 3 : 0) + 1;
    char *path = malloc(len);
    path[0] = '\0';
    strcat(path, repo_root);
    strcat(path, "/dao/");
    strcat(path, spec);
    if (needs_ext) strcat(path, ".ku");
    return path;
}

static int append_imports_from_source(StrBuf *out, const char *source, const char *repo_root, ModuleLoadContext *ctx);

static int is_import_directive_line(const char *line_start, const char *line_end) {
    const char *p = line_start;
    while (p < line_end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    return p < line_end && strncmp(p, "引", strlen("引")) == 0;
}

static void append_source_without_import_lines(StrBuf *out, const char *source) {
    const char *line = source;
    while (*line) {
        const char *next = strchr(line, '\n');
        if (!next) next = line + strlen(line);
        if (!is_import_directive_line(line, next)) {
            sb_append_n(out, line, (int)(next - line));
            if (*next == '\n') sb_append(out, "\n");
        }
        line = *next ? next + 1 : next;
    }
}

static int collect_source_file(StrBuf *out, const char *path, const char *repo_root, ModuleLoadContext *ctx) {
    if (string_list_contains(&ctx->loaded_paths, path)) return 1;
    string_list_add(&ctx->loaded_paths, path);

    size_t part_len = 0;
    char *part = read_text_file(path, &part_len);
    if (!part) return 0;

    // Strip C-style comments so block/line comments do not confuse the Ku lexer
    char *clean = strip_c_comments(part);
    if (clean) {
        free(part);
        part = clean;
    }

    if (!append_imports_from_source(out, part, repo_root, ctx)) {
        free(part);
        return 0;
    }
    append_source_without_import_lines(out, part);
    sb_append(out, "\n");
    free(part);
    return 1;
}

static int starts_with_alias_prefix(const char *name, const char *alias) {
    size_t n = strlen(alias);
    return n > 0 && strncmp(name, alias, n) == 0 && name[n] == '_';
}

static void append_alias_wrappers(StrBuf *out, ModuleLoadContext *ctx, const char *module_path, const char *module_source, const char *alias) {
    if (!alias || !alias[0]) return;
    size_t key_len = strlen(module_path) + strlen(alias) + 2;
    char *key = malloc(key_len);
    snprintf(key, key_len, "%s|%s", module_path, alias);
    if (string_list_contains(&ctx->alias_exports, key)) {
        free(key);
        return;
    }
    string_list_add(&ctx->alias_exports, key);
    free(key);

    const char *line = module_source;
    while (*line) {
        const char *next = strchr(line, '\n');
        if (!next) next = line + strlen(line);
        const char *p = line;
        while (p < next && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if (p < next && strncmp(p, "思", strlen("思")) == 0) {
            p += strlen("思");
            while (p < next && (*p == ' ' || *p == '\t')) p++;
            const char *name_start = p;
            while (p < next && *p != '(' && *p != ' ' && *p != '\t') p++;
            const char *name_end = p;
            const char *open = memchr(p, '(', next - p);
            const char *close = open ? memchr(open + 1, ')', next - open - 1) : NULL;
            if (name_end > name_start && open && close) {
                int name_len = (int)(name_end - name_start);
                int params_len = (int)(close - open - 1);
                char *name = malloc(name_len + 1);
                char *params = malloc(params_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';
                memcpy(params, open + 1, params_len);
                params[params_len] = '\0';

                if (name[0] != '_' && !starts_with_alias_prefix(name, alias)) {
                    sb_append(out, "\n思 ");
                    sb_append(out, alias);
                    sb_append(out, "_");
                    sb_append(out, name);
                    sb_append(out, "(");
                    sb_append(out, params);
                    sb_append(out, ") {\n  ");
                    sb_append(out, name);
                    sb_append(out, "(");
                    sb_append(out, params);
                    sb_append(out, ")\n}\n");
                }
                free(name);
                free(params);
            }
        }
        line = *next ? next + 1 : next;
    }
}

static int append_import_spec(StrBuf *out, const char *repo_root, ModuleLoadContext *ctx, const char *spec, int spec_len, const char *alias, int alias_len) {
    char *clean = normalize_module_spec(spec, spec_len);
    if (!clean) return 0;
    char *alias_clean = malloc(alias_len + 1);
    memcpy(alias_clean, alias, alias_len);
    alias_clean[alias_len] = '\0';
    char *path = resolve_import_path(repo_root, clean);
    if (!collect_source_file(out, path, repo_root, ctx)) {
        free(path);
        free(alias_clean);
        free(clean);
        return 0;
    }
    size_t module_len = 0;
    char *module_source = read_text_file(path, &module_len);
    if (module_source) {
        append_alias_wrappers(out, ctx, path, module_source, alias_clean);
        free(module_source);
    }
    free(path);
    free(alias_clean);
    free(clean);
    return 1;
}

static int append_imports_from_source(StrBuf *out, const char *source, const char *repo_root, ModuleLoadContext *ctx) {
    const char *line = source;
    while (*line) {
        const char *next = strchr(line, '\n');
        if (!next) next = line + strlen(line);

        const char *p = line;
        while (p < next && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if (p < next && strncmp(p, "引", strlen("引")) == 0) {
            const char *q1 = memchr(p, '"', next - p);
            if (q1) {
                const char *q2 = memchr(q1 + 1, '"', next - q1 - 1);
                if (q2 && q2 > q1 + 1) {
                    const char *alias_start = "";
                    int alias_len = 0;
                    const char *as_kw = strstr(q2 + 1, "别");
                    if (as_kw && as_kw < next) {
                        alias_start = as_kw + strlen("别");
                        while (alias_start < next && (*alias_start == ' ' || *alias_start == '\t')) alias_start++;
                        const char *alias_end = alias_start;
                        while (alias_end < next && *alias_end != ' ' && *alias_end != '\t' && *alias_end != '\r') alias_end++;
                        alias_len = (int)(alias_end - alias_start);
                    }
                    if (!append_import_spec(out, repo_root, ctx, q1 + 1, (int)(q2 - q1 - 1), alias_start, alias_len)) {
                        return 0;
                    }
                }
            }
        }
        line = *next ? next + 1 : next;
    }
    return 1;
}

static char *read_source_files(int count, char **paths, const char *repo_root) {
    StrBuf out;
    ModuleLoadContext ctx = {0};
    sb_init(&out);

    for (int i = 0; i < count; i++) {
        if (!collect_source_file(&out, paths[i], repo_root, &ctx)) {
            string_list_free(&ctx.loaded_paths);
            string_list_free(&ctx.alias_exports);
            free(out.data);
            return NULL;
        }
    }

    string_list_free(&ctx.loaded_paths);
    string_list_free(&ctx.alias_exports);
    return out.data;
}

static Val *parse_json_value(const char *input) {
    JSParse j = {input, 0};
    js_skip(&j);
    return js_parse_val(&j);
}

static ExecResult execute_bytecode_val(Val *bc, Env *global) {
    BytecodeParts parts = bytecode_from_val(bc);
    Frame *frame = frame_new(parts.instrs, parts.instr_count, parts.constants, parts.const_count, global, NULL);
    ExecResult result = exec_frame(frame);
    /* parts.constants 被 MAKE_FUNCTION 持有的 val_fn 共享，不可释放 */
    for (int i = 0; i < parts.instr_count; i++) {
        free(parts.instrs[i].op);
        free(parts.instrs[i].str_arg);
    }
    free(parts.instrs);
    return result;
}

static Instr instr0(const char *op) {
    Instr i = {0};
    i.op = strdup(op);
    return i;
}

static Instr instr_str(const char *op, const char *arg) {
    Instr i = instr0(op);
    i.str_arg = strdup(arg);
    i.has_str = 1;
    return i;
}

static Instr instr_num(const char *op, double arg) {
    Instr i = instr0(op);
    i.num_arg = arg;
    i.has_num = 1;
    return i;
}

static ExecResult execute_source_with_bootstrap(Env *global, const char *source) {
    Val **constants = calloc(1, sizeof(Val *));
    constants[0] = val_str(source);

    Instr *instrs = calloc(18, sizeof(Instr));
    int pc = 0;
    instrs[pc++] = instr_num("LOAD_CONST", 0);
    instrs[pc++] = instr_str("STORE_NAME", "source");
    instrs[pc++] = instr_str("LOAD_NAME", "lex");
    instrs[pc++] = instr_str("LOAD_NAME", "source");
    instrs[pc++] = instr_num("CALL", 1);
    instrs[pc++] = instr_str("STORE_NAME", "tokens");
    instrs[pc++] = instr_str("LOAD_NAME", "parse_tokens");
    instrs[pc++] = instr_str("LOAD_NAME", "tokens");
    instrs[pc++] = instr_num("CALL", 1);
    instrs[pc++] = instr_str("STORE_NAME", "ast");
    instrs[pc++] = instr_str("LOAD_NAME", "compile_ast");
    instrs[pc++] = instr_str("LOAD_NAME", "ast");
    instrs[pc++] = instr_num("CALL", 1);
    instrs[pc++] = instr_str("STORE_NAME", "bytecode");
    instrs[pc++] = instr_str("LOAD_NAME", "run_bytecode");
    instrs[pc++] = instr_str("LOAD_NAME", "bytecode");
    instrs[pc++] = instr_num("CALL", 1);
    instrs[pc++] = instr0("RETURN");

    Frame *frame = frame_new(instrs, pc, constants, 1, global, NULL);
    ExecResult result = exec_frame(frame);
    /* constants 被 MAKE_FUNCTION 持有的 val_fn 共享，不可释放 */
    for (int i = 0; i < pc; i++) { free(instrs[i].op); free(instrs[i].str_arg); }
    free(instrs);
    return result;
}

static void print_usage(const char *argv0) {
    fprintf(stderr, "用法:\n");
    fprintf(stderr, "  %s [bytecode.kub.json]\n", argv0);
    fprintf(stderr, "  %s --bootstrap frontend_bootstrap.kub.json [module.ku ...] program.ku\n", argv0);
}

/* ═══════════════════════════════════════════
 *  VM 执行
 * ═══════════════════════════════════════════ */

static ExecResult exec_frame(Frame *f) {
    ExecResult r = {0, 0, NULL, NULL};

    while (f->pc < f->instr_count) {
        Instr *instr = &f->instrs[f->pc];
        const char *op = instr->op;

        if (strcmp(op, "LOAD_CONST") == 0) {
            int idx = (int)instr->num_arg;
            frame_push(f, f->constants[idx]);
            f->pc++;
        }
        else if (strcmp(op, "LOAD_NAME") == 0) {
            Val *v = env_get(f->env, instr->str_arg);
            if (!v) {
                char message[256];
                snprintf(message, sizeof(message), "ku-vm: '%s' 未定义", instr->str_arg);
                if (frame_raise(f, message)) {
                    continue;
                }
                char error[320];
                snprintf(error, sizeof(error), "NameError: '%s' 未定义", instr->str_arg);
                return exec_error(error);
            }
            frame_push(f, v);
            f->pc++;
        }
        else if (strcmp(op, "STORE_NAME") == 0) {
            Val *v = frame_pop(f);
            env_set(f->env, instr->str_arg, v);
            f->pc++;
        }
        else if (strcmp(op, "STORE_FAST") == 0) {
            Val *v = frame_pop(f);
            env_set(f->env, instr->str_arg, v);
            f->pc++;
        }
        else if (strcmp(op, "BINARY_OP") == 0) {
            Val *right = frame_pop(f);
            Val *left = frame_pop(f);
            const char *op_s = instr->str_arg;

            if (strcmp(op_s, "+") == 0) {
                if (left->type == V_LIST && right->type == V_LIST) {
                    Val *out = val_list(left->len + right->len);
                    for (int i = 0; i < left->len; i++) list_push(out, left->items[i]);
                    for (int i = 0; i < right->len; i++) list_push(out, right->items[i]);
                    frame_push(f, out);
                } else if (left->type == V_STR || right->type == V_STR) {
                    char *left_s = val_to_string(left);
                    char *right_s = val_to_string(right);
                    size_t len = strlen(left_s) + strlen(right_s) + 1;
                    char *buf = malloc(len);
                    snprintf(buf, len, "%s%s", left_s, right_s);
                    frame_push(f, val_str(buf));
                    free(left_s);
                    free(right_s);
                    free(buf);
                } else {
                    frame_push(f, val_num(left->num + right->num));
                }
            }
            else if (strcmp(op_s, "-") == 0) frame_push(f, val_num(left->num - right->num));
            else if (strcmp(op_s, "*") == 0) frame_push(f, val_num(left->num * right->num));
            else if (strcmp(op_s, "/") == 0) frame_push(f, val_num(left->num / right->num));
            else if (strcmp(op_s, "%") == 0) frame_push(f, val_num(fmod(left->num, right->num)));
            else if (strcmp(op_s, "==") == 0 || strcmp(op_s, "=") == 0) frame_push(f, val_bool(val_equal(left, right)));
            else if (strcmp(op_s, "!=") == 0) frame_push(f, val_bool(!val_equal(left, right)));
            else if (strcmp(op_s, "<") == 0) frame_push(f, val_bool(left->num < right->num));
            else if (strcmp(op_s, ">") == 0) frame_push(f, val_bool(left->num > right->num));
            else if (strcmp(op_s, "<=") == 0) frame_push(f, val_bool(left->num <= right->num));
            else if (strcmp(op_s, ">=") == 0) frame_push(f, val_bool(left->num >= right->num));
            else if (strcmp(op_s, "and") == 0 || strcmp(op_s, "且") == 0) frame_push(f, val_bool(val_truthy(left) && val_truthy(right)));
            else if (strcmp(op_s, "or") == 0 || strcmp(op_s, "或") == 0) frame_push(f, val_bool(val_truthy(left) || val_truthy(right)));
            else {
                char message[256];
                snprintf(message, sizeof(message), "Unknown op: %s", op_s);
                if (frame_raise(f, message)) {
                    continue;
                }
                return exec_error(message);
            }
            f->pc++;
        }
        else if (strcmp(op, "UNARY_OP") == 0) {
            Val *v = frame_pop(f);
            if (strcmp(instr->str_arg, "not") == 0 || strcmp(instr->str_arg, "非") == 0) frame_push(f, val_bool(!val_truthy(v)));
            else if (strcmp(instr->str_arg, "-") == 0) frame_push(f, val_num(-v->num));
            f->pc++;
        }
        else if (strcmp(op, "POP") == 0) {
            if (f->sp > 0) frame_pop(f);
            f->pc++;
        }
        else if (strcmp(op, "DUP") == 0) {
            frame_push(f, frame_top(f));
            f->pc++;
        }
        else if (strcmp(op, "JUMP") == 0) {
            f->pc += (int)instr->num_arg;
        }
        else if (strcmp(op, "JUMP_IF_FALSE") == 0) {
            Val *v = frame_pop(f);
            int truthy = val_truthy(v);
            if (!truthy) f->pc += (int)instr->num_arg;
            else f->pc++;
        }
        else if (strcmp(op, "JUMP_IF_TRUE") == 0) {
            Val *v = frame_pop(f);
            if (val_truthy(v)) f->pc += (int)instr->num_arg;
            else f->pc++;
        }
        else if (strcmp(op, "MAKE_FUNCTION") == 0) {
            int idx = (int)instr->num_arg;
            Val *func_info = f->constants[idx];

            /* func_info 是一个 dict: {params, body_const_idx, ...} */
            /* body 是 constants[body_const_idx]，它是一个指令列表 */
            /* 但指令列表中的 LOAD_CONST 索引需要加上 const_offset */

            int body_idx = (int)dict_get(func_info, "body_const_idx")->num;
            int const_offset = 0;
            Val *co = dict_get(func_info, "const_offset");
            if (co && co->type == V_NUM) const_offset = (int)co->num;

            /* body 指令是 JSON 数组，需要解析 */
            /* 实际上 body 已经在 constants[body_idx] 中作为指令列表存在 */
            /* 但我们存储的是 Val*，需要从中提取 Instr* */

            /* 简化方案：将 body 作为原始 JSON 字符串存储，运行时再解析 */
            /* 或者：直接在 constants 中存储已解析的指令 */
            /* compiler.ku 的 constants[body_idx] 是一个指令列表 Val* */

            /* body 是一个 list，每个元素是一个 list [op, arg] */
            Val *body_val = f->constants[body_idx];

            /* 提取 params */
            Val *params_val = dict_get(func_info, "params");
            int param_count = params_val ? params_val->len : 0;
            char **params = calloc(param_count, sizeof(char *));
            for (int i = 0; i < param_count; i++) {
                params[i] = strdup(params_val->items[i]->str);
            }

            /* body_val 是一个 list，每个 item 是 [op, arg] 或 [op] 或 [op, null, arg] */
            int body_len = body_val->len;
            Instr *body = calloc(body_len, sizeof(Instr));
            for (int i = 0; i < body_len; i++) {
                Val *item = body_val->items[i];
                if (item->type == V_LIST && item->len >= 1) {
                    body[i].op = strdup(item->items[0]->str);
                    body[i].has_str = 0;
                    body[i].has_num = 0;
                    /* 3-element format: [op, null, real_arg] */
                    if (item->len >= 3 && item->items[1]->type == V_NIL) {
                        Val *arg = item->items[2];
                        if (arg->type == V_STR) {
                            body[i].str_arg = strdup(arg->str);
                            body[i].has_str = 1;
                        } else if (arg->type == V_NUM) {
                            body[i].num_arg = arg->num;
                            body[i].has_num = 1;
                        }
                    } else if (item->len >= 2) {
                        Val *arg = item->items[1];
                        if (arg->type == V_STR) {
                            body[i].str_arg = strdup(arg->str);
                            body[i].has_str = 1;
                        } else if (arg->type == V_NUM) {
                            body[i].num_arg = arg->num;
                            body[i].has_num = 1;
                        }
                    }
                }
            }

            /* 复制 body + 加偏移 */
            Instr *body_copy = calloc(body_len, sizeof(Instr));
            for (int i = 0; i < body_len; i++) {
                Val *item = body_val->items[i];
                if (item->type == V_LIST && item->len >= 1) {
                    body_copy[i].op = strdup(item->items[0]->str);
                    body_copy[i].has_str = 0;
                    body_copy[i].has_num = 0;
                    if (item->len >= 3 && item->items[1]->type == V_NIL) {
                        Val *arg = item->items[2];
                        if (arg->type == V_STR) { body_copy[i].str_arg = strdup(arg->str); body_copy[i].has_str = 1; }
                        else if (arg->type == V_NUM) { body_copy[i].num_arg = arg->num; body_copy[i].has_num = 1; }
                    } else if (item->len >= 2) {
                        Val *arg = item->items[1];
                        if (arg->type == V_STR) { body_copy[i].str_arg = strdup(arg->str); body_copy[i].has_str = 1; }
                        else if (arg->type == V_NUM) { body_copy[i].num_arg = arg->num; body_copy[i].has_num = 1; }
                    }
                }
            }
            int load_idx = 0;
            for (int i = 0; i < body_len; i++) {
                if (strcmp(body_copy[i].op, "LOAD_CONST") == 0 && body_copy[i].has_num) {
                    int raw_idx = (int)body_copy[i].num_arg;
                    if (raw_idx >= 0 && raw_idx < f->const_count) {
                        body_copy[i].num_arg = raw_idx;
                    } else {
                        body_copy[i].num_arg = const_offset + load_idx;
                    }
                    load_idx++;
                }
            }

            frame_push(f, val_fn(params, param_count, body_copy, body_len, f->constants, f->const_count, f->env));
            /* body 临时副本的使命已完成：body_copy 所有权已转入 val_fn，释放 body */
            for (int i = 0; i < body_len; i++) { free(body[i].op); free(body[i].str_arg); }
            free(body);
            f->pc++;
        }
        else if (strcmp(op, "CALL") == 0) {
            int argc = (int)instr->num_arg;
            Val **args = calloc(argc, sizeof(Val *));
            for (int i = argc - 1; i >= 0; i--) args[i] = frame_pop(f);
            Val *func = frame_pop(f);

            if (func->type == V_FN) {
                /* 创建新环境 */
                Env *call_env = env_new(func->closure);
                for (int i = 0; i < func->param_count && i < argc; i++) {
                    env_set(call_env, func->params[i], args[i]);
                }

                Frame *call_frame = frame_new(
                    func->body, func->body_len,
                    func->constants, func->const_count,
                    call_env, f
                );

                ExecResult cr = exec_frame(call_frame);
                if (cr.is_error) {
                    if (frame_raise(f, cr.error)) {
                        free(cr.error);
                        free(args);
                        continue;
                    }
                    free(args);
                    return cr;
                }
                frame_push(f, cr.val);
            } else if (func->type == V_STR) {
                /* 内置函数查找 */
                Val *result = NULL;
                const char *name = func->str;
                if (strcmp(name, "len") == 0) result = builtin_len(args, argc);
                else if (strcmp(name, "push") == 0) result = builtin_push(args, argc);
                else if (strcmp(name, "str") == 0) result = builtin_str(args, argc);
                else if (strcmp(name, "str_split") == 0) result = builtin_str_split(args, argc);
                else if (strcmp(name, "str_contains") == 0) result = builtin_str_contains(args, argc);
                else if (strcmp(name, "str_trim") == 0) result = builtin_str_trim(args, argc);
                else if (strcmp(name, "str_is_empty") == 0) result = builtin_str_is_empty(args, argc);
                else if (strcmp(name, "str_replace") == 0) result = builtin_str_replace(args, argc);
                else if (strcmp(name, "str_starts_with") == 0) result = builtin_str_starts_with(args, argc);
                else if (strcmp(name, "str_ends_with") == 0) result = builtin_str_ends_with(args, argc);
                else if (strcmp(name, "str_upper") == 0) result = builtin_str_upper(args, argc);
                else if (strcmp(name, "str_lower") == 0) result = builtin_str_lower(args, argc);
                else if (strcmp(name, "int") == 0) result = builtin_int(args, argc);
                else if (strcmp(name, "float") == 0) result = builtin_float(args, argc);
                else if (strcmp(name, "abs") == 0) result = builtin_abs(args, argc);
                else if (strcmp(name, "floor") == 0) result = builtin_floor(args, argc);
                else if (strcmp(name, "slice") == 0) result = builtin_slice(args, argc);
                else if (strcmp(name, "ord") == 0) result = builtin_ord(args, argc);
                else if (strcmp(name, "chr") == 0) result = builtin_chr(args, argc);
                else if (strcmp(name, "is_str") == 0) result = builtin_is_str(args, argc);
                else if (strcmp(name, "is_list") == 0) result = builtin_is_list(args, argc);
                else if (strcmp(name, "is_dict") == 0) result = builtin_is_dict(args, argc);
                else if (strcmp(name, "is_none") == 0) result = builtin_is_none(args, argc);
                else if (strcmp(name, "has") == 0) result = builtin_has(args, argc);
                else if (strcmp(name, "keys") == 0) result = builtin_keys(args, argc);
                else if (strcmp(name, "items") == 0) result = builtin_items(args, argc);
                else if (strcmp(name, "type") == 0) result = builtin_type(args, argc);
                else if (strcmp(name, "print") == 0) result = builtin_print(args, argc);
                else if (strcmp(name, "and") == 0 || strcmp(name, "且") == 0) result = builtin_and(args, argc);
                else if (strcmp(name, "or") == 0 || strcmp(name, "或") == 0) result = builtin_or(args, argc);
                else if (strcmp(name, "not") == 0 || strcmp(name, "非") == 0) result = builtin_not(args, argc);
                else if (strcmp(name, "path_exists") == 0) result = builtin_path_exists(args, argc);
                else if (strcmp(name, "read_file") == 0) result = builtin_read_file(args, argc);
                else if (strcmp(name, "write_file") == 0) result = builtin_write_file(args, argc);
                else if (strcmp(name, "delete_file") == 0) result = builtin_delete_file(args, argc);
                else if (strcmp(name, "mkdir") == 0) result = builtin_mkdir(args, argc);
                else if (strcmp(name, "list_dir") == 0) result = builtin_list_dir(args, argc);
                else if (strcmp(name, "now") == 0) result = builtin_now(args, argc);
                else if (strcmp(name, "now_fmt") == 0) result = builtin_now_fmt(args, argc);
                else if (strcmp(name, "sqlite_open") == 0) result = builtin_sqlite_open(args, argc);
                else if (strcmp(name, "sqlite_exec") == 0) {
                    ExecResult sr = builtin_sqlite_exec(args, argc);
                    if (sr.is_error) {
                        if (frame_raise(f, sr.error)) { free(sr.error); free(args); continue; }
                        free(args);
                        return sr;
                    }
                    result = sr.val;
                }
                else if (strcmp(name, "sqlite_query") == 0) {
                    ExecResult sr = builtin_sqlite_query(args, argc);
                    if (sr.is_error) {
                        if (frame_raise(f, sr.error)) { free(sr.error); free(args); continue; }
                        free(args);
                        return sr;
                    }
                    result = sr.val;
                }
                else if (strcmp(name, "sqlite_close") == 0) result = builtin_sqlite_close(args, argc);
                else if (strcmp(name, "dao_data_dir") == 0) result = builtin_dao_data_dir(args, argc);
                else if (strcmp(name, "dao_data_path") == 0) result = builtin_dao_data_path(args, argc);
                else if (strcmp(name, "system") == 0) result = builtin_system(args, argc);
                else if (strcmp(name, "parse") == 0) {
                    ExecResult pr = builtin_parse(args, argc, f);
                    if (pr.is_error) {
                        if (frame_raise(f, pr.error)) {
                            free(pr.error);
                            free(args);
                            continue;
                        }
                        free(args);
                        return pr;
                    }
                    result = pr.val;
                }
                else if (strcmp(name, "list_thoughts") == 0) result = builtin_list_thoughts(f->env);
                else if (strcmp(name, "json_parse") == 0) result = builtin_json_parse(args, argc);
                else if (strcmp(name, "json_stringify") == 0) result = builtin_json_stringify(args, argc);
                else if (strcmp(name, "url_encode") == 0) result = builtin_url_encode(args, argc);
                else if (strcmp(name, "url_decode") == 0) result = builtin_url_decode(args, argc);
                else if (strcmp(name, "http_get") == 0) result = builtin_http_get(args, argc);
                else if (strcmp(name, "http_post") == 0) result = builtin_http_post(args, argc);
                else if (strcmp(name, "http_put") == 0) result = builtin_http_put(args, argc);
                else if (strcmp(name, "http_delete") == 0) result = builtin_http_delete(args, argc);
                else if (strcmp(name, "run_bytecode") == 0) result = builtin_run_bytecode(args, argc, f->env);
                else {
                    char message[256];
                    snprintf(message, sizeof(message), "NameError: 未定义函数: %s", name);
                    if (frame_raise(f, message)) {
                        free(args);
                        continue;
                    }
                    free(args);
                    return exec_error(message);
                }
                frame_push(f, result);
            } else {
                if (frame_raise(f, "value is not callable")) {
                    free(args);
                    continue;
                }
                free(args);
                return exec_error("value is not callable");
            }
            free(args);
            f->pc++;
        }
        else if (strcmp(op, "RETURN") == 0) {
            r.is_return = 1;
            r.val = f->sp > 0 ? frame_pop(f) : val_nil();
            return r;
        }
        else if (strcmp(op, "BUILD_LIST") == 0) {
            int n = (int)instr->num_arg;
            Val *list = val_list(n);
            Val **items = calloc(n, sizeof(Val *));
            for (int i = n - 1; i >= 0; i--) items[i] = frame_pop(f);
            for (int i = 0; i < n; i++) list_push(list, items[i]);
            free(items);
            frame_push(f, list);
            f->pc++;
        }
        else if (strcmp(op, "BUILD_DICT") == 0) {
            int n = (int)instr->num_arg;
            Val *dict = val_dict();
            for (int i = 0; i < n; i++) {
                Val *val = frame_pop(f);
                Val *key = frame_pop(f);
                dict_set(dict, key->str, val);
            }
            frame_push(f, dict);
            f->pc++;
        }
        else if (strcmp(op, "GET_INDEX") == 0) {
            Val *idx = frame_pop(f);
            Val *obj = frame_pop(f);
            if (obj->type == V_LIST) {
                int i = (int)idx->num;
                if (i < 0) i += obj->len;
                if (i < 0 || i >= obj->len) {
                    if (!frame_raise(f, "list index out of range")) {
                        return exec_error("list index out of range");
                    }
                    continue;
                }
                frame_push(f, obj->items[i]);
            } else if (obj->type == V_STR) {
                int i = (int)idx->num;
                int len = utf8_strlen_chars(obj->str);
                if (i < 0) i += len;
                if (i < 0 || i >= len) {
                    if (!frame_raise(f, "string index out of range")) {
                        return exec_error("string index out of range");
                    }
                    continue;
                }
                char *buf = utf8_substr_chars(obj->str, i, i + 1);
                frame_push(f, val_str(buf));
                free(buf);
            } else if (obj->type == V_DICT) {
                frame_push(f, dict_get(obj, idx->str));
            } else {
                if (!frame_raise(f, "object is not indexable")) {
                    return exec_error("object is not indexable");
                }
                continue;
            }
            f->pc++;
        }
        else if (strcmp(op, "SET_INDEX") == 0) {
            Val *val = frame_pop(f);
            Val *idx = frame_pop(f);
            Val *obj = frame_pop(f);
            if (obj->type == V_LIST) {
                int i = (int)idx->num;
                if (i < 0) i += obj->len;
                if (i < 0 || i >= obj->len) {
                    if (!frame_raise(f, "list assignment index out of range")) {
                        return exec_error("list assignment index out of range");
                    }
                    continue;
                }
                obj->items[i] = val;
            } else if (obj->type == V_DICT) {
                dict_set(obj, idx->str, val);
            } else {
                if (!frame_raise(f, "object does not support item assignment")) {
                    return exec_error("object does not support item assignment");
                }
                continue;
            }
            frame_push(f, val);
            f->pc++;
        }
        else if (strcmp(op, "GET_ATTR") == 0) {
            Val *obj = frame_pop(f);
            if (obj->type == V_DICT) {
                frame_push(f, dict_get(obj, instr->str_arg));
            } else {
                frame_push(f, val_nil());
            }
            f->pc++;
        }
        else if (strcmp(op, "LOOP_BEGIN") == 0) {
            /* 标记循环开始 - 用于 break/continue */
            f->pc++;
        }
        else if (strcmp(op, "LOOP_END") == 0) {
            /* 标记循环结束 */
            f->pc++;
        }
        else if (strcmp(op, "GET_ITER") == 0) {
            /* pop iterable, push iterator */
            Val *iterable = frame_pop(f);
            if (iterable->type == V_LIST) {
                /* 创建简单迭代器：存储索引和列表 */
                Val *iter = val_dict();
                dict_set(iter, "__list__", iterable);
                dict_set(iter, "__idx__", val_num(0));
                frame_push(f, iter);
            } else {
                frame_push(f, val_nil());
            }
            f->pc++;
        }
        else if (strcmp(op, "FOR_ITER") == 0) {
            /* 迭代器取下一个元素 */
            Val *iter = frame_top(f);
            if (iter->type == V_DICT) {
                Val *list_val = dict_get(iter, "__list__");
                Val *idx_val = dict_get(iter, "__idx__");
                int idx = (int)idx_val->num;
                if (list_val && idx < list_val->len) {
                    frame_push(f, list_val->items[idx]);
                    idx_val->num = idx + 1;
                    f->pc++;
                } else {
                    /* 迭代结束，弹出迭代器，跳转 */
                    frame_pop(f);
                    f->pc += (int)instr->num_arg;
                }
            } else {
                frame_pop(f);
                f->pc += (int)instr->num_arg;
            }
        }
        else if (strcmp(op, "TRY_BEGIN") == 0) {
            if (f->try_sp >= TRY_MAX) {
                return exec_error("try stack overflow");
            }
            f->try_stack[f->try_sp++] = f->pc + (int)instr->num_arg;
            f->pc++;
        }
        else if (strcmp(op, "TRY_END") == 0) {
            if (f->try_sp > 0) f->try_sp--;
            f->pc++;
        }
        else if (strcmp(op, "RAISE") == 0) {
            Val *exc = frame_pop(f);
            char *message = val_to_string(exc);
            if (!frame_raise(f, message)) {
                r.is_error = 1;
                r.error = message;
                return r;
            }
            free(message);
        }
        else if (strcmp(op, "NOP") == 0) {
            f->pc++;
        }
        else {
            char message[256];
            snprintf(message, sizeof(message), "未知指令: %s", op);
            return exec_error(message);
        }
    }

    r.val = f->sp > 0 ? frame_top(f) : val_nil();
    return r;
}

/* ═══════════════════════════════════════════
 *  M4: 竞技场批量回收
 *  释放各对象”严格自有”的标量缓冲：
 *    Frame 结构体
 *    V_FN 的 body（Instr 数组 + 每个 Instr 的 op/str_arg）— MAKE_FUNCTION 分配的 body_copy
 *    V_FN 的 params（字符串数组）— MAKE_FUNCTION 时 strdup 的副本
 *    env_set 的 names 字符串 + names/vals 数组
 *  绝不释放共享字段：
 *    V_FN.constants 指向 frame 的共享常量池（其它 Frame/V_FN 可能仍在引用）
 *    V_FN.closure 指向另一个 Env，由 env_arena 自己回收
 *  四个竞技场都是扁平列表，回收顺序与引用无关，天然免疫 env<->closure 环。
 * ═══════════════════════════════════════════ */
static void arena_freeall(void) {
    if (!g_arena_enabled) return;

    /* 第一遍：释放 V_FN 的 body 和 params（MAKE_FUNCTION 的独有副本） */
    for (long i = 0; i < g_val_arena.count; i++) {
        Val *v = (Val *)g_val_arena.items[i];
        if (!v || v->type != V_FN) continue;
        if (v->body) {
            for (int j = 0; j < v->body_len; j++) {
                if (v->body[j].op) free(v->body[j].op);
                if (v->body[j].str_arg) free(v->body[j].str_arg);
            }
            free(v->body);
        }
        if (v->params) {
            for (int j = 0; j < v->param_count; j++) {
                if (v->params[j]) free(v->params[j]);
            }
            free(v->params);
        }
        /* 不释放 v->constants（frame 共享）和 v->closure（env_arena 管理） */
    }
    /* 第二遍：释放 Val 的标量字段 */
    for (long i = 0; i < g_val_arena.count; i++) {
        Val *v = (Val *)g_val_arena.items[i];
        if (!v) continue;
        if (v->str) free(v->str);
        if (v->items) free(v->items);  /* 仅数组，不含元素 */
        DictEntry *e = v->entries;
        while (e) {
            DictEntry *next = e->next;
            if (e->key) free(e->key);
            free(e);
            e = next;
        }
        free(v);
        g_val_freed++;
    }
    free(g_val_arena.items);
    g_val_arena.items = NULL;
    g_val_arena.count = g_val_arena.cap = 0;

    /* env_set 批次：释放 Env 内的 names 字符串和指针数组 */
    for (long i = 0; i < g_env_arena.count; i++) {
        Env *e = (Env *)g_env_arena.items[i];
        if (!e) continue;
        for (int n = 0; n < e->count; n++) {
            if (e->names && e->names[n]) free(e->names[n]);
        }
        if (e->names) free(e->names);
        if (e->vals) free(e->vals);  /* 仅指针数组，不含 Val 本体 */
        free(e);
        g_env_freed++;
    }
    free(g_env_arena.items);
    g_env_arena.items = NULL;
    g_env_arena.count = g_env_arena.cap = 0;

    /* Frame 批次：Frame 不含自有堆字段 */
    for (long i = 0; i < g_frame_arena.count; i++) {
        Frame *f = (Frame *)g_frame_arena.items[i];
        if (!f) continue;
        free(f);
        g_frame_freed++;
    }
    free(g_frame_arena.items);
    g_frame_arena.items = NULL;
    g_frame_arena.count = g_frame_arena.cap = 0;
}

static void arena_report_stats(void) {
    const char *flag = getenv("DAO_GC_STATS");
    if (!flag || flag[0] == '\0' || flag[0] == '0') return;
    fprintf(stderr,
            "[dao-gc] val: alloc=%ld free=%ld leak=%ld | env: alloc=%ld free=%ld leak=%ld | frame: alloc=%ld free=%ld leak=%ld\n",
            g_val_allocated, g_val_freed, g_val_allocated - g_val_freed,
            g_env_allocated, g_env_freed, g_env_allocated - g_env_freed,
            g_frame_allocated, g_frame_freed, g_frame_allocated - g_frame_freed);
}

/* ═══════════════════════════════════════════
 *  主入口
 * ═══════════════════════════════════════════ */

int main(int argc, char **argv) {
    init_exe_dir(argc > 0 ? argv[0] : NULL);
    /* 创建全局环境并注册内置函数 */
    Env *global = env_new(NULL);
    register_builtins(global);

    char *input = NULL;
    char *source = NULL;
    ExecResult result = {0, 0, NULL, NULL};

    if (argc >= 4 && strcmp(argv[1], "--bootstrap") == 0) {
        input = read_text_file(argv[2], NULL);
        if (!input) return 1;
        Val *bootstrap_bc = parse_json_value(input);
        result = execute_bytecode_val(bootstrap_bc, global);
        if (result.is_error) {
            fprintf(stderr, "RuntimeError: %s\n", result.error);
            free(result.error);
            free(input);
            return 1;
        }

        char *repo_root = repo_root_from_bootstrap(argv[2]);
        source = read_source_files(argc - 3, argv + 3, repo_root);
        free(repo_root);
        if (!source) {
            free(input);
            return 1;
        }
        result = execute_source_with_bootstrap(global, source);
    } else if (argc == 1 || argc == 2) {
        input = argc == 2 ? read_text_file(argv[1], NULL) : read_stdin_all(NULL);
        if (!input) return 1;
        Val *bc = parse_json_value(input);
        result = execute_bytecode_val(bc, global);
    } else {
        print_usage(argv[0]);
        return 2;
    }

    if (result.is_error) {
        fprintf(stderr, "RuntimeError: %s\n", result.error);
        free(result.error);
        free(input);
        free(source);
        arena_freeall();
        arena_report_stats();
        return 1;
    }

    /* 输出结果 */
    val_print(result.val);
    printf("\n");

    free(input);
    free(source);
    arena_freeall();
    arena_report_stats();
    return 0;
}
