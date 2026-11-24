#include "kyroolib.h"

#define MAX_LINE 512
#define MAX_ARGS 32
#define MAX_WINDOWS 8

static char current_hostname[64] = "kyroos";

typedef struct {
    int id;
    char name[32];
    char cwd[256];
    bool active;
} shell_window_t;

static shell_window_t windows[MAX_WINDOWS];
static int active_win = 0;

static void win_save_cwd(void) {
    char cur[256];
    if (getcwd(cur, sizeof(cur)) == 0 && cur[0]) {
        strncpy(windows[active_win].cwd, cur, sizeof(windows[active_win].cwd) - 1);
        windows[active_win].cwd[sizeof(windows[active_win].cwd) - 1] = '\0';
    }
}

static void win_show_status(void) {
    print("\033[44;37m [KyroOS Tmux] ");
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;
        if (i == active_win) {
            printf("\033[42;30m %d:%s* \033[44;37m", i, windows[i].name);
        } else {
            printf(" %d:%s ", i, windows[i].name);
        }
    }
    print(" (Ctrl+B then c/n/p/0-7 or 'win') \033[0m\n");
    print_flush();
}

static void win_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].id = i;
        windows[i].name[0] = '\0';
        windows[i].cwd[0] = '/';
        windows[i].cwd[1] = '\0';
        windows[i].active = false;
    }
    windows[0].active = true;
    strncpy(windows[0].name, "sh", sizeof(windows[0].name) - 1);
    windows[0].name[sizeof(windows[0].name) - 1] = '\0';
    active_win = 0;
}

static void win_switch(int target) {
    if (target < 0 || target >= MAX_WINDOWS || !windows[target].active) {
        printf("sh: window %d not active\n", target);
        print_flush();
        return;
    }
    win_save_cwd();
    active_win = target;
    chdir(windows[active_win].cwd);
    win_show_status();
}

static void win_next(void) {
    for (int i = 1; i <= MAX_WINDOWS; i++) {
        int idx = (active_win + i) % MAX_WINDOWS;
        if (windows[idx].active) {
            win_switch(idx);
            return;
        }
    }
}

static void win_prev(void) {
    for (int i = 1; i <= MAX_WINDOWS; i++) {
        int idx = (active_win - i + MAX_WINDOWS) % MAX_WINDOWS;
        if (windows[idx].active) {
            win_switch(idx);
            return;
        }
    }
}

static void win_new(const char *name) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            windows[i].active = true;
            if (name && name[0]) {
                strncpy(windows[i].name, name, sizeof(windows[i].name) - 1);
                windows[i].name[sizeof(windows[i].name) - 1] = '\0';
            } else {
                snprintf(windows[i].name, sizeof(windows[i].name), "sh-%d", i);
            }
            char cur[256];
            if (getcwd(cur, sizeof(cur)) == 0 && cur[0]) {
                strncpy(windows[i].cwd, cur, sizeof(windows[i].cwd) - 1);
            } else {
                strncpy(windows[i].cwd, "/", sizeof(windows[i].cwd) - 1);
            }
            windows[i].cwd[sizeof(windows[i].cwd) - 1] = '\0';
            win_switch(i);
            return;
        }
    }
    print("sh: maximum window limit reached (8 windows)\n");
    print_flush();
}

static void win_close(int target) {
    if (target < 0 || target >= MAX_WINDOWS || !windows[target].active) return;
    int count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].active) count++;
    if (count <= 1) {
        print("sh: cannot close the only active window\n");
        print_flush();
        return;
    }
    windows[target].active = false;
    if (target == active_win) {
        win_next();
    } else {
        win_show_status();
    }
}

static char cmd_history[32][MAX_LINE];
static int cmd_history_count = 0;

