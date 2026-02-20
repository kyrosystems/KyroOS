#include <errno.h>
#include "../../include/kpm.h"
#include "../kyroolib.h"

// Simple helper to parse "key=value" from info.txt
static void parse_info(char *buf, const char *key, char *value) {
    char *p = buf;
    while (*p) {
        if (memcmp(p, key, strlen(key)) == 0 && p[strlen(key)] == '=') {
            char *start = p + strlen(key) + 1;
            char *end = start;
            while (*end && *end != '\n' && *end != '\r') { // Handle both \n and \r\n
                end++;
            }
            memcpy(value, start, end - start);
            value[end - start] = '\0';
            return;
        }
        // Move to next line
        while (*p && *p != '\n' && *p != '\r') p++; // Handle both \n and \r\n
        if (*p == '\r') p++; // Skip \r if present
        if (*p) p++;
    }
}

// Helper to create directories recursively
static int mkdir_p(char *path) {
    char *p = path;
    if (*p == '/') p++; // Skip leading slash

    while (*p) {
        if (*p == '/') {
            *p = '\0'; // Temporarily terminate string
            int ret = mkdir(path);
            if (ret < 0 && ret != -EEXIST) {
                // It's an error other than "already exists", so we should fail.
                *p = '/'; // Restore the slash before returning
                return ret;
            }
            *p = '/'; // Restore slash
        }
        p++;
    }
    // If path is a directory name (e.g., "/a/b/"), ensure the last component is also created
    // This part might be redundant if the loop already creates the last directory,
    // but it handles cases where path is just "/a" or "a"
    if (strlen(path) > 0) {
        int ret = mkdir(path);
        if (ret < 0 && ret != -EEXIST) {
            return ret;
        }
    }
    return 0; // Success
}

int pkg_install_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("kpm: could not open package file.\n");
        return -1;
    }

    print("Installing from ");
    print(path);
    print("...\n");

    char name[128], version[32], install_path[256];
    char meta_buf[512];
    int meta_read = 0;

    // First pass: find and parse metadata
    kpkg_entry_t entry;
    while (read(fd, &entry, sizeof(kpkg_entry_t)) == sizeof(kpkg_entry_t)) {
        if (entry.is_meta && strcmp(entry.filename, "meta/info.txt") == 0) {
            if (read(fd, meta_buf, entry.size) == (int)entry.size) {
                meta_buf[entry.size] = '\0';
                parse_info(meta_buf, "name", name);
                parse_info(meta_buf, "version", version);
                parse_info(meta_buf, "install_path", install_path);
                meta_read = 1;
            }
            // We found meta, break to restart reading for data files
            break; 
        } else {
            // This is very inefficient. A real `read` syscall would have `seek`.
            // Without seek, we have to read the data to skip it.
            char discard_buf[1024];
            int to_read = entry.size;
             while (to_read > 0) {
                int n = read(fd, discard_buf, (to_read > 1024 ? 1024 : to_read));
                if (n <= 0) break;
                to_read -= n;
            }
        }
    }

    if (!meta_read) {
        print("kpm: could not find or read meta/info.txt in package.\n");
        close(fd);
        return -1;
    }
    
    // Re-open file to read data entries
    close(fd);
    fd = open(path, O_RDONLY);

    // Second pass: extract data files
    char file_buf[1024];
    while (read(fd, &entry, sizeof(kpkg_entry_t)) == sizeof(kpkg_entry_t)) {
        int to_read = entry.size;
        if (!entry.is_meta) {
            char full_path[512];
            // Combine install_path and entry.filename
            if (strcmp(install_path, "/") == 0) {
                strcpy(full_path, entry.filename);
            } else {
                strcpy(full_path, install_path);
                strcat(full_path, "/");
                strcat(full_path, entry.filename);
            }

            // Check if it's a directory (size 0, usually implies directory)
            // or if the filename ends with a slash (convention for directories)
            if (entry.size == 0 || full_path[strlen(full_path) - 1] == '/') {
                print("  Creating directory ");
                print(full_path);
                print("\n");
                mkdir_p(full_path);
                // Even if it's a directory, we still need to consume any potential data
                // (though a directory entry should ideally have size 0)
                while (to_read > 0) {
                    int n = read(fd, file_buf, (to_read > 1024 ? 1024 : to_read));
                    if (n <= 0) break;
                    to_read -= n;
                }
                continue; // Move to next entry
            }

            // It's a file, ensure its parent directories exist
            char dir_path[512];
            strcpy(dir_path, full_path);
            char *last_slash = strrchr(dir_path, '/');
            if (last_slash) {
                *last_slash = '\0'; // Null-terminate to get only the directory path
                mkdir_p(dir_path);
            }

            print("  Extracting ");
            print(full_path);
            print("\n");

            int out_fd = create(full_path);
            if (out_fd < 0) {
                print("kpm: failed to create file '");
                print(full_path);
                print("'\n");
                // skip this file's data
                while (to_read > 0) {
                    int n = read(fd, file_buf, (to_read > 1024 ? 1024 : to_read));
                    if (n <= 0) break;
                    to_read -= n;
                }
                continue;
            }
            
            while (to_read > 0) {
                int n = read(fd, file_buf, (to_read > 1024 ? 1024 : to_read));
                if (n <= 0) break;
                write(out_fd, file_buf, n);
                to_read -= n;
            }
            close(out_fd);
        } else {
            // Skip meta section
             while (to_read > 0) {
                int n = read(fd, file_buf, (to_read > 1024 ? 1024 : to_read));
                if (n <= 0) break;
                to_read -= n;
            }
        }
    }
    close(fd);

    // Register in database
    int db_fd = open(PKG_DB_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (db_fd > 0) {
        char entry_line[512];
        strcpy(entry_line, name);
        strcat(entry_line, "|");
        strcat(entry_line, version);
        strcat(entry_line, "|");
        strcat(entry_line, install_path);
        strcat(entry_line, "\n");
        
        // This assumes a write that appends. Our current VFS does not support this well.
        // It will overwrite. This needs to be improved in the VFS/FS layer.
        write(db_fd, entry_line, strlen(entry_line));
        close(db_fd);
    } else {
        print("kpm: could not open package database to register package.\n");
    }

    print("Package installation finished.\n");
    return 0;
}
