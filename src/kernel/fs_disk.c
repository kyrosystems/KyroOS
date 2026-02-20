#include "fs_disk.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "ide.h" // For ide_read_sectors, ide_write_sectors, ide_disk_info_t
#include "vfs.h" // For VFS node interaction
#include "thread.h" // For get_current_thread (to get disk_fd)

// Global state for the mounted filesystem
static vfs_node_t *mounted_disk_node = NULL;
static uint32_t mounted_partition_lba = 0;
static fs_superblock_t current_superblock;
static fs_file_entry_t *file_table_cache = NULL; // Cache the file table in memory

// Helper to read a block from the disk
static int read_block(uint32_t lba, uint8_t *buffer) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted. (read_block)");
        return -1;
    }
    // Need to convert relative LBA to absolute LBA on the disk
    uint32_t absolute_lba = mounted_partition_lba + lba;
    ide_read_blocks_t rb = {
        .lba = absolute_lba,
        .count = 1,
        .buffer = buffer
    };
    if (mounted_disk_node->ioctl(mounted_disk_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to read block LBA %u from disk. (read_block)", absolute_lba);
        return -1;
    }
    return 0;
}

// Helper to write a block to the disk
static int write_block(uint32_t lba, const uint8_t *buffer) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted. (write_block)");
        return -1;
    }
    uint32_t absolute_lba = mounted_partition_lba + lba;
    ide_write_blocks_t wb = {
        .lba = absolute_lba,
        .count = 1,
        .buffer = (void*)buffer // Cast away const
    };
    if (mounted_disk_node->ioctl(mounted_disk_node, IDE_IOCTL_WRITE_BLOCKS, &wb) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to write block LBA %u to disk. (write_block)", absolute_lba);
        return -1;
    }
    return 0;
}

// Helper to update the superblock on disk
static int update_superblock() {
    return write_block(0, (uint8_t*)&current_superblock);
}

// Helper to update the file table on disk
static int update_file_table() {
    if (!file_table_cache) return -1;
    for (uint32_t i = 0; i < current_superblock.file_table_num_blocks; i++) {
        if (write_block(current_superblock.file_table_start_block + i, ((uint8_t*)file_table_cache) + (i * FS_BLOCK_SIZE)) != 0) {
            return -1; // Error on writing any block
        }
    }
    return 0; // Success
}

int fs_format(vfs_node_t *disk_device_node, uint32_t partition_lba, uint32_t partition_size) {
    klog(LOG_INFO, "FS_DISK: Formatting partition LBA %u, size %u", partition_lba, partition_size);

    // Initialize superblock
    fs_superblock_t superblock;
    memset(&superblock, 0, sizeof(fs_superblock_t));
    superblock.signature = FS_SIGNATURE;
    superblock.version = 1;
    superblock.total_blocks = partition_size;
    superblock.file_table_start_block = 1; // Superblock at 0, file table starts at 1
    // Calculate how many blocks the file table needs
    superblock.file_table_num_blocks = (sizeof(fs_file_entry_t) * FS_MAX_FILES + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    superblock.next_free_block = superblock.file_table_start_block + superblock.file_table_num_blocks;
    superblock.free_blocks = superblock.total_blocks - superblock.next_free_block;

    // Write superblock
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &(ide_write_blocks_t){.lba = partition_lba + 0, .count = 1, .buffer = &superblock}) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to write superblock.");
        return -1;
    }

    // Initialize file table
    // For simplicity, zero out the blocks for the file table
    uint8_t zero_block[FS_BLOCK_SIZE];
    memset(zero_block, 0, FS_BLOCK_SIZE);
    for (uint32_t i = 0; i < superblock.file_table_num_blocks; i++) {
        if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &(ide_write_blocks_t){.lba = partition_lba + superblock.file_table_start_block + i, .count = 1, .buffer = zero_block}) != 0) {
            klog(LOG_ERROR, "FS_DISK: Failed to zero out file table block %u.", i);
            return -1;
        }
    }

    // Clear data blocks (optional, can be done on write)

    klog(LOG_INFO, "FS_DISK: Partition formatted successfully.");
    return 0;
}

