#include "shell.h"
#include "elf.h"
#include "epstein.h"
#include "event.h"
#include "fb.h"
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "port_io.h"
#include "scheduler.h"
#include "thread.h"
#include "version.h"
#include "vfs.h"
#include "panic.h" // Include the panic header
#include <stddef.h>
#include <stdint.h>

#define BUFFER_SIZE 256
#define HISTORY_SIZE 10

static char line_buffer[BUFFER_SIZE];
static int buffer_index = 0;

static char history[HISTORY_SIZE][BUFFER_SIZE];
static int history_count = 0;
static int history_index = -1;
static int current_history_view = -1;

static char cwd[256] = "/";

static bool debug_mode_enabled = false;

static void get_cpu_brand(char *buf) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ __volatile__("cpuid" : "=a"(eax) : "a"(0x80000000));
  if (eax < 0x80000004) {
    strncpy(buf, "Generic x86_64 CPU", 48);
    return;
  }
  uint32_t *ptr = (uint32_t *)buf;
  for (uint32_t i = 0; i < 3; i++) {
    __asm__ __volatile__("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(0x80000002 + i));
    ptr[i * 4 + 0] = eax;
    ptr[i * 4 + 1] = ebx;
    ptr[i * 4 + 2] = ecx;
    ptr[i * 4 + 3] = edx;
  }
  buf[48] = '\0';
}

static void shell_kyrofetch() {
  uint64_t total_bytes = pmm_get_total_memory();
  uint64_t used_bytes = pmm_get_used_memory();
  uint32_t total_mb = (uint32_t)(total_bytes / 1024 / 1024);
  uint32_t used_mb = (uint32_t)(used_bytes / 1024 / 1024);
  uint32_t mem_percent =
      (total_bytes > 0) ? (uint32_t)((used_bytes * 100ULL) / total_bytes) : 0;

  char buf[128];
  char cpu_brand[64];
  get_cpu_brand(cpu_brand);
  const fb_info_t *fb = fb_get_info();
  uint64_t ticks = timer_get_ticks();
  uint32_t total_sec = (uint32_t)(ticks / 100);
  uint32_t hrs = total_sec / 3600;
  uint32_t mins = (total_sec % 3600) / 60;
  uint32_t secs = total_sec % 60;

  klog_print_str("\n");
  klog_print_str("   _  ___              ____  ____\n");
  klog_print_str("  | |/ /_ _________   / __ \\/ __/\n");
  klog_print_str("  | ' / // / __/ _ \\ / /_/ /\\ \\  \n");
  klog_print_str("  |_|\\_\\_, /_/  \\___/ \\____/___/ \n");
  klog_print_str("      /___/                      \n");
  klog_print_str("\n");
  klog_print_str(" OS:      ");
  klog_print_str(KYROOS_VERSION_STRING);
  klog_print_str("\n");
  ksprintf(buf, " Kernel:  KyroOS %d.%d.%d (Build %s)", KYROOS_VERSION_MAJOR,
           KYROOS_VERSION_MINOR, KYROOS_VERSION_PATCH, KYROOS_VERSION_BUILD);
  klog_print_str(buf);
  klog_print_str("\n");
  if (hrs > 0)
    ksprintf(buf, " Uptime:  %d hours, %d mins, %d secs", hrs, mins, secs);
  else if (mins > 0)
    ksprintf(buf, " Uptime:  %d mins, %d secs", mins, secs);
  else
    ksprintf(buf, " Uptime:  %d secs", secs);
  klog_print_str(buf);
  klog_print_str("\n");
  ksprintf(buf, " CPU:     %s", cpu_brand);
  klog_print_str(buf);
  klog_print_str("\n");
  if (fb) {
    ksprintf(buf, " Display: %dx%d @ %dbpp", (int)fb->width, (int)fb->height,
             (int)fb->bpp);
    klog_print_str(buf);
    klog_print_str("\n");
  }
  ksprintf(buf, " Memory:  %d MB / %d MB (%d%%)", used_mb, total_mb,
           mem_percent);
  klog_print_str(buf);
  klog_print_str("\n");
  klog_print_str(" Shell:   kyrooshell v0.3\n\n");
}

