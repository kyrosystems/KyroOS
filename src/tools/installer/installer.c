#include <kyroolib.h>
#include <installer.h>
#include <kpm.h> // For kpm_install_file
#include <fs_disk.h> // For fs_format

// --- Placeholder Functions for Installer Steps ---

void installer_disk_detection() {
    print("Installer: Detecting disks...\n");
    
    // For now, assume a single primary master IDE drive at /dev/sda
    const char *disk_path = "/dev/sda";
    int disk_fd = open(disk_path, O_RDONLY);
    if (disk_fd < 0) {
        print("INSTALLER: Error: Could not open disk device "); print(disk_path); print("\n");
        return;
    }

    ide_disk_info_t disk_info;
    if (ioctl(disk_fd, IDE_IOCTL_GET_DISK_INFO, &disk_info) != 0) {
        print("INSTALLER: Error: Could not get disk info for "); print(disk_path); print("\n");
        close(disk_fd);
        return;
    }

    char info_buf[128];
    ksprintf(info_buf, "INSTALLER: Found disk %s: Total sectors: %llu, Bytes per sector: %u\n", 
             disk_path, disk_info.total_sectors, disk_info.bytes_per_sector);
    print(info_buf);

    // Read MBR (first sector)
    mbr_t mbr;
    if (read(disk_fd, &mbr, sizeof(mbr_t)) != sizeof(mbr_t)) { // Read sector 0
        print("INSTALLER: Error: Could not read MBR from "); print(disk_path); print("\n");
        close(disk_fd);
        return;
    }

    if (mbr.boot_signature != __builtin_bswap16(0xAA55)) { // MBR signature is little-endian
        print("INSTALLER: Warning: Invalid MBR signature (0xAA55 not found).\n");
        // Proceeding anyway, but user should be aware.
    }

    print("INSTALLER: Detected Partitions:\n");
    for (int i = 0; i < 4; i++) {
        mbr_partition_entry_t *part = &mbr.partitions[i];
        if (part->type_code == 0x00) continue; // Empty partition

        ksprintf(info_buf, "  [%d] Type: 0x%02x, Start LBA: %u, Size: %u sectors\n",
                 i, part->type_code, part->starting_lba, part->size_in_sectors);
        print(info_buf);
    }
    close(disk_fd);
}

static uint32_t selected_partition_lba = 0;
static uint32_t selected_partition_size = 0;

void installer_partition_selection() {
    print("Installer: Selecting partitions...\n");
    
    // Re-read MBR to get partition info (could be passed from disk_detection too)
    const char *disk_path = "/dev/sda";
    int disk_fd = open(disk_path, O_RDONLY);
    if (disk_fd < 0) {
        print("INSTALLER: Error: Could not open disk device "); print(disk_path); print("\n");
        return;
    }
    mbr_t mbr;
    if (read(disk_fd, &mbr, sizeof(mbr_t)) != sizeof(mbr_t)) {
        print("INSTALLER: Error: Could not read MBR from "); print(disk_path); print("\n");
        close(disk_fd);
        return;
    }
    close(disk_fd);

    print("INSTALLER: Please select a partition for installation:\n");
    for (int i = 0; i < 4; i++) {
        mbr_partition_entry_t *part = &mbr.partitions[i];
        if (part->type_code == 0x00) continue;

        char info_buf[128];
        ksprintf(info_buf, "  [%d] Type: 0x%02x, Start LBA: %u, Size: %u sectors\n",
                 i + 1, part->type_code, part->starting_lba, part->size_in_sectors); // Display 1-indexed
        print(info_buf);
    }

    // For now, hardcode selection of the first valid primary partition
    for (int i = 0; i < 4; i++) {
        mbr_partition_entry_t *part = &mbr.partitions[i];
        if (part->type_code != 0x00 && part->size_in_sectors > 0) {
            selected_partition_lba = part->starting_lba;
            selected_partition_size = part->size_in_sectors;
            char info_buf[128];
            ksprintf(info_buf, "INSTALLER: Auto-selected partition %d (LBA: %u, Size: %u sectors)\n",
                     i + 1, selected_partition_lba, selected_partition_size);
            print(info_buf);
            return;
        }
    }

    print("INSTALLER: No suitable partition found for installation. Aborting.\n");
    selected_partition_lba = 0; // Mark as no selection
}

