/**
 * dao_ipc.c — C 引擎 IPC 守护程序
 * 精简稳定版：无复杂内存共享，纯求值后输出。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>

enum { T_ERR, T_NUM, T_SYM, T_SEXPR, T_FUN, T_STR };
#define ERR_MAX 512

typedef struct Val Val;
typedef struct Env Env;
typedef Val*(*Builtin)(Env*, Val*);

struct Val {
    int t;
    double num; char* sym; char* str;
    Builtin fn; Env* env;
    int n; Val** c;
};
struct Env {
    Env* parent;
    int n; char** syms; Val** vals;
};

Val* val_err(char* fmt, ...) {
    Val* v = calloc(1, sizeof(Val));
    v->t = T_ERR; v->str = malloc(ERR_MAX);
    va_list a; va_start(a, fmt); vsnprintf(v->str, ERR_MAX, fmt, a); va_end(a);
    return v;
}
Val* val_num(double x) { Val* v = calloc(1, sizeof(Val)); v->t = T_NUM; v->num = x; return v; }
Val* val_sym(char* s) { Val* v = calloc(1, sizeof(Val)); v->t = T_SYM; v->sym = strdup(s); return v; }
Val* val_sexpr(void) { Val* v = calloc(1, sizeof(Val)); v->t = T_SEXPR; return v; }
Val* val_fn(Builtin f) { Val* v = calloc(1, sizeof(Val)); v->t = T_FUN; v->fn = f; return v; }
void val_add(Val* v, Val* x) { v->n++; v->c = realloc(v->c, sizeof(Val*)*v->n); v->c[v->n-1] = x; }

void val_del(Val* v) {
    /* 进程退出时 OS 回收，不手动释放 */
}
void env_del(Env* e) {
    /* 进程退出时 OS 回收，不手动释放 */
}

void val_to_str(Val* v, char* b, int sz) {
    switch (v->t) {
        case T_NUM: snprintf(b, sz, "%g", v->num); return;
        case T_SYM: snprintf(b, sz, "%s", v->sym); return;
        case T_ERR: snprintf(b, sz, "Error: %s", v->str); return;
        case T_STR: snprintf(b, sz, "\"%s\"", v->str); return;
        case T_SEXPR: {
            int n = snprintf(b, sz, "(");
            for (int i=0; i<v->n; i++) {
                if (i) n += snprintf(b+n, sz-n, " ");
                val_to_str(v->c[i], b+n, sz-n);
                n = strlen(b);
            }
            snprintf(b+n, sz-n, ")");
            return;
        }
        case T_FUN: snprintf(b, sz, "<fn>"); return;
    }
}

Env* env_new(void) { return calloc(1, sizeof(Env)); }
void env_put(Env* e, char* name, Val* v) {
    for (int i=0;i<e->n;i++)
        if (strcmp(e->syms[i], name) == 0) { val_del(e->vals[i]); e->vals[i] = v; return; }
    e->n++; e->syms = realloc(e->syms, sizeof(char*)*e->n);
    e->vals = realloc(e->vals, sizeof(Val*)*e->n);
    e->syms[e->n-1] = strdup(name);
    e->vals[e->n-1] = v;
}
Val* env_get(Env* e, char* name) {
    for (int i=0;i<e->n;i++)
        if (strcmp(e->syms[i], name) == 0) return e->vals[i];
    if (e->parent) return env_get(e->parent, name);
    return NULL;
}

/* ===== Parser ===== */
typedef struct { const char* s; int p; } Parse;
static Parse pa;

static void skip(void) {
    while (pa.s[pa.p]==' '||pa.s[pa.p]=='\t'||pa.s[pa.p]=='\n'||pa.s[pa.p]=='\r') pa.p++;
}
static Val* parse_num(void) {
    skip(); char* e; errno = 0;
    double v = strtod(pa.s+pa.p, &e);
    if (e == pa.s+pa.p || errno) return NULL;
    pa.p = e - pa.s;
    return val_num(v);
}
static Val* parse_sym(void) {
    skip(); int s = pa.p; char c;
    while ((c=pa.s[pa.p]) && c!=' ' && c!='\t' && c!='\n' && c!='\r' && c!='(' && c!=')') pa.p++;
    if (pa.p == s) return NULL;
    return val_sym(strndup(pa.s+s, pa.p-s));
}
static Val* parse_expr(void);
static Val* parse_sexpr(void) {
    skip(); if (pa.s[pa.p] != '(') return NULL;
    pa.p++; Val* v = val_sexpr();
    while (1) {
        skip(); if (pa.s[pa.p] == ')') { pa.p++; break; }
        Val* x = parse_expr(); if (!x) { val_del(v); return NULL; }
        val_add(v, x);
    }
    return v;
}
static Val* parse_expr(void) {
    Val* v;
    if ((v = parse_num())) return v;
    if ((v = parse_sym())) return v;
    if ((v = parse_sexpr())) return v;
    return NULL;
}

