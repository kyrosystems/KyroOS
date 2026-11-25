#include "fs_disk.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "ide.h"
#include "vfs.h"
#include "thread.h"

// Global state for the mounted filesystem
static vfs_node_t *mounted_disk_node = NULL;
static uint32_t mounted_partition_lba = 0;
static fs_superblock_t current_superblock;
static fs_file_entry_t *file_table_cache = NULL;
static uint8_t *bitmap_cache = NULL;

static int read_block(uint32_t lba, uint8_t *buffer) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted. (read_block)");
        return -1;
    }
    uint32_t absolute_lba = mounted_partition_lba + lba;
    ide_read_blocks_t rb = {
        .lba = absolute_lba,
        .count = 1,
        .buffer = buffer
    };
    if (mounted_disk_node->ioctl(mounted_disk_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to read block LBA %u from disk.", absolute_lba);
        return -1;
    }
    return 0;
}

static int write_block(uint32_t lba, const uint8_t *buffer) {
    if (!mounted_disk_node) {
        klog(LOG_ERROR, "FS_DISK: No filesystem mounted. (write_block)");
        return -1;
    }
    uint32_t absolute_lba = mounted_partition_lba + lba;
    ide_write_blocks_t wb = {
        .lba = absolute_lba,
        .count = 1,
        .buffer = (void*)buffer
    };
    if (mounted_disk_node->ioctl(mounted_disk_node, IDE_IOCTL_WRITE_BLOCKS, &wb) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to write block LBA %u to disk.", absolute_lba);
        return -1;
    }
    return 0;
}

static int update_superblock(void) {
    return write_block(0, (uint8_t*)&current_superblock);
}

static int update_file_table(void) {
    if (!file_table_cache) return -1;
    for (uint32_t i = 0; i < current_superblock.file_table_num_blocks; i++) {
        if (write_block(current_superblock.file_table_start_block + i,
                        ((uint8_t*)file_table_cache) + (i * FS_BLOCK_SIZE)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int flush_bitmap_block(uint32_t bitmap_blk_idx) {
    if (!bitmap_cache || bitmap_blk_idx >= current_superblock.block_bitmap_blocks) return -1;
    return write_block(current_superblock.block_bitmap_start + bitmap_blk_idx,
                       bitmap_cache + (bitmap_blk_idx * FS_BLOCK_SIZE));
}

static inline void bitmap_set(uint32_t block) {
    bitmap_cache[block / 8] |= (1 << (block % 8));
}

static inline void bitmap_clear(uint32_t block) {
    bitmap_cache[block / 8] &= ~(1 << (block % 8));
}

static inline int bitmap_test(uint32_t block) {
    return (bitmap_cache[block / 8] & (1 << (block % 8))) != 0;
}

static uint32_t allocate_block(void) {
    if (current_superblock.free_blocks == 0 || !bitmap_cache) {
        klog(LOG_ERROR, "FS_DISK: Out of free disk blocks!");
        return 0;
    }

    for (uint32_t b = current_superblock.data_start_block; b < current_superblock.total_blocks; b++) {
        if (!bitmap_test(b)) {
            bitmap_set(b);
            current_superblock.free_blocks--;
            uint32_t b_idx = b / (FS_BLOCK_SIZE * 8);
            flush_bitmap_block(b_idx);
            update_superblock();
            return b;
        }
    }

    klog(LOG_ERROR, "FS_DISK: Bitmap full, no free block found.");
    return 0;
}

static void free_block(uint32_t block) {
    if (block < current_superblock.data_start_block || block >= current_superblock.total_blocks || !bitmap_cache) {
        return;
    }
    if (bitmap_test(block)) {
        bitmap_clear(block);
        current_superblock.free_blocks++;
        uint32_t b_idx = block / (FS_BLOCK_SIZE * 8);
        flush_bitmap_block(b_idx);
        update_superblock();
    }
}

int fs_format(vfs_node_t *disk_device_node, uint32_t partition_lba, uint32_t partition_size) {
    klog(LOG_INFO, "FS_DISK: Formatting partition LBA %u, size %u", partition_lba, partition_size);

    fs_superblock_t superblock;
    memset(&superblock, 0, sizeof(fs_superblock_t));
    superblock.signature = FS_SIGNATURE;
    superblock.version = 2; // Version 2: Bitmap allocator & Inode table
    superblock.total_blocks = partition_size;

    superblock.block_bitmap_start = 1;
    superblock.block_bitmap_blocks = (partition_size + (FS_BLOCK_SIZE * 8) - 1) / (FS_BLOCK_SIZE * 8);

    superblock.file_table_start_block = superblock.block_bitmap_start + superblock.block_bitmap_blocks;
    superblock.file_table_num_blocks = (sizeof(fs_file_entry_t) * FS_MAX_FILES + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;

    superblock.data_start_block = superblock.file_table_start_block + superblock.file_table_num_blocks;
    if (superblock.data_start_block >= superblock.total_blocks) {
        klog(LOG_ERROR, "FS_DISK: Partition too small for metadata.");
        return -1;
    }
    superblock.free_blocks = superblock.total_blocks - superblock.data_start_block;

    // Write superblock
    ide_write_blocks_t wb = {.lba = partition_lba, .count = 1, .buffer = &superblock};
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to write superblock.");
        return -1;
    }

    // Allocate temporary bitmap to format disk
    size_t bmp_bytes = superblock.block_bitmap_blocks * FS_BLOCK_SIZE;
    uint8_t *temp_bmp = (uint8_t*)kmalloc(bmp_bytes);
    if (!temp_bmp) return -1;
    memset(temp_bmp, 0, bmp_bytes);

    // Mark metadata blocks (0 to data_start_block - 1) as allocated
    for (uint32_t i = 0; i < superblock.data_start_block; i++) {
        temp_bmp[i / 8] |= (1 << (i % 8));
    }

    // Write bitmap blocks
    for (uint32_t i = 0; i < superblock.block_bitmap_blocks; i++) {
        wb.lba = partition_lba + superblock.block_bitmap_start + i;
        wb.buffer = temp_bmp + (i * FS_BLOCK_SIZE);
        if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb) != 0) {
            kfree(temp_bmp);
            return -1;
        }
    }
    kfree(temp_bmp);

    // Clear file table / inode blocks
    uint8_t zero_block[FS_BLOCK_SIZE];
    memset(zero_block, 0, FS_BLOCK_SIZE);
    wb.buffer = zero_block;
    for (uint32_t i = 0; i < superblock.file_table_num_blocks; i++) {
        wb.lba = partition_lba + superblock.file_table_start_block + i;
        if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb) != 0) {
            return -1;
        }
    }

    klog(LOG_INFO, "FS_DISK: Formatted successfully with block bitmap. Free blocks: %u", superblock.free_blocks);
    return 0;
}