void installer_filesystem_formatting() {
    print("Installer: Formatting filesystem...\n");

    if (selected_partition_lba == 0) {
        print("INSTALLER: No partition selected for formatting. Aborting.\n");
        return;
    }

    const char *disk_path = "/dev/sda";
    int disk_fd = open(disk_path, O_RDWR); // Open with write access
    if (disk_fd < 0) {
        print("INSTALLER: Error: Could not open disk device for formatting "); print(disk_path); print("\n");
        return;
    }
    
    char info_buf[128];
    ksprintf(info_buf, "INSTALLER: Formatting partition at LBA %u, size %u sectors...\n", 
             selected_partition_lba, selected_partition_size);
    print(info_buf);

    // This fs_format uses the disk_fd directly
    ide_ioctl_format_t fmt_req = {
        .partition_lba = selected_partition_lba,
        .partition_size = selected_partition_size
    };
    if (ioctl(disk_fd, IDE_IOCTL_FORMAT, &fmt_req) != 0) {
        print("INSTALLER: Error: Filesystem formatting failed.\n");
        close(disk_fd);
        return;
    }
    close(disk_fd); // Close after formatting

    print("INSTALLER: Filesystem formatted successfully. Attempting to mount...\n");

    // Mount the formatted partition to /mnt
    // The device node path should reflect the specific partition, e.g. /dev/sda1
    // For now, hardcode it as /dev/sda1 in the device_node_path
    char partition_device_node_path[32];
    ksprintf(partition_device_node_path, "%s%u", disk_path, 1); // e.g., /dev/sda1 (assuming first partition)

    if (mount("/mnt", partition_device_node_path, "fs_disk") != 0) {
        print("INSTALLER: Error: Failed to mount filesystem to /mnt.\n");
        return;
    }
    print("INSTALLER: Filesystem mounted to /mnt successfully.\n");
}

// Helper function to copy a file from source_vfs to dest_vfs
static int copy_file_to_mounted_fs(const char *src_path, const char *dest_path_on_mounted_fs) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        print("INSTALLER: Error: Could not open source file for copy: "); print(src_path); print("\n");
        return -1;
    }
    
    // Get file size
    struct stat st;
    if (stat(src_path, &st) != 0) {
        print("INSTALLER: Error: Could not stat source file: "); print(src_path); print("\n");
        close(src_fd);
        return -1;
    }

    uint8_t *buffer = (uint8_t*)malloc(st.st_size);
    if (!buffer) {
        print("INSTALLER: Error: Failed to allocate buffer for file copy.\n");
        close(src_fd);
        return -1;
    }
    
    if (read(src_fd, buffer, st.st_size) != (int)st.st_size) {
        print("INSTALLER: Error: Failed to read source file: "); print(src_path); print("\n");
        free(buffer);
        close(src_fd);
        return -1;
    }
    close(src_fd);

    // Create and write to destination on mounted FS
    int dest_fd = open(dest_path_on_mounted_fs, O_CREAT | O_TRUNC | O_WRONLY);
    if (dest_fd < 0) {
        print("INSTALLER: Error: Could not create destination file: "); print(dest_path_on_mounted_fs); print("\n");
        free(buffer);
        return -1;
    }

    if (write(dest_fd, buffer, st.st_size) != (int)st.st_size) {
        print("INSTALLER: Error: Failed to write to destination file: "); print(dest_path_on_mounted_fs); print("\n");
        free(buffer);
        close(dest_fd);
        return -1;
    }
    close(dest_fd);
    free(buffer);

    print("INSTALLER: Copied "); print(src_path); print(" to "); print(dest_path_on_mounted_fs); print("\n");
    return 0;
}