static void read_line(char *buf, int max_len) {
    int len = 0;
    int pos = 0;
    int history_idx = -1;

    if (!buf || max_len <= 0) return;
    buf[0] = '\0';

    while (1) {
        char ch = 0;
        int n = read(0, &ch, 1);
        if (n <= 0) continue;

        // Ctrl+C (SIGINT): Cancel current command line and start fresh
        if (ch == 3) {
            print("^C\n");
            print_flush();
            buf[0] = '\0';
            return;
        }

        // Ctrl+D (EOF): Exit if line is empty
        if (ch == 4) {
            if (len == 0) {
                strncpy(buf, "exit", max_len - 1);
                buf[max_len - 1] = '\0';
                print("exit\n");
                print_flush();
                return;
            }
            continue;
        }

        // Ctrl+A: Move to beginning of line (Home)
        if (ch == 1) {
            if (pos > 0) {
                char mc[16];
                snprintf(mc, sizeof(mc), "\033[%dD", pos);
                print(mc);
                print_flush();
                pos = 0;
            }
            continue;
        }

        // Ctrl+E: Move to end of line (End)
        if (ch == 5) {
            if (pos < len) {
                char mc[16];
                snprintf(mc, sizeof(mc), "\033[%dC", len - pos);
                print(mc);
                print_flush();
                pos = len;
            }
            continue;
        }

        // Ctrl+U: Erase entire line
        if (ch == 21) {
            if (pos > 0) {
                char mc[16];
                snprintf(mc, sizeof(mc), "\033[%dD", pos);
                print(mc);
            }
            print("\033[K");
            print_flush();
            len = 0;
            pos = 0;
            buf[0] = '\0';
            continue;
        }

        // Ctrl+K: Kill from cursor to end of line
        if (ch == 11) {
            print("\033[K");
            print_flush();
            len = pos;
            buf[len] = '\0';
            continue;
        }

        if (ch == 2) { // Ctrl+B (tmux hotkey prefix)
            print("\033[7m[TMUX]\033[0m");
            print_flush();
            char action = 0;
            while (read(0, &action, 1) <= 0);
            print("\b\b\b\b\b\b      \b\b\b\b\b\b");
            print_flush();
            if (action == 'c' || action == 'C') {
                print("\n");
                win_new(NULL);
                buf[0] = '\0';
                return;
            } else if (action == 'n' || action == 'N') {
                print("\n");
                win_next();
                buf[0] = '\0';
                return;
            } else if (action == 'p' || action == 'P') {
                print("\n");
                win_prev();
                buf[0] = '\0';
                return;
            } else if (action >= '0' && action <= '7') {
                print("\n");
                win_switch(action - '0');
                buf[0] = '\0';
                return;
            } else if (action == 'x' || action == 'X' || action == '&') {
                print("\n");
                win_close(active_win);
                buf[0] = '\0';
                return;
            } else if (action == 'w' || action == 'l') {
                print("\n");
                win_show_status();
            }
            continue;
        }

        // VT100 / ANSI Escape Sequences (Arrows, Home, End, Delete)
        if (ch == 27) {
            char seq[3];
            int n1 = read(0, &seq[0], 1);
            if (n1 <= 0) continue;
            if (seq[0] == '[') {
                int n2 = read(0, &seq[1], 1);
                if (n2 <= 0) continue;

                if (seq[1] == 'D') { // Left Arrow
                    if (pos > 0) {
                        pos--;
                        print("\033[D");
                        print_flush();
                    }
                } else if (seq[1] == 'C') { // Right Arrow
                    if (pos < len) {
                        pos++;
                        print("\033[C");
                        print_flush();
                    }
                } else if (seq[1] == 'H') { // Home
                    if (pos > 0) {
                        char mc[16];
                        snprintf(mc, sizeof(mc), "\033[%dD", pos);
                        print(mc);
                        print_flush();
                        pos = 0;
                    }
                } else if (seq[1] == 'F') { // End
                    if (pos < len) {
                        char mc[16];
                        snprintf(mc, sizeof(mc), "\033[%dC", len - pos);
                        print(mc);
                        print_flush();
                        pos = len;
                    }
                } else if (seq[1] == 'A') { // Up Arrow (History Prev)
                    if (cmd_history_count > 0) {
                        if (history_idx == -1) history_idx = cmd_history_count - 1;
                        else if (history_idx > 0) history_idx--;

                        if (pos > 0) {
                            char mc[16];
                            snprintf(mc, sizeof(mc), "\033[%dD", pos);
                            print(mc);
                        }
                        print("\033[K");
                        strncpy(buf, cmd_history[history_idx], max_len - 1);
                        buf[max_len - 1] = '\0';
                        len = strlen(buf);
                        pos = len;
                        print(buf);
                        print_flush();
                    }
                } else if (seq[1] == 'B') { // Down Arrow (History Next)
                    if (history_idx >= 0) {
                        history_idx++;
                        if (pos > 0) {
                            char mc[16];
                            snprintf(mc, sizeof(mc), "\033[%dD", pos);
                            print(mc);
                        }
                        print("\033[K");
                        if (history_idx >= cmd_history_count) {
                            history_idx = -1;
                            len = 0;
                            pos = 0;
                            buf[0] = '\0';
                        } else {
                            strncpy(buf, cmd_history[history_idx], max_len - 1);
                            buf[max_len - 1] = '\0';
                            len = strlen(buf);
                            pos = len;
                            print(buf);
                        }
                        print_flush();
                    }
                } else if (seq[1] >= '0' && seq[1] <= '9') { // CSI <n>~ (e.g. Delete \033[3~, Insert, PgUp, PgDn)
                    char tilde = 0;
                    if (read(0, &tilde, 1) > 0 && seq[1] == '3' && tilde == '~' && pos < len) {
                        memmove(buf + pos, buf + pos + 1, len - pos);
                        len--;
                        buf[len] = '\0';
                        print(buf + pos);
                        print(" \033[K");
                        char mc[16];
                        snprintf(mc, sizeof(mc), "\033[%dD", len - pos + 1);
                        print(mc);
                        print_flush();
                    }
                }
            }
            continue;
        }

        // Enter: Commit command line
        if (ch == '\r' || ch == '\n') {
            print("\n");
            print_flush();
            buf[len] = '\0';
            // Save to history
            if (len > 0) {
                if (cmd_history_count == 0 || strcmp(cmd_history[cmd_history_count - 1], buf) != 0) {
                    if (cmd_history_count < 32) {
                        strncpy(cmd_history[cmd_history_count], buf, sizeof(cmd_history[0]) - 1);
                        cmd_history[cmd_history_count][sizeof(cmd_history[0]) - 1] = '\0';
                        cmd_history_count++;
                    } else {
                        memmove(&cmd_history[0], &cmd_history[1], sizeof(cmd_history[0]) * 31);
                        strncpy(cmd_history[31], buf, sizeof(cmd_history[0]) - 1);
                        cmd_history[31][sizeof(cmd_history[0]) - 1] = '\0';
                    }
                }
            }
            break;
        }

        // Backspace: Delete character before cursor
        if (ch == '\b' || ch == 127) {
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
                print("\033[D");
                print(buf + pos);
                print(" \033[K");
                char mc[16];
                snprintf(mc, sizeof(mc), "\033[%dD", len - pos + 1);
                print(mc);
                print_flush();
            }
            continue;
        }

        // Regular printable character
        if (ch >= 32 && ch <= 126) {
            if (len < max_len - 1) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = ch;
                pos++;
                len++;
                buf[len] = '\0';

                char out[2] = {ch, '\0'};
                print(out);
                if (pos < len) {
                    print(buf + pos);
                    print("\033[K");
                    char mc[16];
                    snprintf(mc, sizeof(mc), "\033[%dD", len - pos);
                    print(mc);
                }
                print_flush();
            }
        }
    }
    buf[len] = '\0';
}