int fs_mount(vfs_node_t *disk_device_node, uint32_t partition_lba) {
    klog(LOG_INFO, "FS_DISK: Mounting filesystem on LBA %u...", partition_lba);

    // Read superblock
    fs_superblock_t superblock;
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_READ_BLOCKS, &(ide_read_blocks_t){.lba = partition_lba + 0, .count = 1, .buffer = &superblock}) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to read superblock.");
        return -1;
    }

    if (superblock.signature != FS_SIGNATURE) {
        klog(LOG_ERROR, "FS_DISK: Invalid filesystem signature %x.", superblock.signature);
        return -1;
    }

    // Cache file table
    if (file_table_cache) {
        kfree(file_table_cache);
    }
    file_table_cache = (fs_file_entry_t*)kmalloc(superblock.file_table_num_blocks * FS_BLOCK_SIZE);
    if (!file_table_cache) {
        klog(LOG_ERROR, "FS_DISK: Failed to allocate file table cache.");
        return -1;
    }
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_READ_BLOCKS, &(ide_read_blocks_t){.lba = partition_lba + superblock.file_table_start_block, .count = superblock.file_table_num_blocks, .buffer = file_table_cache}) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to read file table into cache.");
        kfree(file_table_cache);
        file_table_cache = NULL;
        return -1;
    }

    mounted_disk_node = disk_device_node;
    mounted_partition_lba = partition_lba;
    current_superblock = superblock;

    klog(LOG_INFO, "FS_DISK: Filesystem mounted successfully. Total blocks: %u, Free blocks: %u", current_superblock.total_blocks, current_superblock.free_blocks);
    return 0;
}

int fs_unmount(vfs_node_t *disk_device_node) {
    (void)disk_device_node; // Suppress unused parameter warning
    if (!mounted_disk_node) {
        klog(LOG_WARN, "FS_DISK: No filesystem currently mounted.");
        return 0;
    }
    
    // Write back any cached changes (superblock, file table)
    update_superblock();
    update_file_table();

    if (file_table_cache) {
        kfree(file_table_cache);
        file_table_cache = NULL;
    }
    mounted_disk_node = NULL;
    mounted_partition_lba = 0;
    klog(LOG_INFO, "FS_DISK: Filesystem unmounted.");
    return 0;
}

// Find a free file entry slot
static int find_free_file_entry() {
    if (!file_table_cache) return -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (file_table_cache[i].filename[0] == '\0') { // Check if entry is empty
            return i;
        }
    }
    return -1; // No free entry
}

// Find a file entry by path
static int find_file_entry(const char *path) {
    if (!file_table_cache) return -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (file_table_cache[i].filename[0] != '\0' && strcmp(file_table_cache[i].filename, path) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

// Allocate a free data block
static uint32_t allocate_block() {
    if (current_superblock.free_blocks == 0) {
        klog(LOG_ERROR, "FS_DISK: No free blocks available.");
        return 0;
    }
    uint32_t block = current_superblock.next_free_block;
    current_superblock.next_free_block++;
    current_superblock.free_blocks--;
    update_superblock();
    return block;
}

int fs_write_file(const char *path, const uint8_t *data, size_t size) {
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) {
        klog(LOG_ERROR, "FS_DISK: File not found for writing: %s", path);
        return -1;
    }

    fs_file_entry_t *entry = &file_table_cache[entry_idx];
    if (!(entry->flags & FS_FILE_FLAG_FILE)) {
        klog(LOG_ERROR, "FS_DISK: Can only write to regular files: %s", path);
        return -1;
    }

    // For simplicity, overwrite entire file data in new blocks
    // This is very inefficient for small writes and fragmented files
    uint32_t needed_blocks = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    if (needed_blocks > current_superblock.free_blocks) {
        klog(LOG_ERROR, "FS_DISK: Not enough free blocks for file: %s", path);
        return -1;
    }

    uint32_t current_block_lba = allocate_block(); // Allocate first block
    entry->start_block = current_block_lba;
    
    // Write data block by block
    for (uint32_t i = 0; i < needed_blocks; i++) {
        uint8_t block_buf[FS_BLOCK_SIZE];
        size_t bytes_to_write = (size - i * FS_BLOCK_SIZE > FS_BLOCK_SIZE) ? FS_BLOCK_SIZE : (size - i * FS_BLOCK_SIZE);
        memset(block_buf, 0, FS_BLOCK_SIZE); // Clear block to avoid old data
        memcpy(block_buf, data + i * FS_BLOCK_SIZE, bytes_to_write);

        if (write_block(current_block_lba + i, block_buf) != 0) {
            klog(LOG_ERROR, "FS_DISK: Failed to write data block for %s.", path);
            return -1;
        }
    }
    entry->size_bytes = size;
    update_file_table();
    klog(LOG_INFO, "FS_DISK: Wrote %u bytes to file: %s", size, path);
    return size;
}

int fs_read_file(const char *path, uint8_t *buffer, size_t size) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted.");
        return -1;
    }
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) {
        klog(LOG_ERROR, "FS_DISK: File not found for reading: %s", path);
        return -1;
    }

    fs_file_entry_t *entry = &file_table_cache[entry_idx];
    if (!(entry->flags & FS_FILE_FLAG_FILE)) {
        klog(LOG_ERROR, "FS_DISK: Can only read from regular files: %s", path);
        return -1;
    }

    size_t bytes_to_read = size;
    if (bytes_to_read > entry->size_bytes) {
        bytes_to_read = entry->size_bytes;
    }

    uint32_t current_block_lba = entry->start_block;
    for (size_t i = 0; i < (bytes_to_read + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE; i++) {
        uint8_t block_buf[FS_BLOCK_SIZE];
        if (read_block(current_block_lba + i, block_buf) != 0) {
            klog(LOG_ERROR, "FS_DISK: Failed to read data block for %s.", path);
            return -1;
        }
        size_t copy_len = (bytes_to_read - i * FS_BLOCK_SIZE > FS_BLOCK_SIZE) ? FS_BLOCK_SIZE : (bytes_to_read - i * FS_BLOCK_SIZE);
        memcpy(buffer + i * FS_BLOCK_SIZE, block_buf, copy_len);
    }
    klog(LOG_INFO, "FS_DISK: Read %u bytes from file: %s", bytes_to_read, path);
    return bytes_to_read;
}