static void run_sse_test() {
  float a[4] __attribute__((aligned(16))) = {1.0f, 2.0f, 3.0f, 4.0f};
  float b[4] __attribute__((aligned(16))) = {5.0f, 6.0f, 7.0f, 8.0f};

  klog(LOG_INFO, "Testing SSE operations...");
  klog_print_str("Testing SSE operations...");
  __asm__ __volatile__ (
        "movups (%0), %%xmm0 \n\t"
        "movups (%1), %%xmm1 \n\t"
        "addps %%xmm1, %%xmm0 \n\t"
        "movups %%xmm0, (%0) \n\t"
        :
        : "r" (a), "r" (b)
        : "memory"
    );

  uint32_t raw_bits;
  memcpy(&raw_bits, &a[0], sizeof(uint32_t));

  klog(LOG_INFO, "SSE Result (IEEE 754 bits): 0x%x", (int)raw_bits);
}

void shell_init() {
  buffer_index = 0;
  memset(line_buffer, 0, BUFFER_SIZE);
  klog_print_str("\nWelcome to KyroOS Shell!\n");
  klog_print_str("Type 'help' to see available commands.\n");
  klog_print_str("Type 'kyrofetch' for system info.\n");
  klog_print_str("Discord: https://discord.gg/nSgmyadnbn\n\n");
  klog_print_str("user@kyroos:/> ");
}

static void add_to_history(const char *cmd) {
  if (strlen(cmd) == 0)
    return;
  if (history_count > 0 &&
      strcmp(history[(history_index) % HISTORY_SIZE], cmd) == 0)
    return;

  history_index = (history_index + 1) % HISTORY_SIZE;
  strncpy(history[history_index], cmd, BUFFER_SIZE);
  if (history_count < HISTORY_SIZE)
    history_count++;
  current_history_view = -1;
}

static int tokenize(char *line, char **argv, int max_args) {
  int argc = 0;
  char *ptr = line;
  while (*ptr && argc < max_args) {
    while (*ptr == ' ')
      ptr++;
    if (*ptr == '\0')
      break;

    argv[argc++] = ptr;
    while (*ptr && *ptr != ' ')
      ptr++;

    if (*ptr) {
      *ptr = '\0';
      ptr++;
    }
  }
  return argc;
}

static void shell_edit(const char *arg) {
  // Logic for the simple editor
  char full_path[256];
  if (arg[0] == '/') {
    strncpy(full_path, arg, 256);
  } else {
    if (strcmp(cwd, "/") == 0)
      ksprintf(full_path, "/%s", arg);
    else
      ksprintf(full_path, "%s/%s", cwd, arg);
  }

  vfs_node_t *file = vfs_resolve_path(vfs_root, full_path);
  if (!file) {
    // Try to create it
    char parent_path[256];
    char filename[256];
    char *last_slash = strrchr(full_path, '/');
    if (last_slash == full_path) {
      strncpy(parent_path, "/", 256);
      strncpy(filename, last_slash + 1, 256);
    } else if (last_slash) {
      int len = last_slash - full_path;
      strncpy(parent_path, full_path, len);
      parent_path[len] = '\0';
      strncpy(filename, last_slash + 1, 256);
    } else {
      // Should not happen for absolute path
      return;
    }
    vfs_node_t *parent = vfs_resolve_path(vfs_root, parent_path);
    if (parent && vfs_create(parent, filename, 0) == 0) {
      file = vfs_resolve_path(vfs_root, full_path);
    }
  }

  if (!file || !(file->flags & VFS_FILE)) {
    klog_print_str("Cannot edit this file.\n");
    return;
  }

  console_clear();
  klog_print_str("--- Kyro-Editor --- Ctrl+S to Save | Ctrl+X to Exit ---\n");

  char *edit_buffer = (char *)kmalloc(4096);
  memset(edit_buffer, 0, 4096);
  int edit_idx = 0;

  if (file->length > 0) {
    edit_idx = vfs_read(file, 0, (file->length < 4095) ? file->length : 4095,
                        (uint8_t *)edit_buffer);
    edit_buffer[edit_idx] = '\0';
    klog_print_str(edit_buffer);
  }

  while (1) {
    event_t ev;
    if (event_pop(&ev)) {
      if (ev.type == EVENT_KEY_DOWN) {
        char c = (char)ev.data1;
        uint8_t scancode = (uint8_t)ev.data2;
        bool ctrl = keyboard_is_ctrl_pressed();

        if (ctrl && (scancode == 0x2D))
          break;                          // Ctrl+X
        if (ctrl && (scancode == 0x1F)) { // Ctrl+S
          vfs_write(file, 0, edit_idx, (uint8_t *)edit_buffer);
          continue;
        }

        if (c == '\n') {
          if (edit_idx < 4095) {
            edit_buffer[edit_idx++] = '\n';
            klog_putchar('\n');
          }
        } else if (c == '\b') {
          if (edit_idx > 0) {
            edit_idx--;
            klog_print_str("\b \b");
          }
        } else if (c >= 32 && c <= 126) {
          if (edit_idx < 4095) {
            edit_buffer[edit_idx++] = c;
            klog_putchar(c);
          }
        }
      }
    }
    fb_flush();
    __asm__ __volatile__("hlt");
  }

  kfree(edit_buffer);
  console_clear();
}

