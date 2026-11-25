#include <kyroolib.h>
#include <procinfo.h>
#include <stdint.h>

static void get_cpu_model(char *buf) {
    uint32_t brand[12];
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax >= 0x80000004) {
        __asm__ volatile("cpuid" : "=a"(brand[0]), "=b"(brand[1]), "=c"(brand[2]), "=d"(brand[3]) : "a"(0x80000002));
        __asm__ volatile("cpuid" : "=a"(brand[4]), "=b"(brand[5]), "=c"(brand[6]), "=d"(brand[7]) : "a"(0x80000003));
        __asm__ volatile("cpuid" : "=a"(brand[8]), "=b"(brand[9]), "=c"(brand[10]), "=d"(brand[11]) : "a"(0x80000004));
        memcpy(buf, brand, 48);
        buf[48] = '\0';
        // Trim leading spaces
        char *p = buf;
        while (*p == ' ') p++;
        if (p != buf) memmove(buf, p, strlen(p) + 1);
    } else {
        strcpy(buf, "x86_64 Compatible Processor");
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char host[64] = "kyroos";
    gethostname(host, sizeof(host));

    char cpu_str[64];
    get_cpu_model(cpu_str);

    struct sysinfo_k si;
    memset(&si, 0, sizeof(si));
    sysinfo_k(&si);

    struct ifinfo ifs[4];
    int nifs = net_ifconf(ifs, 4);
    char ip_str[32] = "127.0.0.1";
    for (int i = 0; i < nifs; i++) {
        if (ifs[i].ip != 0 && ifs[i].ip != 0x7F000001) {
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u (%s)",
                     (ifs[i].ip >> 24) & 0xFF, (ifs[i].ip >> 16) & 0xFF,
                     (ifs[i].ip >> 8) & 0xFF, ifs[i].ip & 0xFF,
                     ifs[i].name);
            break;
        }
    }

    gpu_info_t gpu;
    memset(&gpu, 0, sizeof(gpu));
    get_gpu_info(&gpu);

    uint32_t upt_s = si.uptime_ticks / 100;
    uint32_t upt_m = upt_s / 60;
    uint32_t upt_h = upt_m / 60;
    upt_m %= 60;
    upt_s %= 60;

    print("\n");
    printf("  \033[1;36m      /\\      \033[0m   \033[1;32mroot\033[0m@\033[1;32m%s\033[0m\n", host);
    print("  \033[1;36m     /  \\     \033[0m   ----------------------------------------\n");
    printf("  \033[1;36m    / /\\ \\    \033[0m   \033[1;33mOS:\033[0m       KyroOS 26.11.13 Carbon (x86_64)\n");
    printf("  \033[1;36m   / /  \\ \\   \033[0m   \033[1;33mKernel:\033[0m   KyroKernel 26.11.13-SMP\n");
    printf("  \033[1;36m  / / /\\ \\ \\  \033[0m   \033[1;33mUptime:\033[0m   %uh %um %us\n", upt_h, upt_m, upt_s);
    printf("  \033[1;36m / / /  \\ \\ \\ \033[0m   \033[1;33mShell:\033[0m    /bin/sh (KyroShell v2.0)\n");
    printf("  \033[1;36m/_/ /    \\ \\_\\\033[0m   \033[1;33mCPU:\033[0m      %s\n", cpu_str);
    printf("  \033[1;36m  \\/      \\/  \033[0m   \033[1;33mMemory:\033[0m   %u MB / %u MB\n",
           si.mem_used_kb / 1024, si.mem_total_kb / 1024);
    printf("                   \033[1;33mDisplay:\033[0m  %u x %u @ %u bpp\n",
           gpu.fb_width ? gpu.fb_width : 1024,
           gpu.fb_height ? gpu.fb_height : 768,
           gpu.fb_bpp ? gpu.fb_bpp : 32);
    printf("                   \033[1;33mNetwork:\033[0m  %s\n", ip_str);
    print("\n");
    print("                   \033[40m   \033[41m   \033[42m   \033[43m   \033[44m   \033[45m   \033[46m   \033[47m   \033[0m\n");
    print("                   \033[100m   \033[101m   \033[102m   \033[103m   \033[104m   \033[105m   \033[106m   \033[107m   \033[0m\n\n");
    print_flush();
    return 0;
}