int fs_list_dir(const char *path) {
    (void)path; // Suppress unused parameter warning
    // For this simple flat FS, there's effectively only one root directory.
    // So "path" is ignored, we list all files.
    klog(LOG_INFO, "FS_DISK: Listing files (all files are in root for simple FS):");
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (file_table_cache[i].filename[0] != '\0') {
            klog(LOG_INFO, "  - %s (size: %u bytes, start block: %u)",
                file_table_cache[i].filename, file_table_cache[i].size_bytes, file_table_cache[i].start_block);
        }
    }
    return 0;
}

int fs_get_file_info(const char *path, fs_file_entry_t *info) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted.");
        return -1;
    }
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) {
        return -1; // Not found
    }
    memcpy(info, &file_table_cache[entry_idx], sizeof(fs_file_entry_t));
    return 0;
}

int fs_get_file_info_by_index(int index, fs_file_entry_t *info) {
    if (!mounted_disk_node || !file_table_cache || index < 0 || index >= FS_MAX_FILES) {
        return -1;
    }
    if (file_table_cache[index].filename[0] == '\0') { // Check if entry is empty
        return -1;
    }
    memcpy(info, &file_table_cache[index], sizeof(fs_file_entry_t));
    return 0;
}

// Creates a new file or directory entry
int fs_create_file(const char *path, uint32_t flags) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted. (fs_create_file)");
        return -1;
    }
    if (find_file_entry(path) != -1) {
        klog(LOG_WARN, "FS_DISK: File/directory already exists: %s", path);
        return -1; // Already exists
    }

    int entry_idx = find_free_file_entry();
    if (entry_idx == -1) {
        klog(LOG_ERROR, "FS_DISK: No free file entries.");
        return -1;
    }

    fs_file_entry_t *entry = &file_table_cache[entry_idx];
    memset(entry, 0, sizeof(fs_file_entry_t));
    strncpy(entry->filename, path, FS_MAX_FILENAME_LEN - 1);
    entry->flags = flags;
    entry->size_bytes = 0; // New files are empty
    entry->start_block = 0; // No data blocks yet

    update_file_table();
    klog(LOG_INFO, "FS_DISK: Created new entry: %s with flags %u", path, flags);
    return 0;
}

// Deletes a file or directory entry
int fs_delete_file(const char *path) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted. (fs_delete_file)");
        return -1;
    }
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) {
        klog(LOG_WARN, "FS_DISK: File/directory not found for deletion: %s", path);
        return -1; // Not found
    }

    fs_file_entry_t *entry = &file_table_cache[entry_idx];

    // For simplicity, we just clear the entry.
    // In a real FS, we would also free up data blocks if start_block != 0.
    memset(entry, 0, sizeof(fs_file_entry_t));
    update_file_table();
    klog(LOG_INFO, "FS_DISK: Deleted entry: %s", path);
    return 0;
}
