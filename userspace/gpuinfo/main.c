#include <kyroolib.h>
#include <procinfo.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: gpuinfo [--help]\n");
        print("Display hardware GPU vendor, device model, VRAM aperture, and display resolution.\n");
        print_flush();
        return 0;
    }

    gpu_info_t info;
    memset(&info, 0, sizeof(info));

    if (get_gpu_info(&info) != 0) {
        print("gpuinfo: failed to query GPU hardware info from kernel\n");
        print_flush();
        return 1;
    }

    print("\033[1;36m============================================================\033[0m\n");
    print("\033[1;32m                 KyroOS GPU & Video Information             \033[0m\n");
    print("\033[1;36m============================================================\033[0m\n");

    printf("  \033[1;33mGPU Vendor\033[0m    : %s\n", info.vendor_name[0] ? info.vendor_name : "Unknown Vendor");
    printf("  \033[1;33mGPU Model\033[0m     : %s\n", info.model_name[0] ? info.model_name : "Unknown Display Device");
    printf("  \033[1;33mPCI Location\033[0m  : %02x:%02x.%x (Vendor: 0x%04x, Device: 0x%04x)\n",
           info.bus, info.slot, info.func, info.vendor_id, info.device_id);

    uint64_t vram_mb = info.vram_size_bytes / (1024 * 1024);
    if (vram_mb >= 1024) {
        printf("  \033[1;33mDedicated VRAM\033[0m: %llu MB (~%llu GB)\n", vram_mb, vram_mb / 1024);
    } else {
        printf("  \033[1;33mDedicated VRAM\033[0m: %llu MB\n", vram_mb);
    }

    if (info.bar0_base) {
        printf("  \033[1;33mPCI BAR0 Base\033[0m : 0x%llx\n", info.bar0_base);
    }
    if (info.bar1_base) {
        printf("  \033[1;33mPCI BAR1 Base\033[0m : 0x%llx\n", info.bar1_base);
    }

    printf("  \033[1;33mFramebuffer\033[0m   : %u x %u @ %u bpp (RGBA%s)\n",
           info.fb_width, info.fb_height, info.fb_bpp, (info.fb_bpp == 32) ? "8888" : "");
    print("\033[1;36m============================================================\033[0m\n");
    print_flush();

    return 0;
}