static char* format_size_human_readable(uint32_t size) {
    static char buffer[32];
    const char* units[] = {"B", "KB", "MB", "GB"};
    uint32_t s = size;
    int i = 0;

    if (s == 0) {
        ksprintf(buffer, "0 B");
        return buffer;
    }

    while (s >= 1024 && i < 3) {
        s /= 1024;
        i++;
    }
    ksprintf(buffer, "%u %s", s, units[i]);
    return buffer;
}

static void execute_command(char *line) {
  if (line == NULL || strlen(line) == 0)
    return;

  add_to_history(line);

  char line_copy[256];
  strncpy(line_copy, line, 255);
  line_copy[255] = '\0';

  char *argv[16];
  int argc = tokenize(line_copy, argv, 16);
  if (argc == 0)
    return;

  char *cmd = argv[0];

  if (strcmp(cmd, "help") == 0) {
    klog_print_str("KyroOS Shell Built-in Commands:\n");
    klog_print_str("  ls [path]             - List directory contents.\n");
    klog_print_str("  cd [path]             - Change current directory.\n");
    klog_print_str("  pwd                   - Print current working directory.\n");
    klog_print_str("  cat <file>            - Concatenate and print files.\n");
    klog_print_str("  mkdir <path>          - Create a directory.\n");
    klog_print_str("  touch <file>          - Create an empty file.\n");
    klog_print_str("  rm <path>             - Remove files or empty directories.\n");
    klog_print_str("  edit <file>           - Simple text editor.\n");
    klog_print_str("  clear                 - Clear the terminal screen.\n");
    klog_print_str("  version / info        - Display KyroOS version information.\n");
    klog_print_str("  reboot                - Reboot the system.\n");
    klog_print_str("  kyrofetch             - Display system information.\n");
    klog_print_str("  install               - Run the system installer.\n");
    klog_print_str("  kpm <command> [args]  - KyroOS Package Manager (external).\n");
    klog_print_str("  game                  - Run the built-in game (external).\n");
    klog_print_str("  enable_debug          - Enable debug logging and features.\n");
    klog_print_str("  disable_debug         - Disable debug logging and features.\n");
    klog_print_str("  stat <path>           - Display file or directory status.\n");
    klog_print_str("  mount <point> <dev> <fs> - Mount a filesystem.\n");
    klog_print_str("  panic <reason>        - Trigger a kernel panic (debug mode only).\n");
    klog_print_str("\nType '[command] --help' for more information on a specific command.\n");
  } else if (strcmp(cmd, "pwd") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: pwd\n");
        klog_print_str("  Prints the current working directory.\n");
        return;
    }
    klog_print_str(cwd);
    klog_putchar('\n');
  } else if (strcmp(cmd, "cd") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: cd [path]\n");
        klog_print_str("  Changes the current working directory.\n");
        klog_print_str("  'cd' or 'cd /' changes to the root directory.\n");
        klog_print_str("  'cd ..' moves to the parent directory.\n");
        return;
    }
    if (argc < 2) {
      strncpy(cwd, "/", 256);
      return;
    }
    char *path = argv[1];
    if (strcmp(path, "..") == 0) {
      if (strcmp(cwd, "/") != 0) {
        char *last = strrchr(cwd, '/');
        if (last == cwd)
          cwd[1] = '\0';
        else if (last)
          *last = '\0';
      }
      return;
    }
    if (strcmp(path, ".") == 0)
      return;
    char full[256];
    if (path[0] == '/')
      strncpy(full, path, 256);
    else
      ksprintf(full, "%s%s%s", cwd, (strcmp(cwd, "/") == 0) ? "" : "/", path);
    klog(LOG_DEBUG, "CD: Attempting to resolve path: %s", full);
    vfs_node_t *node = vfs_resolve_path(vfs_root, full);
    if (node && (node->flags & VFS_DIRECTORY)) {
    // Canonicalize 'full' path (remove trailing slash unless it's the root)
    size_t full_len = strlen(full);
    if (full_len > 1 && full[full_len - 1] == '/') {
        full[full_len - 1] = '\0';
    }
    strncpy(cwd, full, 256); // <--- Corrected line
      klog(LOG_DEBUG, "CD: Changed directory to: %s", cwd);
    } else {
      klog_print_str("No such directory\n");
      klog(LOG_DEBUG, "CD: Failed to change directory to %s (node: %p, flags: %x)", full, node, node ? node->flags : 0);
    }
  } else if (strcmp(cmd, "ls") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: ls [path]\n");
        klog_print_str("  Lists the contents of a directory.\n");
        klog_print_str("  If no path is provided, it lists the current directory.\n");
        return;
    }
    char *path = (argc > 1) ? argv[1] : cwd;
    vfs_node_t *dir = vfs_resolve_path(vfs_root, path);
    if (dir && (dir->flags & VFS_DIRECTORY)) {
      struct dirent de;
      int i = 0;
      while (vfs_readdir(dir, i++, &de)) {
        klog_print_str(de.name);
        klog_print_str("  ");
      }
      klog_putchar('\n');
    } else
      klog_print_str("Directory not found\n");
  } else if (strcmp(cmd, "mkdir") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: mkdir <path>\n");
        klog_print_str("  Creates a new directory at the specified path.\n");
        return;
    }
    if (argc < 2) {
      klog_print_str("Usage: mkdir <path>\n");
    } else {
      char full_path[256];
      if (argv[1][0] == '/') {
        strncpy(full_path, argv[1], 256);
      } else {
        if (strcmp(cwd, "/") == 0)
          ksprintf(full_path, "/%s", argv[1]);
        else
          ksprintf(full_path, "%s/%s", argv[1]);
      }
      if (vfs_mkdir(vfs_root, full_path, 0) != 0)
        klog_print_str("mkdir failed\n");
    }
  } else if (strcmp(cmd, "cat") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: cat <file>\n");
        klog_print_str("  Concatenates file contents to standard output.\n");
        return;
    }
    if (argc < 2) {
      klog_print_str("Usage: cat <file>\n");
      return;
    }
    char full_path[256];
    if (argv[1][0] == '/') {
      strncpy(full_path, argv[1], 256);
    } else {
      if (strcmp(cwd, "/") == 0)
        ksprintf(full_path, "/%s", argv[1]);
      else
        ksprintf(full_path, "%s/%s", cwd, argv[1]);
    }
    klog(LOG_DEBUG, "CAT: Attempting to open file: %s", full_path);
    vfs_node_t *node = vfs_resolve_path(vfs_root, full_path);
    if (node && (node->flags & VFS_FILE)) {
      klog(LOG_DEBUG, "CAT: File found (node: %p, flags: %x, length: %u)", node, node->flags, node->length);
      uint8_t buf[1024];
      uint32_t r, total = 0;
      while (total < node->length) {
        r = vfs_read(node, total, 1023, buf);
        klog(LOG_DEBUG, "CAT: Read %u bytes at offset %u", r, total);
        if (r == 0) break;
        buf[r] = '\0';
        klog_print_str((char *)buf);
        total += r;
      }
      klog_putchar('\n');
    } else {
      klog_print_str("No such file or not a regular file\n");
      klog(LOG_DEBUG, "CAT: Failed to open %s (node: %p, flags: %x)", full_path, node, node ? node->flags : 0);
    }
  } else if (strcmp(cmd, "touch") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: touch <path>\n");
        klog_print_str("  Creates a new, empty file at the specified path.\n");
        klog_print_str("  If the file already exists, its timestamp might be updated (not fully implemented yet).\n");
        return;
    }
    if (argc < 2) {
      klog_print_str("Usage: touch <path>\n");
    } else {
      char full_path[256];
      if (argv[1][0] == '/') {
        strncpy(full_path, argv[1], 256);
      } else {
        if (strcmp(cwd, "/") == 0)
          ksprintf(full_path, "/%s", argv[1]);
        else
          ksprintf(full_path, "%s/%s", cwd, argv[1]);
      }
      if (vfs_create(vfs_root, full_path, 0) != 0)
        klog_print_str("touch failed\n");
    }
  } else if (strcmp(cmd, "rm") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: rm <path>\n");
        klog_print_str("  Removes a file or an empty directory at the specified path.\n");
        return;
    }
    if (argc < 2) {
      klog_print_str("Usage: rm <path>\n");
    } else {
      char full_path[256];
      if (argv[1][0] == '/') {
        strncpy(full_path, argv[1], 256);
      } else {
        if (strcmp(cwd, "/") == 0)
          ksprintf(full_path, "/%s", argv[1]);
        else
          ksprintf(full_path, "%s/%s", cwd, argv[1]);
      }
      if (vfs_remove(vfs_root, full_path) != 0)
        klog_print_str("rm failed\n");
    }
  } else if (strcmp(cmd, "stat") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: stat <path>\n");
        klog_print_str("  Displays status information about a file or directory.\n");
        return;
    }
    if (argc < 2) {
      klog_print_str("Usage: stat <path>\n");
    } else {
      char full_path[256];
      if (argv[1][0] == '/') {
        strncpy(full_path, argv[1], 256);
      } else {
        if (strcmp(cwd, "/") == 0)
          ksprintf(full_path, "/%s", argv[1]);
        else
          ksprintf(full_path, "%s/%s", cwd, argv[1]);
      }
      
      struct stat st;
      char debug_buf[128];
      ksprintf(debug_buf, "STAT_DEBUG: Attempting to stat path: %s\n", full_path);
      klog_print_str(debug_buf);

      int stat_ret = vfs_stat(vfs_root, full_path, &st);
      ksprintf(debug_buf, "STAT_DEBUG: vfs_stat returned: %d\n", stat_ret);
      klog_print_str(debug_buf);

      if (stat_ret == 0) {
        ksprintf(debug_buf, "STAT_DEBUG: Raw size: %u, Inode: %u, Mode: %x\n", st.st_size, st.st_ino, st.st_mode);
        klog_print_str(debug_buf);
        char stat_buf[128];
        ksprintf(stat_buf, "Size: %s, Inode: %d, Mode: %x\n", format_size_human_readable(st.st_size),
                 st.st_ino, st.st_mode);
        klog_print_str(stat_buf);
      } else {
        klog_print_str("stat failed\n");
        ksprintf(debug_buf, "STAT_DEBUG: vfs_stat failed for path: %s\n", full_path);
        klog_print_str(debug_buf);
      }
    }
  } else if (strcmp(cmd, "mount") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: mount <mount_point> <device_path> <fs_type>\n");
        klog_print_str("  Mounts a filesystem from a device onto a mount point.\n");
        klog_print_str("  Example: mount /mnt /dev/sda1 fs_disk\n");
        return;
    }
    if (argc < 4) {
      klog_print_str("Usage: mount <point> <device> <fs_type>\n");
    } else {
      if (vfs_mount(vfs_resolve_path(vfs_root, argv[1]),
                    vfs_resolve_path(vfs_root, argv[2]), argv[3]) == 0)
        klog_print_str("Mounted successfully\n");
      else
        klog_print_str("Mount failed\n");
    }
  } else if (strcmp(cmd, "enable_debug") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: enable_debug\n");
        klog_print_str("  Enables debug logging level and activates debug-only features (like 'panic' command).\n");
        return;
    }
    debug_mode_enabled = true;
    klog(LOG_INFO, "Debug logging enabled. Debug mode enabled.");
  } else if (strcmp(cmd, "disable_debug") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: disable_debug\n");
        klog_print_str("  Disables debug logging level and deactivates debug-only features.\n");
        return;
    }
    debug_mode_enabled = false;
    klog(LOG_INFO, "Debug logging disabled. Debug mode disabled.");
  } else if (strcmp(cmd, "clear") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: clear\n");
        klog_print_str("  Clears the terminal screen.\n");
        return;
    }
    console_clear();
  } else if (strcmp(cmd, "reboot") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: reboot\n");
        klog_print_str("  Reboots the system. WARNING: Unsaved data may be lost.\n");
        return;
    }
        klog_print_str("Executing reboot command...\n");
    klog_print_str("Attempting to reboot...\n");
    outb(0x64, 0xFE);
  } else if (strcmp(cmd, "edit") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: edit <file>\n");
        klog_print_str("  Opens a simple text editor for the specified file.\n");
        klog_print_str("  Controls: Ctrl+S to Save, Ctrl+X to Exit.\n");
        return;
    }
    if (argc > 1)
      shell_edit(argv[1]);
    else
      klog_print_str("Usage: edit <file>\n");
  } else if (strcmp(cmd, "kyrofetch") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: kyrofetch\n");
        klog_print_str("  Displays system information like OS version, CPU info, memory, and uptime.\n");
        return;
    }
    shell_kyrofetch();
  } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "info") == 0) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        klog_print_str("Usage: version / info\n");
        klog_print_str("  Displays the KyroOS version and build number.\n");
        return;
    }
    klog_print_str("KyroOS ");
    klog_print_str(KYROOS_VERSION_STRING);
    klog_print_str(" (Build ");
    klog_print_str(KYROOS_VERSION_BUILD);
    klog_print_str(")\n");
  } else if (strcmp(cmd, "panic") == 0) {
    if (!debug_mode_enabled) {
      klog_print_str("Error: Debug mode is not enabled. Use 'enable_debug' to activate.\n");
      return;
    }
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
      klog_print_str("Usage: panic <reason>\n");
      klog_print_str("Triggers a kernel panic for testing purposes. Only available in debug mode.\n");
      klog_print_str("Available reasons:\n");
      klog_print_str("  generic                 - A generic, unspecified panic.\n");
      klog_print_str("  fb_init_fail            - Framebuffer Initialization Failure.\n");
      klog_print_str("  no_runnable_threads     - No Runnable Threads in Scheduler.\n");
      klog_print_str("  kpm_main_thread_alloc_fail - Failed to Allocate Main Kernel Thread (KPM related).\n");
      klog_print_str("  returned_to_dead_thread - Returned to a Dead Thread.\n");
      klog_print_str("  kmalloc_fail            - General Memory Allocation Failure (kmalloc).\n");
      return;
    }

    panic_reason_t selected_reason = PANIC_GENERIC;
    if (strcmp(argv[1], "fb_init_fail") == 0) {
      selected_reason = PANIC_FB_INIT_FAIL;
    } else if (strcmp(argv[1], "no_runnable_threads") == 0) {
      selected_reason = PANIC_NO_RUNNABLE_THREADS;
    } else if (strcmp(argv[1], "kpm_main_thread_alloc_fail") == 0) {
      selected_reason = PANIC_KPM_MAIN_THREAD_ALLOC_FAIL;
    } else if (strcmp(argv[1], "returned_to_dead_thread") == 0) {
      selected_reason = PANIC_RETURNED_TO_DEAD_THREAD;
    } else if (strcmp(argv[1], "kmalloc_fail") == 0) {
      selected_reason = PANIC_KMALLOC_FAIL;
    }
    trigger_kernel_panic(selected_reason);
  } else if (strcmp(cmd, "iloveepsteinislanditsthebestislandindaworld") == 0) {
    klog_print_str("ты нашел эту комманду, хз зачем, вероятно ты нашёл её просто смотря код\n");
    klog_print_str("спасибо что юзаете систему, заходите в дискорд и будьте крутыми!\n");
    klog_print_str("я создал эту систему просто потому что было скучно\n");
    klog_print_str("начал я её делать в начале 2026 года, примерно в промежутке с 4го по 6ое января\n");
    klog_print_str("мне нравится её делать\n");
    klog_print_str("я хочу сделать реально крутую систему\n");
    klog_print_str("если вы хейтер или вам ненравятся бесконечные леса else if, то анлак\n");
    klog_print_str("если у вас есть идеи по улучшению системы или какие то полезные изменения\n");
    klog_print_str("то создавайте issues на гитхабе либо пишите идеи в дискорд\n");
    klog_print_str("предлагайте, что мне сделать, что убрать и что как написать\n");
    klog_print_str("спасибо!\n");
  } else if (strcmp(cmd, "DONTRUNTHIS") == 0) {
      if (debug_mode_enabled) {
        run_sse_test();
      } else {
        klog_print_str("WHY DID YOU TRY TO RUN THIS???? DONT RUN THIS!!! IT WILL CRASH KERNEL OR EVEN FUCK YOUR CPU! (enable debug mode).\n");
      }
  } else if (strcmp(cmd, "gemini") == 0) {
    klog_print_str("не\n");
  } else {
    char full_path[256];
    ksprintf(full_path, "/bin/%s", cmd);

    if (elf_exec_as_thread(full_path, argc, argv) != 0) {
      // If /bin/cmd fails, try the name as-is (in case it's a full path)
      if (elf_exec_as_thread(cmd, argc, argv) != 0) {
        klog_print_str("Unknown command: ");
        klog_print_str(cmd);
        klog_putchar('\n');
      }
    }
  }
}

