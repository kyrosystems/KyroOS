#ifndef KYROFS_H
#define KYROFS_H

#include "vfs.h"
#include "limine.h"
#include <stdint.h>


#define KYROFS_MAGIC            0x4B59524F46533032ULL // "KYROFS02" 
#define KYROFS_BLOCK_SIZE       4096
#define KYROFS_MAX_EXTENTS      12
#define KYROFS_BLOCKS_PER_GROUP 8192  // 8192 * 4096B = 32MB per group
#define KYROFS_ROOT_INODE       1
#define KYROFS_JOURNAL_SLOTS    64    // each slot = 2 blocks (hdr+data) 
#define KYROFS_JOURNAL_MAGIC    0x4A524E4Cu // "JRNL" 

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t root_inode;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t group_count;
    uint64_t group_desc_block;
    uint64_t journal_start;
    uint64_t journal_blocks;
    uint32_t journal_seq;
    uint32_t crc32;
    uint8_t  clean_unmount;
    uint8_t  reserved[411];
} __attribute__((packed)) kyrofs_superblock_t;

typedef struct {
    uint64_t block_bitmap_block;
    uint64_t inode_bitmap_block;
    uint64_t inode_table_block;
    uint64_t data_block_start;
    uint32_t group_blocks;
    uint32_t group_inodes;
    uint32_t free_blocks_in_group;
    uint32_t free_inodes_in_group;
    uint32_t crc32;
    uint32_t reserved;
} __attribute__((packed)) kyrofs_group_desc_t;

typedef struct {
    uint64_t start_block;
    uint64_t length;
} __attribute__((packed)) kyrofs_extent_t;

#define KYROFS_MODE_FILE 0x8000
#define KYROFS_MODE_DIR  0x4000

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t blocks;
    uint64_t atime, mtime, ctime;
    uint32_t nlinks;
    uint32_t extent_count;
    kyrofs_extent_t extents[KYROFS_MAX_EXTENTS];
    uint32_t crc32;
    uint8_t  reserved[12];
} __attribute__((packed)) kyrofs_inode_t;

typedef struct {
    uint64_t inode;
    uint16_t rec_len;
    uint16_t name_len;
    uint8_t  file_type;
    uint8_t  reserved[3];
    char     name[240];
} __attribute__((packed)) kyrofs_dirent_t;

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint64_t target_block;
    uint32_t payload_crc32;
    uint8_t  committed;
    uint8_t  applied;
    uint8_t  reserved[4078];
} __attribute__((packed)) kyrofs_journal_header_t;

typedef struct {
    kyrofs_inode_t inode;
    uint64_t inode_num;
    int refcount;
    int dirty;
} kyrofs_inode_info_t;

void kyrofs_init(struct limine_module_response *mod_resp);
int kyrofs_add_file(const char *full_path, void *data, uint32_t size);
vfs_node_t *get_kyrofs_root();
int kyrofs_create_node(vfs_node_t *parent, char *name, uint32_t flags);

int kyrofs_format(vfs_node_t *disk_device_node, uint32_t partition_lba, uint64_t partition_blocks);
int kyrofs_mount(vfs_node_t *mount_point, vfs_node_t *device_node);
int kyrofs_unmount(vfs_node_t *mount_point);
int kyrofs_journal_write(uint64_t target_block, const void *data);
int kyrofs_journal_replay(void);
void kyrofs_register(void);

#endif // KYROFS_H