static int parse_args(char *line, char *argv[], char **redir_in, char **redir_out, int *append_out) {
    int argc = 0;
    char *p = line;
    char pending_op = '\0';
    *redir_in = NULL;
    *redir_out = NULL;
    *append_out = 0;

    while ((*p || pending_op) && argc < MAX_ARGS - 1) {
        if (!pending_op) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0') break;
        }

        char op = pending_op;
        if (op) {
            pending_op = '\0';
        } else if (*p == '>' || *p == '<') {
            op = *p++;
        }

        // Check for redirection
        if (op == '>') {
            if (*p == '>') {
                *append_out = 1;
                p++;
            } else {
                *append_out = 0;
            }
            while (*p == ' ' || *p == '\t') p++;
            char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '<' && *p != '>') p++;
            if (*p == '<' || *p == '>') {
                pending_op = *p;
                *p++ = '\0';
            } else if (*p) {
                *p++ = '\0';
            }
            if (*start == '\0') {
                print("sh: syntax error near unexpected token '>'\n");
                print_flush();
                return -1;
            }
            *redir_out = start;
            continue;
        } else if (op == '<') {
            while (*p == ' ' || *p == '\t') p++;
            char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '<' && *p != '>') p++;
            if (*p == '<' || *p == '>') {
                pending_op = *p;
                *p++ = '\0';
            } else if (*p) {
                *p++ = '\0';
            }
            if (*start == '\0') {
                print("sh: syntax error near unexpected token '<'\n");
                print_flush();
                return -1;
            }
            *redir_in = start;
            continue;
        }

        char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '<') p++;
        if (*p == '>' || *p == '<') {
            pending_op = *p;
            *p++ = '\0';
        } else if (*p) {
            *p++ = '\0';
        }
        argv[argc++] = start;
    }
    argv[argc] = NULL;
    return argc;
}

