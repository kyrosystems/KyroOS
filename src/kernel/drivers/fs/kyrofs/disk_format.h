#ifndef KYROFS_DISK_FORMAT_H
#define KYROFS_DISK_FORMAT_H

#include <stdint.h>
#include <stddef.h> // For size_t

// Use __attribute__((packed)) for all on-disk structures
#pragma pack(push, 1)

// --- Constants ---
#define KYROFS_MAGIC_VALUE      0x4B59524F // "KYRO"
#define KYROFS_VERSION_MAJOR    1
#define KYROFS_VERSION_MINOR    0
#define KYROFS_DEFAULT_BLOCK_SIZE_BYTES 4096 // 4KB blocks (must be a multiple of 512 for disk I/O)
#define KYROFS_MAX_EXTENTS_PER_INODE    10   // Direct extents in inode
#define KYROFS_INODE_SIZE_BYTES         256  // Fixed size for each inode entry
#define KYROFS_SUPERBLOCK_SIZE_BYTES    512  // Superblock fits in one 512-byte sector
#define KYROFS_BGDT_SIZE_BYTES          512  // Block Group Descriptor Table entry fits in one 512-byte sector
#define KYROFS_JOURNAL_ENTRY_SIZE_BYTES 512  // Each journal entry fits in one 512-byte sector

// --- Extent Structure ---
// Represents a contiguous range of blocks
typedef struct {
    uint64_t logical_start;   // Logical block start within the file (in filesystem blocks)
    uint64_t physical_start;  // Physical block start on disk (in 512-byte sectors)
    uint32_t block_count;     // Number of contiguous blocks in this extent (in filesystem blocks)
    // Total size: 8 + 8 + 4 = 20 bytes.
} kyrofs_extent_t; // Total 20 bytes

// --- Inode Structure ---
typedef struct {
    uint16_t mode;          // File type and permissions (POSIX-like)
    uint16_t uid;           // User ID
    uint16_t gid;           // Group ID
    uint32_t links_count;   // Number of hard links
    uint64_t size;          // File size in bytes
    uint64_t atime;         // Last access time (nanoseconds since epoch)
    uint64_t mtime;         // Last modification time (nanoseconds since epoch)
    uint64_t ctime;         // Creation time (nanoseconds since epoch)
    uint32_t flags;         // Inode-specific flags (e.g., compressed, immutable)
    uint32_t blocks_count;  // Number of 512-byte disk sectors actually used by file

    kyrofs_extent_t extents[KYROFS_MAX_EXTENTS_PER_INODE]; // 10 direct extents * 20 bytes = 200 bytes
    // Fixed part: (uint16_t*3 + uint32_t*2 + uint64_t*4 + uint32_t*2) = 6 + 8 + 32 + 8 = 54 bytes.
    // Fixed part breakdown:
    // mode, uid, gid: 2*3 = 6 bytes
    // links_count, flags, blocks_count: 4*3 = 12 bytes
    // size, atime, mtime, ctime: 8*4 = 32 bytes
    // Total fixed part: 6 + 12 + 32 = 50 bytes.

    uint32_t checksum;      // CRC32 checksum of the inode
    uint8_t  padding[KYROFS_INODE_SIZE_BYTES - (50 + sizeof(kyrofs_extent_t)*KYROFS_MAX_EXTENTS_PER_INODE + sizeof(uint32_t))];
    // Padding: 256 - (50 + 200 + 4) = 256 - 254 = 2 bytes.
    // Total size = 256 bytes.
} kyrofs_inode_t;