/* ===== Eval ===== */
Val* builtin_add(Env* e, Val* a);
Val* builtin_mul(Env* e, Val* a);
Val* builtin_sub(Env* e, Val* a);
Val* builtin_def(Env* e, Val* a);

Val* val_eval(Env* e, Val* v) {
    if (v->t == T_SYM) {
        Val* r = env_get(e, v->sym);
        if (!r) return val_err("未绑定符号: %s", v->sym);
        return r;
    }
    if (v->t == T_SEXPR) {
        if (v->n == 0) return val_sexpr();
        
        /* 特殊形式: def 不预求值参数 */
        if (v->c[0]->t == T_SYM && strcmp(v->c[0]->sym, "def") == 0) {
            if (v->n != 3 || v->c[1]->t != T_SYM) {
                return val_err("def 需要: 符号 值");
            }
            Val* val = val_eval(e, v->c[2]);
            if (val->t == T_ERR) return val;
            env_put(e, v->c[1]->sym, val);
            Val* r = val_sexpr();
            return r;
        }
        
        /* 普通求值：先求值第一个子节点（应得到函数） */
        Val* fn = val_eval(e, v->c[0]);
        if (fn->t == T_ERR) { val_del(v); return fn; }
        if (fn->t != T_FUN) { val_del(fn); val_del(v); return val_err("不是函数"); }
        
        /* 求值参数 */
        Val* args = val_sexpr();
        for (int i=1; i<v->n; i++) {
            Val* ev = val_eval(e, v->c[i]);
            if (ev->t == T_ERR) { val_del(args); val_del(fn); return ev; }
            /* 复制值离开调用者，确保 args 拥有独立所有权 */
            if (ev->t == T_NUM) ev = val_num(ev->num);
            val_add(args, ev);
        }
        
        /* 调用 */
        Val* r = fn->fn(e, args);
        val_del(args);  /* 调用者释放参数 */
        val_del(fn);
        return r;
    }
    return v;
}

Val* builtin_add(Env* e, Val* a) {
    double s = 0;
    for (int i=0;i<a->n;i++) {
        if (a->c[i]->t != T_NUM) return val_err("需要数字");
        s += a->c[i]->num;
    }
    return val_num(s);
}
Val* builtin_mul(Env* e, Val* a) {
    double s = 1;
    for (int i=0;i<a->n;i++) {
        if (a->c[i]->t != T_NUM) return val_err("需要数字");
        s *= a->c[i]->num;
    }
    return val_num(s);
}
Val* builtin_sub(Env* e, Val* a) {
    if (a->n == 0) return val_num(0);
    if (a->n == 1) { return val_num(-a->c[0]->num); }
    double s = a->c[0]->num;
    for (int i=1;i<a->n;i++) {
        if (a->c[i]->t != T_NUM) return val_err("需要数字");
        s -= a->c[i]->num;
    }
    return val_num(s);
}
Val* builtin_def(Env* e, Val* a) {
    if (a->n != 2 || a->c[0]->t != T_SYM) { return val_err("def 需要: 符号 值"); }
    /* 复制值以确保独立所有权 */
    Val* val = val_num(a->c[1]->num);
    env_put(e, a->c[0]->sym, val);
    return val_sexpr();
}

/* ===== Main ===== */
int main(void) {
    Env* e = env_new();
    env_put(e, "+", val_fn(builtin_add));
    env_put(e, "-", val_fn(builtin_sub));
    env_put(e, "*", val_fn(builtin_mul));
    env_put(e, "def", val_fn(builtin_def));
    
    char line[65536];
    while (fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        if (l > 0 && line[l-1] == '\n') line[l-1] = '\0';
        if (line[0] == '\0' || strcmp(line, "exit") == 0) break;
        
        pa.s = line; pa.p = 0;
        Val* parsed = parse_expr();
        if (!parsed) { printf("parse error\n"); fflush(stdout); continue; }
        
        Val* result = val_eval(e, parsed);
        
        char buf[ERR_MAX*2];
        val_to_str(result, buf, ERR_MAX*2);
        printf("%s\n", buf);
        fflush(stdout);
    }
    
    // 不释放 env——进程退出时 OS 会自动清理
    // env_del(e);  避免双释放
    return 0;
}