static void get_username(uint32_t uid, char *out, size_t out_len) {
    if (uid == 0) {
        strncpy(out, "root", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        char buf[2048];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (line && *line) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                char *f1 = strchr(line, ':');
                if (f1) {
                    *f1 = '\0';
                    char *f2 = strchr(f1 + 1, ':');
                    if (f2) {
                        uint32_t u = (uint32_t)atoi(f2 + 1);
                        if (u == uid) {
                            strncpy(out, line, out_len - 1);
                            out[out_len - 1] = '\0';
                            return;
                        }
                    }
                }
                line = nl ? nl + 1 : NULL;
            }
        }
    }
    snprintf(out, out_len, "user%u", uid);
}

static int run_command_line(char *line);
static int source_depth = 0;

static int execute_command_ex(int argc, char *argv[], char *redir_in, char *redir_out, int append_out, int *async_pid_out) {
    if (async_pid_out) *async_pid_out = -1;
    if (argc <= 0 || !argv[0]) return 0;

    // Builtin exit handles directly before setting up redirections
    if (strcmp(argv[0], "exit") == 0) {
        int code = (argc > 1) ? atoi(argv[1]) : 0;
        exit(code);
    }

    // Handle I/O redirection for both builtins and external binaries
    int orig_stdin = -1;
    int orig_stdout = -1;

    if (redir_in) {
        int ifd = open(redir_in, O_RDONLY);
        if (ifd < 0) {
            printf("sh: cannot open input file '%s'\n", redir_in);
            print_flush();
            return 1;
        }
        orig_stdin = dup(0);
        if (orig_stdin < 0) {
            close(ifd);
            print("sh: too many open files\n");
            print_flush();
            return 1;
        }
        dup2(ifd, 0);
        close(ifd);
    }

    if (redir_out) {
        int flags = O_WRONLY | O_CREAT | (append_out ? O_APPEND : O_TRUNC);
        int ofd = open(redir_out, flags);
        if (ofd < 0) {
            printf("sh: cannot open output file '%s'\n", redir_out);
            print_flush();
            if (orig_stdin >= 0) { dup2(orig_stdin, 0); close(orig_stdin); }
            return 1;
        }
        orig_stdout = dup(1);
        if (orig_stdout < 0) {
            close(ofd);
            if (orig_stdin >= 0) { dup2(orig_stdin, 0); close(orig_stdin); }
            print("sh: too many open files\n");
            print_flush();
            return 1;
        }
        print_flush();
        dup2(ofd, 1);
        close(ofd);
    }

    int ret = 0;
    bool handled = true;

    if (strcmp(argv[0], "source") == 0 || strcmp(argv[0], ".") == 0) {
        if (argc < 2) {
            print("sh: source: filename argument required\n");
            print_flush();
            ret = 1;
        } else if (source_depth >= 8) {
            print("sh: source: recursion depth limit exceeded\n");
            print_flush();
            ret = 1;
        } else {
            int sfd = open(argv[1], O_RDONLY);
            if (sfd < 0) {
                printf("sh: source: cannot open '%s'\n", argv[1]);
                print_flush();
                ret = 1;
            } else {
                char sbuf[4096];
                int n = read(sfd, sbuf, sizeof(sbuf) - 1);
                close(sfd);
                if (n > 0) {
                    sbuf[n] = '\0';
                    source_depth++;
                    char *sline = sbuf;
                    while (sline && *sline) {
                        char *nl = strchr(sline, '\n');
                        if (nl) *nl = '\0';
                        while (*sline == ' ' || *sline == '\t') sline++;
                        if (*sline && *sline != '#') {
                            ret = run_command_line(sline);
                        }
                        sline = nl ? nl + 1 : NULL;
                    }
                    source_depth--;
                }
            }
        }
    } else if (strcmp(argv[0], "cd") == 0) {
        const char *target = (argc > 1) ? argv[1] : "/";
        if (chdir(target) != 0) {
            printf("sh: cd: %s: No such directory\n", target);
            print_flush();
            ret = 1;
        } else {
            win_save_cwd();
        }
    } else if (strcmp(argv[0], "..") == 0) {
        if (chdir("..") != 0) {
            printf("sh: cd: ..: No such directory\n");
            print_flush();
            ret = 1;
        } else {
            win_save_cwd();
        }
    } else if (strcmp(argv[0], "pwd") == 0) {
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd)) == 0) {
            printf("%s\n", cwd);
        } else {
            print("/\n");
        }
        print_flush();
    } else if (strcmp(argv[0], "clear") == 0) {
        print("\033[2J\033[H");
        print_flush();
    } else if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            printf("%s%s", argv[i], (i == argc - 1) ? "" : " ");
        }
        print("\n");
        print_flush();
    } else if (strcmp(argv[0], "version") == 0) {
        print("KyroOS Beryllium 26.03.12 (Userspace Shell v2.0)\n");
        print_flush();
    } else if (strcmp(argv[0], "win") == 0 || strcmp(argv[0], "tab") == 0 || strcmp(argv[0], "tmux") == 0) {
        if (argc == 1 || strcmp(argv[1], "list") == 0) {
            print("Active Shell Windows (Tmux):\n");
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].active) {
                    printf("  [%d] %-10s %s (dir: %s)\n", i, windows[i].name,
                           (i == active_win) ? "* (active)" : "", windows[i].cwd);
                }
            }
            print("\nUsage:\n");
            print("  win new [name]    Create and switch to new window\n");
            print("  win next / prev   Switch to next or previous window\n");
            print("  win <0..7>        Switch to specific window index\n");
            print("  win close [0..7]  Close window\n");
            print("  win rename <name> Rename current window\n");
            print("Hotkeys: Ctrl+B then c (create), n (next), p (prev), 0-7 (switch), x (close)\n");
            print_flush();
        } else if (strcmp(argv[1], "new") == 0 || strcmp(argv[1], "new-window") == 0) {
            const char *name = (argc > 2) ? argv[2] : NULL;
            win_new(name);
        } else if (strcmp(argv[1], "next") == 0 || strcmp(argv[1], "n") == 0) {
            win_next();
        } else if (strcmp(argv[1], "prev") == 0 || strcmp(argv[1], "p") == 0) {
            win_prev();
        } else if (strcmp(argv[1], "close") == 0 || strcmp(argv[1], "kill") == 0) {
            int target = (argc > 2) ? atoi(argv[2]) : active_win;
            win_close(target);
        } else if (strcmp(argv[1], "rename") == 0 && argc > 2) {
            strncpy(windows[active_win].name, argv[2], sizeof(windows[active_win].name) - 1);
            windows[active_win].name[sizeof(windows[active_win].name) - 1] = '\0';
            win_show_status();
        } else if (argv[1][0] >= '0' && argv[1][0] <= '7') {
            win_switch(atoi(argv[1]));
        }
    } else if (strcmp(argv[0], "help") == 0) {
        print("KyroOS Shell Builtins:\n");
        print("  cd [dir]       Change current working directory\n");
        print("  pwd            Show current working directory\n");
        print("  echo [text...] Print arguments to stdout\n");
        print("  clear          Clear the console screen\n");
        print("  version        Display system version\n");
        print("  win / tab      Tmux-style multi-window manager (tabs, multiplexer)\n");
        print("  help           Display this help guide\n");
        print("  exit [code]    Exit the current shell\n");
        print("Hotkeys: Ctrl+B then c (new), n (next), p (prev), 0-7 (switch), x (close)\n");
        print("\nAvailable Userspace Utilities in /bin:\n");
        print("  ls, cat, edit, nano, mkdir, rm, cp, mv, touch, df, du, lsblk\n");
        print("  ps, free, sysmon, lspci, kyrofetch, 3d, ifconfig, ping, curl, wget\n");
        print("  gears, vkcube, useradd, userdel, chmod, chown\n");
        print("  insmod, rmmod, lsmod, installer\n");
        print_flush();
    } else {
        handled = false;
    }

    if (!handled) {
        // Check for trailing & background job execution
        bool background = false;
        if (argc > 0 && strcmp(argv[argc - 1], "&") == 0) {
            background = true;
            argv[argc - 1] = NULL;
            argc--;
        }

        if (argc > 0) {
            int pid = execve(argv[0], argv, NULL);
            if (pid < 0) {
                char bin_path[256];
                int n = snprintf(bin_path, sizeof(bin_path), "/bin/%s", argv[0]);
                if (n > 0 && n < (int)sizeof(bin_path)) {
                    pid = execve(bin_path, argv, NULL);
                }
            }

            if (pid < 0) {
                printf("sh: %s: command not found\n", argv[0]);
                print_flush();
                ret = 127;
            } else if (background) {
                printf("[job] process %d running in background\n", pid);
                print_flush();
                ret = 0;
            } else if (async_pid_out) {
                *async_pid_out = pid;
                ret = 0;
            } else {
                int status = 0;
                waitpid(pid, &status, 0);
                ret = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (status & 0x7F);
            }
        }
    }

    // Restore redirection
    if (orig_stdin >= 0) {
        dup2(orig_stdin, 0);
        close(orig_stdin);
    }
    if (orig_stdout >= 0) {
        print_flush();
        dup2(orig_stdout, 1);
        close(orig_stdout);
    }

    return ret;
}

