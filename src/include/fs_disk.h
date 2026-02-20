#ifndef FS_DISK_H
#define FS_DISK_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

// Filesystem constants
#define FS_SIGNATURE        0x4B59524F // "KYRO"
#define FS_BLOCK_SIZE       512        // Assuming sector size
#define FS_MAX_FILES        32         // Max number of files in the FS
#define FS_MAX_FILENAME_LEN 60         // Max filename length

// Superblock structure (at LBA 0 of the partition)
typedef struct {
    uint32_t signature;
    uint32_t version;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t next_free_block; // For simple allocation
    uint32_t file_table_start_block;
    uint32_t file_table_num_blocks;
    uint8_t  padding[FS_BLOCK_SIZE - (7 * sizeof(uint32_t))]; // Fill to block size
} __attribute__((packed)) fs_superblock_t;

// File Entry structure (for the file table)
typedef struct {
    char     filename[FS_MAX_FILENAME_LEN];
    uint32_t start_block;   // First data block
    uint32_t size_bytes;    // Size of the file in bytes
    uint32_t flags;         // E.g., is_directory, is_file
    uint8_t  padding[FS_BLOCK_SIZE - (FS_MAX_FILENAME_LEN + 3 * sizeof(uint32_t))]; // Fill to block size
} __attribute__((packed)) fs_file_entry_t;

// File Entry flags
#define FS_FILE_FLAG_FILE       0x01
#define FS_FILE_FLAG_DIRECTORY  0x02

// Functions for disk filesystem operations
int fs_format(vfs_node_t *disk_device_node, uint32_t partition_lba, uint32_t partition_size);
int fs_mount(vfs_node_t *disk_device_node, uint32_t partition_lba);
int fs_unmount(vfs_node_t *disk_device_node);

int fs_create_file(const char *path, uint32_t flags); // flags: FS_FILE_FLAG_FILE or FS_FILE_FLAG_DIRECTORY
int fs_write_file(const char *path, const uint8_t *data, size_t size);
int fs_read_file(const char *path, uint8_t *buffer, size_t size);
int fs_delete_file(const char *path);
int fs_list_dir(const char *path);
int fs_get_file_info(const char *path, fs_file_entry_t *info);
int fs_get_file_info_by_index(int index, fs_file_entry_t *info); // New: to support readdir

#endif // FS_DISK_H