void installer_base_system_installation() {
    print("Installer: Installing base system...\n");
    
    // Create necessary directories on /mnt
    print("INSTALLER: Creating directories on /mnt...\n");
    mkdir("/mnt/bin");
    mkdir("/mnt/etc");
    mkdir("/mnt/var");
    mkdir("/mnt/var/lib");
    mkdir("/mnt/var/lib/kpm");
    mkdir("/mnt/boot"); // For kernel

    // Copy essential executables
    print("INSTALLER: Copying essential executables...\n");
    if (copy_file_to_mounted_fs("/bin/shell", "/mnt/bin/shell") != 0) return;
    if (copy_file_to_mounted_fs("/bin/kpm", "/mnt/bin/kpm") != 0) return;
    if (copy_file_to_mounted_fs("/bin/installer", "/mnt/bin/installer") != 0) return; // Copy installer itself

    // Coreutils (assume they are also in /bin for now)
    if (copy_file_to_mounted_fs("/bin/ls", "/mnt/bin/ls") != 0) return;
    if (copy_file_to_mounted_fs("/bin/cat", "/mnt/bin/cat") != 0) return;
    if (copy_file_to_mounted_fs("/bin/mkdir", "/mnt/bin/mkdir") != 0) return;
    if (copy_file_to_mounted_fs("/bin/rm", "/mnt/bin/rm") != 0) return;
    if (copy_file_to_mounted_fs("/bin/rmdir", "/mnt/bin/rmdir") != 0) return;
    if (copy_file_to_mounted_fs("/bin/cp", "/mnt/bin/cp") != 0) return;
    if (copy_file_to_mounted_fs("/bin/mv", "/mnt/bin/mv") != 0) return;
    if (copy_file_to_mounted_fs("/bin/pwd", "/mnt/bin/pwd") != 0) return;
    if (copy_file_to_mounted_fs("/bin/cd", "/mnt/bin/cd") != 0) return;

    // Simulate copying kernel (actual kernel image is passed as limine module)
    // For a real installer, we'd need a syscall to get the kernel's module address/size
    // and write it to /mnt/boot/kernel.elf
    // For now, assume a dummy kernel.elf file exists in root for copy
    print("INSTALLER: Simulating kernel image copy...\n");
    // if (copy_file_to_mounted_fs("/boot/kernel.elf", "/mnt/boot/kernel.elf") != 0) return;
    print("  (Kernel copy simulated: ensure kernel module is available during installation.)\n");

    print("Installer: Base system installation complete.\n");
}

void installer_install_initial_packages() {
    print("Installer: Installing initial packages...\n");
    print("  (Note: kpm's network fetch is currently disabled due to missing TLS support.)\n");
    print("  This would involve running 'kpm install <local_package.kpkg>' for base packages.\n");
    print("  e.g., 'kpm install /packages/coreutils.kpkg' (simulated)\n");
    print("Installer: Initial packages installation complete (simulated).\n");
}

