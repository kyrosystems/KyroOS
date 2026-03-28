#ifndef KYROFS_H
#define KYROFS_H

#include "vfs.h"
#include "limine.h"
#include <stdint.h>

// KyroFS on-disk format structures
#define KYROFS_MAGIC 0x4B59524F46533031  // "KYROFS01"
#define KYROFS_BLOCK_SIZE 4096
#define KYROFS_MAX_EXTENTS 64

// Superblock structure
typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t inode_count;
    uint64_t root_inode;
    uint64_t journal_start;
    uint64_t journal_blocks;
    uint8_t  reserved[384];
} __attribute__((packed)) kyrofs_superblock_t;

// Inode structure
typedef struct {
    uint32_t mode;      // File mode (type + permissions)
    uint32_t uid;       // Owner UID
    uint32_t gid;       // Owner GID
    uint64_t size;      // File size
    uint64_t blocks;    // Number of blocks
    uint64_t atime;     // Access time
    uint64_t mtime;     // Modification time
    uint64_t ctime;     // Change time
    uint32_t nlinks;    // Link count
    uint32_t extent_count;
    struct {
        uint64_t start_block;
        uint64_t length;
    } extents[KYROFS_MAX_EXTENTS];
} __attribute__((packed)) kyrofs_inode_t;

// Directory entry structure
typedef struct {
    uint64_t inode;
    uint16_t name_len;
    uint8_t  file_type;
    uint8_t  reserved;
    char     name[255];
} __attribute__((packed)) kyrofs_dirent_t;

// Journal entry types
#define KYROFS_JOURNAL_ADD_BLOCK    1
#define KYROFS_JOURNAL_FREE_BLOCK   2
#define KYROFS_JOURNAL_CREATE_INODE 3
#define KYROFS_JOURNAL_DELETE_INODE 4

typedef struct {
    uint32_t type;
    uint64_t block;
    uint64_t data[64];
} __attribute__((packed)) kyrofs_journal_entry_t;

// In-memory structures
typedef struct {
    kyrofs_inode_t inode;
    uint64_t inode_num;
    int refcount;
    int dirty;
} kyrofs_inode_info_t;

typedef struct {
    vfs_node_t vfs_node;
    kyrofs_inode_info_t *inode_info;
    uint64_t file_pos;
} kyrofs_file_t;

// Functions
void kyrofs_init(struct limine_module_response *mod_resp);
int kyrofs_add_file(const char *full_path, void *data, uint32_t size);
vfs_node_t *get_kyrofs_root();
int kyrofs_create_node(vfs_node_t *parent, char *name, uint32_t flags);

// Mount/unmount
int kyrofs_mount(vfs_node_t *mount_point, vfs_node_t *device_node);
int kyrofs_unmount(vfs_node_t *mount_point);

// Journal functions
int kyrofs_journal_write(kyrofs_journal_entry_t *entry);
int kyrofs_journal_replay(void);

#endif // KYROFS_H