int fs_mount(vfs_node_t *disk_device_node, uint32_t partition_lba) {
    klog(LOG_INFO, "FS_DISK: Mounting filesystem on LBA %u...", partition_lba);

    fs_superblock_t superblock;
    ide_read_blocks_t rb = {.lba = partition_lba, .count = 1, .buffer = &superblock};
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        klog(LOG_ERROR, "FS_DISK: Failed to read superblock.");
        return -1;
    }

    if (superblock.signature != FS_SIGNATURE) {
        klog(LOG_ERROR, "FS_DISK: Invalid signature 0x%x.", superblock.signature);
        return -1;
    }

    if (file_table_cache) kfree(file_table_cache);
    if (bitmap_cache) kfree(bitmap_cache);

    file_table_cache = (fs_file_entry_t*)kmalloc(superblock.file_table_num_blocks * FS_BLOCK_SIZE);
    if (!file_table_cache) return -1;

    rb.lba = partition_lba + superblock.file_table_start_block;
    rb.count = superblock.file_table_num_blocks;
    rb.buffer = file_table_cache;
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        kfree(file_table_cache);
        file_table_cache = NULL;
        return -1;
    }

    bitmap_cache = (uint8_t*)kmalloc(superblock.block_bitmap_blocks * FS_BLOCK_SIZE);
    if (!bitmap_cache) {
        kfree(file_table_cache);
        file_table_cache = NULL;
        return -1;
    }

    rb.lba = partition_lba + superblock.block_bitmap_start;
    rb.count = superblock.block_bitmap_blocks;
    rb.buffer = bitmap_cache;
    if (disk_device_node->ioctl(disk_device_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        kfree(bitmap_cache);
        kfree(file_table_cache);
        bitmap_cache = NULL;
        file_table_cache = NULL;
        return -1;
    }

    mounted_disk_node = disk_device_node;
    mounted_partition_lba = partition_lba;
    current_superblock = superblock;

    klog(LOG_INFO, "FS_DISK: Mounted. Total blocks: %u, Free: %u (Bitmap allocator active)",
         current_superblock.total_blocks, current_superblock.free_blocks);
    return 0;
}

