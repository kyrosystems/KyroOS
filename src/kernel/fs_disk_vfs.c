#include "fs_disk_vfs.h"
#include "fs_disk.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "vfs.h"
#include "thread.h" // For get_current_thread (to get disk_fd)

// --- VFS Node Operations for Mounted FS_DISK ---
// These functions will call the fs_disk.c functions

static int fs_disk_vfs_stat(vfs_node_t *node, struct stat *stat_buf); // Forward declaration
static int fs_disk_vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry); // Forward declaration

static uint32_t fs_disk_vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    if (node->flags & VFS_DIRECTORY) {
        return (uint32_t)-1; // Cannot read a directory as a file
    }
    // For simple flat FS_DISK, path is actually node->name

    if (offset != 0) {
        klog(LOG_WARN, "FS_DISK_VFS: Offset not supported for read yet.");
        return (uint32_t)-1;
    }
    return fs_read_file(node->name, buffer, size);
}

static uint32_t fs_disk_vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    if (node->flags & VFS_DIRECTORY) {
        return (uint32_t)-1; // Cannot write a directory as a file
    }
    // For simple flat FS_DISK, path is actually node->name

    if (offset != 0) {
        klog(LOG_WARN, "FS_DISK_VFS: Offset not supported for write yet.");
        return (uint32_t)-1;
    }
    return fs_write_file(node->name, buffer, size);
}

static void fs_disk_vfs_open(vfs_node_t *node, int flags) {
    // fs_disk.c doesn't have an explicit open, it operates on path.

    (void)node;
    (void)flags;
}

static void fs_disk_vfs_close(vfs_node_t *node) {
    // fs_disk.c doesn't have an explicit close
    (void)node;
}

static vfs_node_t *fs_disk_vfs_finddir(vfs_node_t *node, char *name) {
    // For simple flat FS_DISK, all files are in root.
    // If node is the mount point, search for name.
    if (node->flags & VFS_MOUNTPOINT) {
        fs_file_entry_t file_info;
        if (fs_get_file_info(name, &file_info) == 0) {
            // Found it, create a temporary VFS node for it
            vfs_node_t *found_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
            if (found_node) {
                memset(found_node, 0, sizeof(vfs_node_t));
                strncpy(found_node->name, name, MAX_FILENAME_LEN - 1);
                found_node->length = file_info.size_bytes;
                found_node->flags = 0;
                if (file_info.flags & FS_FILE_FLAG_FILE) found_node->flags |= VFS_FILE;
                if (file_info.flags & FS_FILE_FLAG_DIRECTORY) found_node->flags |= VFS_DIRECTORY;
                
                // Assign operations
                found_node->read = fs_disk_vfs_read;
                found_node->write = fs_disk_vfs_write;
                found_node->open = fs_disk_vfs_open;
                found_node->close = fs_disk_vfs_close;
                found_node->stat = fs_disk_vfs_stat;
                // Directories in simple flat FS cannot be further traversed
                if (found_node->flags & VFS_DIRECTORY) {
                    found_node->finddir = fs_disk_vfs_finddir;
                    found_node->readdir = fs_disk_vfs_readdir;
                }
                return found_node;
            }
        }
    }
    return NULL; // Not found
}

static int fs_disk_vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
    // For simple flat FS_DISK, all files are in root.
    // We need to iterate through the fs_disk's file table.
    if (!(node->flags & VFS_MOUNTPOINT)) { // Only readdir on the mount point
        return 0;
    }

    // This is crude: we directly access the fs_disk's internal file table logic.
    // A better VFS would have a generic way to iterate FS entries.

    uint32_t count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_file_entry_t file_info;
        if (fs_get_file_info_by_index(i, &file_info) == 0 && file_info.filename[0] != '\0') {
            if (count == index) {
                strncpy(dir_entry->name, file_info.filename, MAX_FILENAME_LEN - 1);
                dir_entry->ino = i;
                return 1; // Found entry at index
            }
            count++;
        }
    }
    return 0; // End of directory or invalid index
}

static int fs_disk_vfs_mkdir(vfs_node_t *node, char *name, uint16_t mode) {
    (void)node; (void)mode;
    return fs_create_file(name, FS_FILE_FLAG_DIRECTORY);
}

static int fs_disk_vfs_create(vfs_node_t *node, char *name, uint16_t mode) {
    (void)node; (void)mode;
    return fs_create_file(name, FS_FILE_FLAG_FILE);
}