static void clear_current_line() {
  while (buffer_index > 0) {
    klog_print_str("\b \b");
    buffer_index--;
  }
}

void shell_main(void *arg) {
  (void)arg;
  console_clear();
  shell_init();

  while (1) {
    event_t ev;
    if (event_pop(&ev)) {
      if (ev.type == EVENT_KEY_DOWN) {
        char c = (char)ev.data1;
        uint8_t scancode = (uint8_t)ev.data2;

        if (scancode == 0x48) { // Up
          if (history_count > 0) {
            if (current_history_view == -1)
              current_history_view = history_index;
            else if (current_history_view > 0)
              current_history_view--;
            clear_current_line();
            strncpy(line_buffer, history[current_history_view], BUFFER_SIZE);
            buffer_index = strlen(line_buffer);
            klog_print_str(line_buffer);
          }
        } else if (c == '\n') {
          klog_putchar('\n');
          line_buffer[buffer_index] = '\0';
          execute_command(line_buffer);
          buffer_index = 0;
          klog_print_str("user@kyroos:");
          klog_print_str(cwd);
          klog_print_str("> ");
          current_history_view = -1;
        } else if (c == '\b') {
          if (buffer_index > 0) {
            buffer_index--;
            klog_print_str("\b \b");
          }
        } else if (c >= 32 && c <= 126) {
          if (buffer_index < BUFFER_SIZE - 1) {
            line_buffer[buffer_index++] = c;
            klog_putchar(c);
          }
        }
      }
    }
    fb_flush();
    __asm__ __volatile__("hlt");
  }
}
