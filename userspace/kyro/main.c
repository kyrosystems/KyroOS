#include <kyroolib.h>

#define MAX_VARS 64
#define MAX_VAR_NAME 32
#define MAX_VAR_VAL 256
#define MAX_LINE 512
#define MAX_ARGS 32

typedef struct {
    char name[MAX_VAR_NAME];
    char val[MAX_VAR_VAL];
} kyro_var_t;

static kyro_var_t vars[MAX_VARS];
static int var_count = 0;

static const char *get_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            return vars[i].val;
        }
    }
    return "";
}

static void set_var(const char *name, const char *val) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            strncpy(vars[i].val, val, MAX_VAR_VAL - 1);
            vars[i].val[MAX_VAR_VAL - 1] = '\0';
            return;
        }
    }
    if (var_count < MAX_VARS) {
        strncpy(vars[var_count].name, name, MAX_VAR_NAME - 1);
        vars[var_count].name[MAX_VAR_NAME - 1] = '\0';
        strncpy(vars[var_count].val, val, MAX_VAR_VAL - 1);
        vars[var_count].val[MAX_VAR_VAL - 1] = '\0';
        var_count++;
    }
}

// Substitute $VAR with its value
static void expand_vars(const char *input, char *output, size_t max_len) {
    size_t out_idx = 0;
    const char *p = input;

    while (*p && out_idx < max_len - 1) {
        if (*p == '$' && ((*(p + 1) >= 'a' && *(p + 1) <= 'z') ||
                          (*(p + 1) >= 'A' && *(p + 1) <= 'Z') ||
                          (*(p + 1) == '_'))) {
            p++;
            char vname[MAX_VAR_NAME];
            int vi = 0;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') {
                if (vi < MAX_VAR_NAME - 1) vname[vi++] = *p;
                p++;
            }
            vname[vi] = '\0';
            const char *val = get_var(vname);
            while (*val && out_idx < max_len - 1) {
                output[out_idx++] = *val++;
            }
        } else {
            output[out_idx++] = *p++;
        }
    }
    output[out_idx] = '\0';
}

static int execute_line(char *line);