static int run_command_line(char *line) {
    // Trim leading spaces
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return 0;

    // Check for pipeline |
    char *pipe_pos = strchr(p, '|');
    if (pipe_pos) {
        *pipe_pos = '\0';
        char *cmd1_str = p;
        char *cmd2_str = pipe_pos + 1;

        char *c1_argv[MAX_ARGS], *c2_argv[MAX_ARGS];
        char *r_in1 = NULL, *r_out1 = NULL, *r_in2 = NULL, *r_out2 = NULL;
        int a_out1 = 0, a_out2 = 0;

        int c1_argc = parse_args(cmd1_str, c1_argv, &r_in1, &r_out1, &a_out1);
        int c2_argc = parse_args(cmd2_str, c2_argv, &r_in2, &r_out2, &a_out2);

        if (c1_argc <= 0 || c2_argc <= 0) {
            if (c1_argc == 0 || c2_argc == 0) {
                print("sh: syntax error near '|'\n");
                print_flush();
            }
            return 1;
        }

        int fds[2];
        if (pipe(fds) == 0) {
            int orig_out = dup(1);
            if (orig_out < 0) {
                close(fds[0]);
                close(fds[1]);
                return 1;
            }
            print_flush();
            dup2(fds[1], 1);
            close(fds[1]);

            int c1_pid = -1;
            execute_command_ex(c1_argc, c1_argv, r_in1, NULL, 0, &c1_pid);

            print_flush();
            dup2(orig_out, 1);
            close(orig_out);

            int orig_in = dup(0);
            if (orig_in < 0) {
                close(fds[0]);
                if (c1_pid > 0) waitpid(c1_pid, NULL, 0);
                return 1;
            }
            dup2(fds[0], 0);
            close(fds[0]);

            int ret = execute_command_ex(c2_argc, c2_argv, NULL, r_out2, a_out2, NULL);

            dup2(orig_in, 0);
            close(orig_in);

            if (c1_pid > 0) {
                waitpid(c1_pid, NULL, 0);
            }
            return ret;
        }
        return -1;
    }

    char *cmd_argv[MAX_ARGS];
    char *redir_in = NULL;
    char *redir_out = NULL;
    int append_out = 0;
    int parsed_argc = parse_args(p, cmd_argv, &redir_in, &redir_out, &append_out);
    if (parsed_argc < 0) return 1;

    return execute_command_ex(parsed_argc, cmd_argv, redir_in, redir_out, append_out, NULL);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: sh [-c \"command\"] [--help]\n");
        print("KyroOS POSIX-compatible shell with built-in Tmux-like window multiplexer.\n");
        print("Type 'help' inside the shell for builtins or 'win' for window management.\n");
        print_flush();
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        char cmd_copy[MAX_LINE];
        strncpy(cmd_copy, argv[2], sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';
        int ret = run_command_line(cmd_copy);
        print_flush();
        return ret;
    }

    win_init();

    gethostname(current_hostname, sizeof(current_hostname));
    current_hostname[sizeof(current_hostname) - 1] = '\0';
    if (current_hostname[0] == '\0') {
        strncpy(current_hostname, "kyroos", sizeof(current_hostname) - 1);
        current_hostname[sizeof(current_hostname) - 1] = '\0';
    }

    win_show_status();

    char line[MAX_LINE];

    while (1) {
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd)) != 0 || cwd[0] == '\0') {
            strncpy(cwd, "/", sizeof(cwd) - 1);
            cwd[sizeof(cwd) - 1] = '\0';
        }

        uint32_t uid = getuid();
        char username[64];
        get_username(uid, username, sizeof(username));
        char prompt_char = (uid == 0) ? '#' : '$';

        // Colored prompt: [root@kyroos /]#
        printf("\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m%c ", username, current_hostname, cwd, prompt_char);
        print_flush();

        read_line(line, sizeof(line));

        run_command_line(line);
    }

    return 0;
}