static int fs_disk_vfs_remove(vfs_node_t *node, char *name) {
    (void)node;
    return fs_delete_file(name);
}

static int fs_disk_vfs_rmdir(vfs_node_t *node, char *name, uint16_t mode) {
    (void)node; (void)mode;
    // Simple FS_DISK doesn't enforce empty check for directories.
    // Assume a directory can be "deleted" if it's in the file table.
    return fs_delete_file(name); // Deleting a "directory" entry is same as file
}

static int fs_disk_vfs_stat(vfs_node_t *node, struct stat *stat_buf) {
    fs_file_entry_t file_info;
    if (fs_get_file_info(node->name, &file_info) == 0) {
        stat_buf->st_size = file_info.size_bytes;
        stat_buf->st_ino = 0; // Inode not meaningful in simple FS_DISK
        stat_buf->st_mode = 0;
        if (file_info.flags & FS_FILE_FLAG_FILE) stat_buf->st_mode |= S_IFREG;
        if (file_info.flags & FS_FILE_FLAG_DIRECTORY) stat_buf->st_mode |= S_IFDIR;
        stat_buf->st_mode |= 0755; // Default permissions
        return 0;
    }
    return -1;
}

static int fs_disk_vfs_ioctl(vfs_node_t *node, int request, void* argp) {
    // fs_disk.c does not expose IOCTLs directly.
    // All operations are through file-like functions.
    (void)node; (void)request; (void)argp;
    return -1;
}


// --- Mount Function for FS_DISK ---
int fs_disk_mount_func(vfs_node_t *mount_point, vfs_node_t *device_node) {
    if (!device_node || device_node->read == NULL || device_node->ioctl == NULL) {
        klog(LOG_ERROR, "FS_DISK_VFS: Invalid device node for mounting.");
        return -1;
    }

    // We need the raw disk_fd and partition LBA from device_node.
    // This is a hacky way to get the fd of /dev/sda
    int disk_fd = -1;
    thread_t *curr_thread = get_current_thread();
    for(int i = 0; i < MAX_FILES; i++) {
        if (curr_thread->fd_table[i].type == FD_TYPE_FILE && curr_thread->fd_table[i].data.file.node == device_node) {
            disk_fd = i;
            break;
        }
    }
    if (disk_fd == -1) {
        klog(LOG_ERROR, "FS_DISK_VFS: Could not find fd for device node.");
        return -1;
    }

    // Assume device_node->ptr holds the partition LBA for /dev/sdaX
    uint32_t partition_lba = (uint32_t)(uintptr_t)device_node->ptr;

    if (fs_mount(device_node, partition_lba) != 0) {
        klog(LOG_ERROR, "FS_DISK_VFS: fs_mount failed.");
        return -1;
    }

    // The mount_point now takes on the characteristics of the mounted FS_DISK's root
    mount_point->flags |= VFS_MOUNTPOINT | VFS_DIRECTORY;
    mount_point->read = NULL; // Root of mounted FS is not readable as file
    mount_point->write = NULL; // Root of mounted FS is not writable as file
    mount_point->open = fs_disk_vfs_open;
    mount_point->close = fs_disk_vfs_close;
    mount_point->finddir = fs_disk_vfs_finddir;
    mount_point->readdir = fs_disk_vfs_readdir;
    mount_point->mkdir = fs_disk_vfs_mkdir;
    mount_point->create = fs_disk_vfs_create;
    mount_point->remove = fs_disk_vfs_remove;
    mount_point->rmdir = fs_disk_vfs_rmdir;
    mount_point->stat = fs_disk_vfs_stat;
    mount_point->ioctl = fs_disk_vfs_ioctl; // Pass through if needed by FS
    mount_point->ptr = NULL; // FS_DISK manages its own global state

    klog(LOG_INFO, "FS_DISK_VFS: Mounted FS_DISK successfully.");
    return 0;
}

// Global filesystem_type structure for FS_DISK
struct filesystem_type fs_disk_filesystem = {
    .name = "fs_disk",
    .mount_func = fs_disk_mount_func,
    .next = NULL // Initialized as NULL, VFS layer manages linked list
};

void fs_disk_vfs_init() {
    register_filesystem(&fs_disk_filesystem);
    klog(LOG_INFO, "FS_DISK VFS interface initialized and registered.");
}