// --- Superblock Structure ---
typedef struct {
    uint32_t magic;                 // KYROFS_MAGIC_VALUE
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t block_size;            // Size of a data block in bytes (e.g., 4096)
    uint64_t total_blocks;          // Total blocks in the filesystem (in block_size units)
    uint64_t free_blocks_count;     // Count of free blocks
    uint32_t total_inodes;          // Total number of inodes
    uint32_t free_inodes_count;     // Count of free inodes
    uint8_t  uuid[16];              // Unique filesystem ID
    uint64_t last_mount_time;       // Last mount time (nanoseconds)
    uint64_t last_write_time;       // Last write time (nanoseconds)
    uint32_t block_group_count;     // Number of block groups
    uint64_t first_data_block;      // LBA of the first *KyroFS block* (after metadata)

    uint64_t block_group_desc_table_start_block; // LBA of Block Group Descriptor Table (in 512-byte sectors)
    uint32_t blocks_per_group;      // Number of KyroFS blocks per group
    uint32_t inodes_per_group;      // Number of inodes per group

    uint64_t journal_start_block;   // LBA of the journal start (in 512-byte sectors)
    uint64_t journal_size_blocks;   // Size of the journal (in 512-byte sectors)
    uint64_t journal_head_block;    // Head of the circular journal buffer (in 512-byte sectors)
    uint32_t journal_sequence_num;  // Current sequence number for journal entries

    uint32_t checksum;              // CRC32 checksum for superblock integrity
    uint8_t  padding[KYROFS_SUPERBLOCK_SIZE_BYTES - (
        sizeof(uint32_t) * 9 +      // magic, block_size, total_inodes, free_inodes_count, block_group_count, blocks_per_group, inodes_per_group, journal_sequence_num, checksum
        sizeof(uint16_t) * 2 +      // version_major, version_minor
        sizeof(uint64_t) * 9 +      // total_blocks, free_blocks_count, last_mount_time, last_write_time, first_data_block, block_group_desc_table_start_block, journal_start_block, journal_size_blocks, journal_head_block
        sizeof(uint8_t) * 16        // uuid
    )];
    // Total used (calculated): 36 + 4 + 72 + 16 = 128 bytes.
    // Padding needed: 512 - 128 = 384 bytes.
} kyrofs_superblock_t;

// --- Block Group Descriptor (per block group) ---
typedef struct {
    uint64_t block_bitmap_block;    // LBA of block allocation bitmap (in 512-byte sectors)
    uint64_t inode_bitmap_block;    // LBA of inode allocation bitmap (in 512-byte sectors)
    uint64_t inode_table_start_block; // LBA of inode table for this group (in 512-byte sectors)
    uint32_t free_blocks_count;     // Free KyroFS blocks in this group
    uint32_t free_inodes_count;     // Free inodes in this group
    uint32_t used_dirs_count;       // Count of directories in this group
    uint32_t checksum;              // CRC32 checksum for descriptor
    uint8_t  padding[KYROFS_BGDT_SIZE_BYTES - (sizeof(uint64_t)*3 + sizeof(uint32_t)*4)];
    // Total used: 24 + 16 = 40 bytes.
    // Padding needed: 512 - 40 = 472 bytes.
} kyrofs_block_group_descriptor_t;

// --- Journal Entry (simplified) ---
// For metadata changes. Each entry could describe a single operation.
// This is structured to fit exactly one 512-byte sector.
typedef struct {
    uint32_t sequence_num;          // Sequence number of this journal entry
    uint32_t type;                  // E.g., KYROFS_JOURNAL_TYPE_INODE_UPDATE, KYROFS_JOURNAL_TYPE_BLOCK_ALLOC
    uint64_t affected_lba;          // Disk LBA of the affected metadata (in 512-byte sectors)
    uint32_t affected_size_sectors; // Size of the affected metadata (in 512-byte sectors)
    uint32_t checksum;              // Checksum of the journal entry data
    uint8_t  data[KYROFS_JOURNAL_ENTRY_SIZE_BYTES - (sizeof(uint32_t)*3 + sizeof(uint64_t))];
    // Data size: 512 - (12 + 8) = 512 - 20 = 492 bytes.
    // This allows a single journal entry to contain a full 492 bytes of data (e.g. part of an inode or block bitmap)
} kyrofs_journal_entry_t;

// Journal entry types
#define KYROFS_JOURNAL_TYPE_INODE_UPDATE    1   // Journal entry contains an updated inode
#define KYROFS_JOURNAL_TYPE_BLOCK_BITMAP    2   // Journal entry contains a part of the block bitmap
#define KYROFS_JOURNAL_TYPE_INODE_BITMAP    3   // Journal entry contains a part of the inode bitmap
#define KYROFS_JOURNAL_TYPE_SUPERBLOCK      4   // Journal entry contains an updated superblock
#define KYROFS_JOURNAL_TYPE_BGDT_UPDATE     5   // Journal entry contains an updated BGD table entry

#pragma pack(pop)

#endif // KYROFS_DISK_FORMAT_H