void installer_install_bootloader() {
    print("Installer: Installing bootloader...\n");
    print("  (This involves writing Limine bootloader stages to the disk and creating limine.cfg)\n");
    print("  Simulating Limine bootloader installation to MBR/ESP and /mnt/boot/limine.cfg.\n");

    // Simulate creating a limine.cfg
    const char *limine_cfg_content = 
        "TIMEOUT=3\n"
        "DEFAULT_ENTRY=1\n"
        "\n"
        ":KyroOS\n"
        "PROTOCOL=limine\n"
        "KASLR=no\n"
        "KERNEL_PATH=boot:///boot/kernel.elf\n"; // Path to kernel on the mounted FS

    // Write limine.cfg to /mnt/boot/limine.cfg
    int cfg_fd = open("/mnt/boot/limine.cfg", O_CREAT | O_TRUNC | O_WRONLY);
    if (cfg_fd < 0) {
        print("INSTALLER: Error: Could not create /mnt/boot/limine.cfg\n");
        return;
    }
    write(cfg_fd, (uint8_t*)limine_cfg_content, strlen(limine_cfg_content));
    close(cfg_fd);
    print("INSTALLER: /mnt/boot/limine.cfg created.\n");

    // Simulate writing bootloader stage1/stage2 to MBR/boot sector
    print("INSTALLER: Simulating writing Limine stage1/stage2 to disk (requires raw disk access).\n");
    // This would ideally involve:
    // 1. Opening the raw disk device (/dev/sda).
    // 2. Using IOCTLs or direct write syscalls to write Limine's bootloader.
    // 3. This is a complex step and will be simulated for now.

    print("Installer: Bootloader installation complete (simulated).\n");
}

void installer_create_initial_user() {
    print("Installer: Creating initial user...\n");
    print("  (This involves creating an entry in /mnt/etc/passwd and a home directory)\n");

    // Create a simple /mnt/etc/passwd file
    const char *passwd_content = "kyro:x:1000:1000:Kyro User,,,:/home/kyro:/bin/shell\n";
    int passwd_fd = open("/mnt/etc/passwd", O_CREAT | O_TRUNC | O_WRONLY);
    if (passwd_fd < 0) {
        print("INSTALLER: Error: Could not create /mnt/etc/passwd\n");
        return;
    }
    write(passwd_fd, (uint8_t*)passwd_content, strlen(passwd_content));
    close(passwd_fd);
    print("INSTALLER: /mnt/etc/passwd created with user 'kyro'.\n");

    // Simulate creating a home directory
    mkdir("/mnt/home"); // Create /mnt/home first
    mkdir("/mnt/home/kyro"); // Then create the user's home directory
    print("INSTALLER: /mnt/home/kyro created.\n");

    print("Installer: Initial user creation complete.\n");
}

void installer_basic_configuration() {
    print("Installer: Performing basic configuration (timezone, keyboard)...\n");
    print("  (This involves writing configuration files to /mnt/etc/)\n");
    print("  Simulating 'UTC' timezone and 'US' keyboard layout configuration.\n");
    // In a real scenario, this would involve creating /mnt/etc/timezone, /mnt/etc/default/keyboard etc.
    print("Installer: Basic configuration complete (simulated).\n");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv; // Suppress unused parameter warnings
    user_fb_info_t fb_info;
    if (gfx_get_fb_info(&fb_info) != 0) {
        print("Installer: Failed to get framebuffer info.\n");
        return 1;
    }
    
    // Simple clear screen (mimic shell's clear)
    // Direct framebuffer access is not available from userspace.
    // Need a syscall for clearing screen or rely on shell's clear.

    print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");

    print("=========================================\n");
    print("      KyroOS Graphical Installer\n");
    print("=========================================\n\n");
    print("1. Start Installation\n");
    print("2. Exit Installer\n\n");
    print("Enter your choice: ");

    event_t ev;
    while (1) {
        if (input_poll_event(&ev)) {
            if (ev.type == EVENT_KEY_DOWN) {
                char c = (char)ev.data1;
                if (c == '1') {
                    print("1\nStarting installation...\n");
                    installer_disk_detection();
                    installer_partition_selection();
                    installer_filesystem_formatting();
                    installer_base_system_installation();
                    installer_install_initial_packages();
                    installer_install_bootloader();
                    installer_create_initial_user();
                    installer_basic_configuration();
                    print("--- Installation process finished (simulated) ---\n");
                    print("Press any key to exit.\n");
                    while (!input_poll_event(&ev)); // Wait for key press
                    break;
                } else if (c == '2') {
                    print("2\nExiting installer.\n");
                    break;
                }
            }
        }
    }
    return 0;
}