// Parse and execute a single statement/command
static int execute_statement(char *stmt) {
    while (*stmt == ' ' || *stmt == '\t') stmt++;
    if (*stmt == '\0' || *stmt == '#') return 0;

    // Check for "set NAME = VALUE" or "NAME=VALUE"
    if (strncmp(stmt, "set ", 4) == 0) {
        char *eq = strchr(stmt + 4, '=');
        if (eq) {
            *eq = '\0';
            char *vname = stmt + 4;
            char *vval = eq + 1;
            while (*vname == ' ' || *vname == '\t') vname++;
            char *vname_end = vname + strlen(vname) - 1;
            while (vname_end > vname && (*vname_end == ' ' || *vname_end == '\t')) *vname_end-- = '\0';
            while (*vval == ' ' || *vval == '\t') vval++;
            char exp_val[MAX_VAR_VAL];
            expand_vars(vval, exp_val, sizeof(exp_val));
            set_var(vname, exp_val);
            return 0;
        }
    }

    char exp_line[MAX_LINE];
    expand_vars(stmt, exp_line, sizeof(exp_line));

    char *p = exp_line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') return 0;

    // Builtin: echo
    if (strncmp(p, "echo ", 5) == 0 || strcmp(p, "echo") == 0) {
        const char *msg = (p[4] == ' ') ? p + 5 : "";
        print(msg);
        print("\n");
        print_flush();
        return 0;
    }

    // Builtin: sleep / delay
    if (strncmp(p, "sleep ", 6) == 0 || strncmp(p, "delay ", 6) == 0) {
        int ms = atoi(p + 6);
        if (ms <= 0) ms = 1;
        // sleep_ms or busy wait loop with timer ticks
        uint64_t start = get_ticks();
        uint64_t wait_ticks = (uint64_t)(ms / 10);
        if (wait_ticks == 0) wait_ticks = 1;
        while (get_ticks() - start < wait_ticks) {
            sleep_ms(10);
        }
        return 0;
    }

    // Builtin: exit
    if (strncmp(p, "exit", 4) == 0) {
        int code = (p[4] == ' ') ? atoi(p + 5) : 0;
        exit(code);
    }

    // External binary execution via execve
    char *args[MAX_ARGS];
    int argc = 0;
    char *tok = p;
    while (*tok && argc < MAX_ARGS - 1) {
        while (*tok == ' ' || *tok == '\t') tok++;
        if (*tok == '\0') break;
        char *start = tok;
        while (*tok && *tok != ' ' && *tok != '\t') tok++;
        if (*tok) { *tok = '\0'; tok++; }
        args[argc++] = start;
    }
    args[argc] = NULL;
    if (argc == 0) return 0;

    int pid = execve(args[0], args, NULL);
    if (pid < 0) {
        char bin_path[128];
        snprintf(bin_path, sizeof(bin_path), "/bin/%s", args[0]);
        pid = execve(bin_path, args, NULL);
    }

    if (pid < 0) {
        printf("kyro: %s: command not found\n", args[0]);
        print_flush();
        return 127;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

// Execute script buffer
static int run_script_content(const char *content) {
    const char *p = content;
    char line_buf[MAX_LINE];

    // Initialize built-in environment variables
    set_var("OS", "KyroOS");
    set_var("VERSION", "26.03.12");
    set_var("ARCH", "x86_64");

    while (*p) {
        int idx = 0;
        while (*p && *p != '\n' && *p != '\r' && idx < MAX_LINE - 1) {
            line_buf[idx++] = *p++;
        }
        line_buf[idx] = '\0';
        if (*p == '\r') p++;
        if (*p == '\n') p++;

        char *s = line_buf;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\0' || *s == '#') continue;

        // Skip shebang
        if (s[0] == '#' && s[1] == '!') continue;

        // repeat N { statement } loop support
        if (strncmp(s, "repeat ", 7) == 0 || strncmp(s, "loop ", 5) == 0) {
            char *num_start = strchr(s, ' ');
            if (num_start) {
                while (*num_start == ' ') num_start++;
                int count = atoi(num_start);
                char *brace = strchr(num_start, '{');
                if (brace) {
                    char *close_b = strchr(brace + 1, '}');
                    if (close_b) {
                        *close_b = '\0';
                        char loop_body[MAX_LINE];
                        strncpy(loop_body, brace + 1, sizeof(loop_body) - 1);
                        loop_body[sizeof(loop_body) - 1] = '\0';
                        for (int i = 0; i < count; i++) {
                            char i_str[16];
                            sprintf(i_str, "%d", i);
                            set_var("INDEX", i_str);
                            char body_copy[MAX_LINE];
                            strncpy(body_copy, loop_body, sizeof(body_copy) - 1);
                            execute_statement(body_copy);
                        }
                        continue;
                    }
                }
            }
        }

        execute_statement(s);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: kyro [SCRIPT.ks | SCRIPT.kyro | SCRIPT.sh] [-c \"commands\"]\n");
        print("KyroScript - Native High-Performance Scripting Engine for KyroOS.\n");
        print("Features: Shebang (#!/bin/kyro), variables, repeat loops, command execution.\n");
        print_flush();
        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        return run_script_content(argv[2]);
    }

    if (argc > 1) {
        const char *filepath = argv[1];
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) {
            printf("kyro: cannot open script '%s'\n", filepath);
            print_flush();
            return 1;
        }

        char buffer[16384];
        int n = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);

        if (n <= 0) {
            return 0;
        }

        buffer[n] = '\0';
        return run_script_content(buffer);
    }

    // Interactive Kyro REPL if invoked with no arguments
    print("\033[1;36m============================================================\033[0m\n");
    print("\033[1;32m           KyroScript Interactive Shell v1.0                \033[0m\n");
    print("\033[1;36m============================================================\033[0m\n");
    print("Type 'exit' to quit. Variables: set NAME = val. Loops: repeat 3 { echo hi }\n\n");
    print_flush();

    set_var("OS", "KyroOS");
    set_var("VERSION", "26.03.12");
    set_var("ARCH", "x86_64");

    char line[MAX_LINE];
    while (1) {
        print("\033[1;35mkyro>\033[0m ");
        print_flush();

        int idx = 0;
        while (idx < MAX_LINE - 1) {
            char ch = 0;
            int n = read(0, &ch, 1);
            if (n <= 0) continue;
            if (ch == '\r' || ch == '\n') {
                print("\n");
                print_flush();
                break;
            } else if (ch == '\b' || ch == 127) {
                if (idx > 0) {
                    idx--;
                    print("\b \b");
                    print_flush();
                }
            } else if (ch >= 32 && ch <= 126) {
                line[idx++] = ch;
                char out[2] = {ch, '\0'};
                print(out);
                print_flush();
            }
        }
        line[idx] = '\0';
        if (line[0] == '\0') continue;
        execute_statement(line);
    }

    return 0;
}