int fs_unmount(vfs_node_t *disk_device_node) {
    (void)disk_device_node;
    if (!mounted_disk_node) return 0;

    update_superblock();
    update_file_table();
    for (uint32_t i = 0; i < current_superblock.block_bitmap_blocks; i++) {
        flush_bitmap_block(i);
    }

    if (file_table_cache) { kfree(file_table_cache); file_table_cache = NULL; }
    if (bitmap_cache) { kfree(bitmap_cache); bitmap_cache = NULL; }
    mounted_disk_node = NULL;
    mounted_partition_lba = 0;
    klog(LOG_INFO, "FS_DISK: Unmounted.");
    return 0;
}

static int find_free_file_entry(void) {
    if (!file_table_cache) return -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (file_table_cache[i].filename[0] == '\0') return i;
    }
    return -1;
}

static int find_file_entry(const char *path) {
    if (!file_table_cache) return -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (file_table_cache[i].filename[0] != '\0' && strcmp(file_table_cache[i].filename, path) == 0) {
            return i;
        }
    }
    return -1;
}

static void free_entry_blocks(fs_file_entry_t *entry) {
    if (!entry) return;
    for (uint32_t i = 0; i < entry->block_count && i < FS_DIRECT_BLOCKS; i++) {
        if (entry->direct_blocks[i] != 0) {
            free_block(entry->direct_blocks[i]);
            entry->direct_blocks[i] = 0;
        }
    }
    if (entry->indirect_block != 0) {
        uint32_t indirect_ptrs[FS_BLOCK_SIZE / sizeof(uint32_t)];
        if (read_block(entry->indirect_block, (uint8_t*)indirect_ptrs) == 0) {
            uint32_t remaining = (entry->block_count > FS_DIRECT_BLOCKS) ? (entry->block_count - FS_DIRECT_BLOCKS) : 0;
            for (uint32_t i = 0; i < remaining && i < (FS_BLOCK_SIZE / sizeof(uint32_t)); i++) {
                if (indirect_ptrs[i] != 0) free_block(indirect_ptrs[i]);
            }
        }
        free_block(entry->indirect_block);
        entry->indirect_block = 0;
    }
    entry->block_count = 0;
}

int fs_write_file(const char *path, const uint8_t *data, size_t size) {
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) {
        klog(LOG_ERROR, "FS_DISK: File not found for writing: %s", path);
        return -1;
    }

    fs_file_entry_t *entry = &file_table_cache[entry_idx];
    if (!(entry->flags & FS_FILE_FLAG_FILE)) {
        return -1;
    }

    uint32_t needed_blocks = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    if (needed_blocks > current_superblock.free_blocks + entry->block_count) {
        klog(LOG_ERROR, "FS_DISK: Not enough free blocks for %s (need %u, free %u)",
             path, needed_blocks, current_superblock.free_blocks);
        return -1;
    }

    // Free previously allocated blocks so reallocation reuses freed blocks!
    free_entry_blocks(entry);

    entry->block_count = needed_blocks;
    entry->size_bytes = (uint32_t)size;

    uint32_t indirect_ptrs[FS_BLOCK_SIZE / sizeof(uint32_t)];
    memset(indirect_ptrs, 0, sizeof(indirect_ptrs));
    int has_indirect = 0;

    for (uint32_t i = 0; i < needed_blocks; i++) {
        uint32_t blk = allocate_block();
        if (blk == 0) {
            klog(LOG_ERROR, "FS_DISK: Failed to allocate block %u", i);
            return -1;
        }

        if (i < FS_DIRECT_BLOCKS) {
            entry->direct_blocks[i] = blk;
        } else {
            if (!has_indirect) {
                entry->indirect_block = allocate_block();
                has_indirect = 1;
            }
            indirect_ptrs[i - FS_DIRECT_BLOCKS] = blk;
        }

        uint8_t block_buf[FS_BLOCK_SIZE];
        memset(block_buf, 0, FS_BLOCK_SIZE);
        size_t offset = i * FS_BLOCK_SIZE;
        size_t chunk = (size - offset > FS_BLOCK_SIZE) ? FS_BLOCK_SIZE : (size - offset);
        memcpy(block_buf, data + offset, chunk);

        if (write_block(blk, block_buf) != 0) {
            klog(LOG_ERROR, "FS_DISK: Failed to write data block %u", blk);
            return -1;
        }
    }

    if (has_indirect) {
        write_block(entry->indirect_block, (uint8_t*)indirect_ptrs);
    }

    update_file_table();
    return (int)size;
}

