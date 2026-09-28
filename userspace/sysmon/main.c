#include <kyroolib.h>
#include <stdint.h>
#include <string.h>

/*
 * sysmon - full-screen process/system monitor (htop/btop analog).
 * Data sources: proc_list / sysinfo_k / net_ifconf syscalls backed by
 * real kernel structures (scheduler queue, PMM, driver counters).
 * No /proc dependency.
 *
 * Keys:
 *   q / Q / Esc   quit
 *   1..4          sort by pid / cpu / name / state
 *   +/-           refresh interval +/- 500ms
 *   k <pid>       kill process (owner or root only; kernel enforces)
 *   h             help
 */

#define MAX_PROCS 64
#define MAX_IFS   4

typedef struct {
    struct procinfo procs[MAX_PROCS];
    int nprocs;
    struct sysinfo_k si;
    struct ifinfo ifs[MAX_IFS];
    int nifs;
    int sort_key;       /* 0=pid 1=cpu 2=name 3=state */
    int interval_ms;
    uint64_t cpu_total; /* sum of cpu_ticks across processes */
    int quit;
} sysmon_state_t;

static sysmon_state_t g;

static void clear_screen(void) {
    print("\033[2J\033[H");
}

static void move_to(int row, int col) {
    char b[16];
    sprintf(b, "\033[%d;%dH", row, col);
    print(b);
}

static void print_pad(const char *s, int width) {
    int len = strlen(s);
    print(s);
    for (int i = len; i < width; i++) print(" ");
}

