#include "kyroolib.h"
#include "stdio.h"
#include "stdlib.h"

#define MAX_VARS 128
#define MAX_LINE 512

typedef enum {
    LUA_VAL_NIL,
    LUA_VAL_NUM,
    LUA_VAL_STR,
    LUA_VAL_BOOL
} val_type_t;

typedef struct {
    val_type_t type;
    union {
        double num;
        char str[128];
        int boolean;
    } as;
} lua_val_t;

typedef struct {
    char name[32];
    lua_val_t val;
} lua_var_t;

static lua_var_t g_vars[MAX_VARS];
static int g_var_count = 0;

static void set_var(const char *name, lua_val_t v) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            g_vars[i].val = v;
            return;
        }
    }
    if (g_var_count < MAX_VARS) {
        strncpy(g_vars[g_var_count].name, name, 31);
        g_vars[g_var_count].val = v;
        g_var_count++;
    }
}

static lua_val_t get_var(const char *name) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return g_vars[i].val;
        }
    }
    lua_val_t nil_val = {.type = LUA_VAL_NIL};
    return nil_val;
}

static void print_val(lua_val_t v) {
    if (v.type == LUA_VAL_NUM) {
        if ((double)(long)v.as.num == v.as.num) {
            printf("%ld", (long)v.as.num);
        } else {
            printf("%f", (float)v.as.num);
        }
    } else if (v.type == LUA_VAL_STR) {
        printf("%s", v.as.str);
    } else if (v.type == LUA_VAL_BOOL) {
        printf("%s", v.as.boolean ? "true" : "false");
    } else {
        printf("nil");
    }
}

static lua_val_t eval_expr(const char *expr) {
    while (*expr == ' ') expr++;
    lua_val_t res = {.type = LUA_VAL_NIL};

    if (*expr == '"' || *expr == '\'') {
        char quote = *expr++;
        res.type = LUA_VAL_STR;
        int i = 0;
        while (*expr && *expr != quote && i < 127) {
            res.as.str[i++] = *expr++;
        }
        res.as.str[i] = '\0';
        return res;
    }

    if (strcmp(expr, "true") == 0) {
        res.type = LUA_VAL_BOOL;
        res.as.boolean = 1;
        return res;
    }
    if (strcmp(expr, "false") == 0) {
        res.type = LUA_VAL_BOOL;
        res.as.boolean = 0;
        return res;
    }

    // Check for arithmetic: +, -, *, /
    char left[64], right[64], op = 0;
    const char *p = expr;
    int paren = 0;
    while (*p) {
        if (*p == '(') paren++;
        else if (*p == ')') paren--;
        else if (paren == 0 && (*p == '+' || *p == '-' || *p == '*' || *p == '/')) {
            op = *p;
            size_t l_len = p - expr;
            if (l_len >= sizeof(left)) l_len = sizeof(left) - 1;
            strncpy(left, expr, l_len);
            left[l_len] = '\0';
            strncpy(right, p + 1, sizeof(right) - 1);
            right[sizeof(right) - 1] = '\0';
            break;
        }
        p++;
    }

    if (op) {
        lua_val_t vl = eval_expr(left);
        lua_val_t vr = eval_expr(right);
        if (vl.type == LUA_VAL_NUM && vr.type == LUA_VAL_NUM) {
            res.type = LUA_VAL_NUM;
            if (op == '+') res.as.num = vl.as.num + vr.as.num;
            else if (op == '-') res.as.num = vl.as.num - vr.as.num;
            else if (op == '*') res.as.num = vl.as.num * vr.as.num;
            else if (op == '/' && vr.as.num != 0) res.as.num = vl.as.num / vr.as.num;
            return res;
        } else if (op == '+' && (vl.type == LUA_VAL_STR || vr.type == LUA_VAL_STR)) {
            res.type = LUA_VAL_STR;
            snprintf(res.as.str, sizeof(res.as.str), "%s%s",
                     vl.type == LUA_VAL_STR ? vl.as.str : "",
                     vr.type == LUA_VAL_STR ? vr.as.str : "");
            return res;
        }
    }

    // Number or variable
    if ((*expr >= '0' && *expr <= '9') || *expr == '-') {
        res.type = LUA_VAL_NUM;
        res.as.num = atoi(expr);
        return res;
    }

    // Variable lookup
    char vname[32];
    int vi = 0;
    while (*expr && *expr != ' ' && *expr != ')' && vi < 31) {
        vname[vi++] = *expr++;
    }
    vname[vi] = '\0';
    return get_var(vname);
}

static void exec_line(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#' || (line[0] == '-' && line[1] == '-')) return; // Comments/empty

    // print(...)
    if (strncmp(line, "print(", 6) == 0) {
        const char *arg = line + 6;
        char buf[128];
        int bi = 0;
        while (*arg && *arg != ')' && bi < 127) {
            buf[bi++] = *arg++;
        }
        buf[bi] = '\0';
        lua_val_t v = eval_expr(buf);
        print_val(v);
        printf("\n");
        return;
    }

    // Assignment: var = expr
    char var[32];
    int vi = 0;
    const char *p = line;
    while (*p && *p != ' ' && *p != '=' && vi < 31) {
        var[vi++] = *p++;
    }
    var[vi] = '\0';
    while (*p == ' ') p++;
    if (*p == '=') {
        p++;
        while (*p == ' ') p++;
        lua_val_t val = eval_expr(p);
        set_var(var, val);
        return;
    }

    // os.execute(...) or system call
    if (strncmp(line, "os.execute(", 11) == 0) {
        const char *arg = line + 11;
        char cmd[128];
        int ci = 0;
        if (*arg == '"' || *arg == '\'') arg++;
        while (*arg && *arg != '"' && *arg != '\'' && *arg != ')' && ci < 127) {
            cmd[ci++] = *arg++;
        }
        cmd[ci] = '\0';
        char *argv[] = {cmd, NULL};
        execve(cmd, argv, NULL);
        return;
    }

    lua_val_t v = eval_expr(line);
    if (v.type != LUA_VAL_NIL) {
        print_val(v);
        printf("\n");
    }
}

static void run_script(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("lua: cannot open %s\n", filename);
        return;
    }
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        exec_line(line);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "-e") == 0 && argc > 2) {
            exec_line(argv[2]);
            return 0;
        }
        run_script(argv[1]);
        return 0;
    }

    printf("Lua 5.3 (KyroOS Carbon embedded script engine)\n");
    printf("Type 'exit' to quit.\n");

    char line[MAX_LINE];
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
        exec_line(line);
    }
    return 0;
}