int fs_read_file(const char *path, uint8_t *buffer, size_t size) {
    if (!mounted_disk_node) return -1;
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) return -1;

    fs_file_entry_t *entry = &file_table_cache[entry_idx];
    if (!(entry->flags & FS_FILE_FLAG_FILE)) return -1;

    size_t to_read = (size < entry->size_bytes) ? size : entry->size_bytes;
    uint32_t blocks_to_read = (to_read + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;

    uint32_t indirect_ptrs[FS_BLOCK_SIZE / sizeof(uint32_t)];
    int loaded_indirect = 0;

    for (uint32_t i = 0; i < blocks_to_read; i++) {
        uint32_t blk = 0;
        if (i < FS_DIRECT_BLOCKS) {
            blk = entry->direct_blocks[i];
        } else {
            if (!loaded_indirect && entry->indirect_block != 0) {
                read_block(entry->indirect_block, (uint8_t*)indirect_ptrs);
                loaded_indirect = 1;
            }
            blk = indirect_ptrs[i - FS_DIRECT_BLOCKS];
        }

        uint8_t block_buf[FS_BLOCK_SIZE];
        if (read_block(blk, block_buf) != 0) return -1;

        size_t offset = i * FS_BLOCK_SIZE;
        size_t chunk = (to_read - offset > FS_BLOCK_SIZE) ? FS_BLOCK_SIZE : (to_read - offset);
        memcpy(buffer + offset, block_buf, chunk);
    }

    return (int)to_read;
}

int fs_create_file(const char *path, uint32_t flags) {
    if (!mounted_disk_node) return -1;
    if (find_file_entry(path) != -1) return -1;

    int entry_idx = find_free_file_entry();
    if (entry_idx == -1) return -1;

    fs_file_entry_t *entry = &file_table_cache[entry_idx];
    memset(entry, 0, sizeof(fs_file_entry_t));
    strncpy(entry->filename, path, FS_MAX_FILENAME_LEN - 1);
    entry->flags = flags;
    entry->size_bytes = 0;
    entry->block_count = 0;

    update_file_table();
    return 0;
}

int fs_delete_file(const char *path) {
    if (!mounted_disk_node) return -1;
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) return -1;

    fs_file_entry_t *entry = &file_table_cache[entry_idx];

    // Truly free all allocated blocks via the block bitmap!
    free_entry_blocks(entry);

    memset(entry, 0, sizeof(fs_file_entry_t));
    update_file_table();
    klog(LOG_INFO, "FS_DISK: Deleted %s, reclaimed blocks. Free blocks now: %u",
         path, current_superblock.free_blocks);
    return 0;
}

int fs_list_dir(const char *path) {
    (void)path;
    if (!file_table_cache) return -1;
    klog(LOG_INFO, "FS_DISK: Listing files:");
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (file_table_cache[i].filename[0] != '\0') {
            klog(LOG_INFO, "  - %s (size: %u bytes, blocks: %u)",
                 file_table_cache[i].filename, file_table_cache[i].size_bytes, file_table_cache[i].block_count);
        }
    }
    return 0;
}

int fs_get_file_info(const char *path, fs_file_entry_t *info) {
    if (!mounted_disk_node || !info) return -1;
    int entry_idx = find_file_entry(path);
    if (entry_idx == -1) return -1;
    memcpy(info, &file_table_cache[entry_idx], sizeof(fs_file_entry_t));
    return 0;
}

int fs_get_file_info_by_index(int index, fs_file_entry_t *info) {
    if (!mounted_disk_node || !file_table_cache || !info || index < 0 || index >= FS_MAX_FILES) {
        return -1;
    }
    if (file_table_cache[index].filename[0] == '\0') return -1;
    memcpy(info, &file_table_cache[index], sizeof(fs_file_entry_t));
    return 0;
}