static void fmt_u64(char *out, uint64_t v) {
    char tmp[24];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

static const char *state_name(uint8_t s) {
    switch (s) {
        case PROC_STATE_RUNNING: return "RUN ";
        case PROC_STATE_READY:   return "READY";
        case PROC_STATE_BLOCKED: return "BLOCK";
        case PROC_STATE_DEAD:    return "DEAD";
        default:                 return "?????";
    }
}

static void poll_data(void) {
    g.nprocs = proc_list(g.procs, MAX_PROCS);
    if (g.nprocs < 0) g.nprocs = 0;
    sysinfo_k(&g.si);
    g.nifs = net_ifconf(g.ifs, MAX_IFS);
    if (g.nifs < 0) g.nifs = 0;
    g.cpu_total = 0;
    for (int i = 0; i < g.nprocs; i++) g.cpu_total += g.procs[i].cpu_ticks;
    if (g.cpu_total == 0) g.cpu_total = 1;
}

static void sort_procs(void) {
    for (int i = 1; i < g.nprocs; i++) {
        struct procinfo key = g.procs[i];
        int j = i - 1;
        while (j >= 0) {
            int less = 0;
            switch (g.sort_key) {
                case 0: less = g.procs[j].pid > key.pid; break;
                case 1: less = g.procs[j].cpu_ticks > key.cpu_ticks; break;
                case 2: less = strcmp(g.procs[j].name, key.name) > 0; break;
                case 3: less = g.procs[j].state > key.state; break;
            }
            if (!less) break;
            g.procs[j + 1] = g.procs[j];
            j--;
        }
        g.procs[j + 1] = key;
    }
}

static void draw(void) {
    char b[160];
    clear_screen();

    uint32_t upt_s = g.si.uptime_ticks / 100;
    print("sysmon - KyroOS  up ");
    fmt_u64(b, upt_s / 60); print(b);
    print("m ");
    fmt_u64(b, upt_s % 60); print(b);
    print("s   ");
    uint32_t mem_total = g.si.mem_total_kb ? g.si.mem_total_kb : 1;
    uint32_t pct = g.si.mem_used_kb * 100 / mem_total;
    sprintf(b, "Mem %u/%u MB (%u%%)   procs: %d",
            g.si.mem_used_kb / 1024, mem_total / 1024, pct, g.nprocs);
    print(b);
    print("   [sort:");
    print(g.sort_key == 0 ? "pid" : g.sort_key == 1 ? "cpu" :
          g.sort_key == 2 ? "name" : "state");
    sprintf(b, " refresh:%dms]", g.interval_ms);
    print(b);
    print("\n");

    if (g.nifs > 0) {
        print("net: rx ");
        fmt_u64(b, g.si.rx_bytes); print(b);
        print(" B / tx ");
        fmt_u64(b, g.si.tx_bytes); print(b);
        print(" B   pkts rx ");
        fmt_u64(b, g.si.rx_packets); print(b);
        print(" tx ");
        fmt_u64(b, g.si.tx_packets); print(b);
        print("\n");
    }

    print("\n");
    print_pad("  PID", 7);
    print_pad("USER", 6);
    print_pad("S", 6);
    print_pad("CPU%", 6);
    print_pad("TICKS", 8);
    print("  NAME\n");
    print("  ----------------------------------------------------------------------\n");

    for (int i = 0; i < g.nprocs && i < 20; i++) {
        struct procinfo *p = &g.procs[i];
        print("  ");
        fmt_u64(b, p->pid);        print_pad(b, 5);
        fmt_u64(b, p->uid);        print_pad(b, 6);
        print_pad(state_name(p->state), 6);
        uint32_t cpct = (uint32_t)(p->cpu_ticks * 100 / g.cpu_total);
        fmt_u64(b, cpct);          print_pad(b, 6);
        fmt_u64(b, p->cpu_ticks);  print_pad(b, 8);
        print("  ");
        print(p->name);
        print("\n");
    }

    move_to(26, 1);
    print("q quit | 1 pid 2 cpu 3 name 4 state | +/- interval | k <pid> kill | h help");
}

static void show_help(void) {
    clear_screen();
    move_to(1, 1);
    print("sysmon help\n\n");
    print("  q / Esc    quit\n");
    print("  1..4       sort: pid, cpu, name, state\n");
    print("  + / -      refresh interval +/-500ms\n");
    print("  k <pid>    terminate process (owner or root; kernel enforces)\n");
    print("  h          this help\n\n");
    print("Counters come from kernel structures: scheduler (procs, ticks),\n");
    print("PMM (memory), e1000 driver (net counters). Press any key to return.");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    memset(&g, 0, sizeof(g));
    g.sort_key = 1; /* cpu */
    g.interval_ms = 1000;

    while (!g.quit) {
        poll_data();
        sort_procs();
        draw();

        uint32_t deadline = uptime_ms() + (uint32_t)g.interval_ms;
        while (uptime_ms() < deadline && !g.quit) {
            event_t ev;
            if (input_poll_event(&ev) && ev.type == EVENT_KEY_DOWN) {
                char c = (char)ev.data1;
                switch (c) {
                    case 'q': case 'Q': case 27:
                        g.quit = 1;
                        break;
                    case '1': g.sort_key = 0; break;
                    case '2': g.sort_key = 1; break;
                    case '3': g.sort_key = 2; break;
                    case '4': g.sort_key = 3; break;
                    case '+': if (g.interval_ms < 10000) g.interval_ms += 500; break;
                    case '-': if (g.interval_ms > 500) g.interval_ms -= 500; break;
                    case 'h':
                        show_help();
                        {
                            event_t e2;
                            /* block for next key to return from help */
                            for (;;) {
                                if (input_poll_event(&e2) && e2.type == EVENT_KEY_DOWN) break;
                            }
                        }
                        break;
                    case 'k': {
                        move_to(27, 1);
                        print("kill pid> ");
                        char cmdbuf[12];
                        cmdbuf[0] = '\0';
                        int got = 0;
                        while (!got) {
                            event_t e3;
                            if (input_poll_event(&e3) && e3.type == EVENT_KEY_DOWN) {
                                char kc = (char)e3.data1;
                                if (kc == '\n') { got = 1; }
                                else if (kc == 27) { got = -1; }
                                else if (kc >= '0' && kc <= '9' && strlen(cmdbuf) < sizeof(cmdbuf) - 1) {
                                    int l = strlen(cmdbuf);
                                    cmdbuf[l] = kc;
                                    cmdbuf[l + 1] = '\0';
                                    print(&kc);
                                }
                            }
                        }
                        if (got == 1) {
                            uint32_t pid = 0;
                            for (const char *s = cmdbuf; *s; s++)
                                if (*s >= '0' && *s <= '9') pid = pid * 10 + (uint32_t)(*s - '0');
                            int r = kill(pid, 9);
                            if (r != 0)
                                print("\nkill failed (EPERM: not owner/root)\n");
                            else
                                print("\nkilled\n");
                            sleep_ms(500);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    clear_screen();
    print("sysmon: exited cleanly\n");
    return 0;
}
