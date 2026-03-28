#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#define MAX_FILENAME_LEN 256

// Define structs first to avoid warnings
struct dirent {
  char name[MAX_FILENAME_LEN];
  uint32_t ino;
};

struct stat {
    uint32_t st_size; // size of file in bytes
    uint32_t st_mode; // protection (file type and permission)
    uint32_t st_ino;  // inode number - FIXED TYPO uint33_t to uint32_t
    // More fields could be added as needed (e.g., st_atime, st_mtime, st_ctime)
};

struct vfs_node; // Forward declaration

// VFS file operations (typedefs before vfs_node_t)
typedef uint32_t (*read_vfs_t)(struct vfs_node *node, uint64_t offset,
                               uint32_t size, uint8_t *buffer);
typedef uint32_t (*write_vfs_t)(struct vfs_node *node, uint64_t offset,
                                uint32_t size, uint8_t *buffer);
typedef void (*open_vfs_t)(struct vfs_node *node, int flags);
typedef void (*close_vfs_t)(struct vfs_node *node);
typedef struct vfs_node *(*finddir_vfs_t)(struct vfs_node *node, char *name);
typedef int (*mkdir_vfs_t)(struct vfs_node *node, char *name, uint16_t mode);
typedef int (*create_vfs_t)(struct vfs_node *node, char *name, uint16_t mode);
typedef int (*readdir_vfs_t)(struct vfs_node *node, uint32_t index, struct dirent *dir_entry);
typedef int (*remove_vfs_t)(struct vfs_node *node, char *name);
typedef int (*rmdir_vfs_t)(struct vfs_node *node, char *name, uint16_t mode);
typedef int (*stat_vfs_t)(struct vfs_node *node, struct stat *stat_buf);
typedef int (*ioctl_vfs_t)(struct vfs_node *node, int request, void* argp);
typedef int (*mount_vfs_t)(struct vfs_node *mount_point, struct vfs_node *device_node, const char* fs_type_name); // Updated signature
typedef int (*unmount_vfs_t)(struct vfs_node *mount_point); // New: for unmounting filesystems

// Structure to define a filesystem type
struct filesystem_type {
    char name[32];
    int (*mount_func)(struct vfs_node *mount_point, struct vfs_node *device_node);
    struct filesystem_type *next; // Linked list of registered filesystems
};

typedef struct vfs_node {
  char name[MAX_FILENAME_LEN];
  uint32_t flags;
  uint32_t length;
  uint32_t inode;
  void *ptr; // Used by mounted filesystems

  // Function pointers for file operations
  read_vfs_t read;
  write_vfs_t write;
  open_vfs_t open;
  close_vfs_t close;
  finddir_vfs_t finddir;
  readdir_vfs_t readdir;
  mkdir_vfs_t mkdir;
  create_vfs_t create;
  remove_vfs_t remove;
  rmdir_vfs_t rmdir;
  stat_vfs_t stat;
  ioctl_vfs_t ioctl;
  mount_vfs_t mount;
} vfs_node_t;

// st_mode flags (simplified from POSIX)
#define S_IFMT      0xF000      // bitmask for the file type bit fields
#define S_IFDIR     0x4000      // directory
#define S_IFREG     0x8000      // regular file
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)  // test for directory
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)  // test for regular file

// Flags for VFS nodes
#define VFS_FILE 0x01
#define VFS_DIRECTORY 0x02
#define VFS_MOUNTPOINT 0x04
#define VFS_CHARDEVICE 0x08

// Standard open flags (simplified from POSIX)
#define O_RDONLY    0x0001 // Open for reading only
#define O_WRONLY    0x0002 // Open for writing only
#define O_RDWR      0x0003 // Open for reading and writing
#define O_CREAT     0x0040 // Create file if it does not exist
#define O_TRUNC     0x0200 // Truncate size to 0
#define O_APPEND    0x0400 // Append mode

// Root of the VFS
extern vfs_node_t *vfs_root;

void vfs_init();
void vfs_open(vfs_node_t *node, int flags); // New declaration
uint32_t vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                  uint8_t *buffer);
uint32_t vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                   uint8_t *buffer);
vfs_node_t *vfs_finddir(vfs_node_t *node, char *name);
int vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry); // Changed return type and added arg
int vfs_mkdir(vfs_node_t *root, const char *path, uint16_t mode);
int vfs_create(vfs_node_t *root, const char *path, uint16_t mode);
int vfs_remove(vfs_node_t *root, const char *path);
vfs_node_t *vfs_resolve_path(vfs_node_t *root, const char *path);
uint32_t vfs_get_next_inode();
int vfs_rmdir(vfs_node_t *root, const char *path, uint16_t mode); // New: for removing directories
int vfs_stat(vfs_node_t *root, const char *path, struct stat *stat_buf); // New: for getting file/dir info
int vfs_ioctl(vfs_node_t *node, int request, void* argp); // New: for device-specific control
int vfs_mount(vfs_node_t *mount_point, vfs_node_t *device_node, const char* fs_type_name); // New: for mounting filesystems
int vfs_unmount(vfs_node_t *mount_point); // New: for unmounting filesystems
void register_filesystem(struct filesystem_type *fs); // New: to register filesystem drivers

int kernel_ioctl(int fd, int request, void* argp); // New: kernel-internal ioctl
uint32_t vfs_get_next_inode(); // New: Function to get a unique inode number

#endif // VFS_H