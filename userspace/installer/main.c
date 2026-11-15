#include "kyroolib.h"
#include "limine-bios-hdd.h"

#define SECTOR_SIZE 512
#define CLUSTER_SECTORS 8
#define CLUSTER_SIZE (CLUSTER_SECTORS * SECTOR_SIZE) // 4096 bytes
#define RESERVED_SECTORS 32

// ============================================================================
// KyroFS Filesystem Definitions (Native KyroOS Filesystem)
// ============================================================================
#define KYROFS_MAGIC            0x4B59524F46533032ULL // "KYROFS02"
#define KYROFS_BLOCK_SIZE       4096
#define KYROFS_BLOCKS_PER_GROUP 8192
#define KYROFS_ROOT_INODE       1
#define KYROFS_JOURNAL_SLOTS    64
#define KYROFS_MAX_EXTENTS      12
#define KYROFS_MODE_FILE        0x8000
#define KYROFS_MODE_DIR         0x4000

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

static uint32_t kyrofs_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1) - 1)));
    }
    return ~crc;
}

// ============================================================================
// MBR Partition Table Entry (16 bytes)
// ============================================================================
typedef struct __attribute__((packed)) {
    uint8_t  boot_indicator; // 0x80 = bootable, 0x00 = not bootable
    uint8_t  start_head;
    uint8_t  start_sector;
    uint8_t  start_cylinder;
    uint8_t  partition_type; // 0x0C = FAT32 LBA / 0x83 = Linux / KyroFS native
    uint8_t  end_head;
    uint8_t  end_sector;
    uint8_t  end_cylinder;
    uint32_t start_lba;
    uint32_t sector_count;
} mbr_partition_t;

// Standard 8.3 FAT directory entry (32 bytes)
typedef struct __attribute__((packed)) {
    char     name[11];      // 8 name, 3 extension
    uint8_t  attr;          // 0x10 = directory, 0x20 = archive/file
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat_dirent_t;

// Standard VFAT LFN directory entry (32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  seq_num;       // 1, 2, ... | 0x40 for last
    uint16_t name1[5];      // 5 UTF-16LE characters
    uint8_t  attr;          // 0x0F (read-only, hidden, system, volid)
    uint8_t  type;          // 0
    uint8_t  checksum;      // Checksum of 8.3 name
    uint16_t name2[6];      // 6 UTF-16LE characters
    uint16_t fst_clus_lo;   // 0
    uint16_t name3[2];      // 2 UTF-16LE characters
} fat_lfn_dirent_t;

// Global Disk and Boot Partition State
static int g_dfd = -1;
static uint32_t g_part1_lba = 2048;
static uint32_t g_part1_sectors = 131072; // 64 MB
static uint32_t g_part2_lba = 133120;
static uint32_t g_part2_sectors = 0;

static uint32_t g_sectors_per_fat = 1024;
static uint32_t g_fat1_lba = 0;
static uint32_t g_fat2_lba = 0;
static uint32_t g_data_lba = 0;
static uint32_t g_next_cluster = 3;

#define MAX_FAT_CACHE_ENTRIES 32768
static uint32_t g_fat_table[MAX_FAT_CACHE_ENTRIES];

#define MAX_DIR_ENTRIES 128
typedef struct {
    uint32_t cluster;
    int entry_count;
    uint8_t data[CLUSTER_SIZE];
} fat_dir_t;

static fat_dir_t g_dir_root;
static fat_dir_t g_dir_boot;
static fat_dir_t g_dir_limine;
static fat_dir_t g_dir_efi;
static fat_dir_t g_dir_efi_boot;
static fat_dir_t g_dir_bin;
static fat_dir_t g_dir_etc;
static fat_dir_t g_dir_lib;
static fat_dir_t g_dir_modules;
static fat_dir_t g_dir_home;
static fat_dir_t g_dir_root_user;
static fat_dir_t g_dir_tmp;
static fat_dir_t g_dir_var;
static fat_dir_t g_dir_dev;
static fat_dir_t g_dir_proc;

static void read_input(char *buf, int max) {
    int idx = 0;
    while (idx < max - 1) {
        char ch = 0;
        int n = read(0, &ch, 1);
        if (n <= 0) continue;
        if (ch == '\r' || ch == '\n') {
            print("\n");
            print_flush();
            break;
        } else if (ch == '\b' || ch == 127) {
            if (idx > 0) {
                idx--;
                print("\b \b");
                print_flush();
            }
        } else {
            buf[idx++] = ch;
            char echo[2] = {ch, '\0'};
            print(echo);
            print_flush();
        }
    }
    buf[idx] = '\0';
}

static uint8_t calc_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (((sum & 1) << 7) | ((sum & 0xFE) >> 1)) + (uint8_t)short_name[i];
    }
    return sum;
}

static void make_short_name(const char *long_name, char *short_name) {
    memset(short_name, ' ', 11);
    const char *dot = strrchr(long_name, '.');
    int name_len = dot ? (int)(dot - long_name) : (int)strlen(long_name);

    int j = 0;
    for (int i = 0; i < name_len && j < 8; i++) {
        char c = long_name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            short_name[j++] = c;
        }
    }

    if (dot) {
        j = 8;
        for (int i = 1; dot[i] && j < 11; i++) {
            char c = dot[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
                short_name[j++] = c;
            }
        }
    }
}

static void dir_add_entry(fat_dir_t *dir, const char *name, uint8_t attr, uint32_t cluster, uint32_t size) {
    char short_name[11];
    make_short_name(name, short_name);
    uint8_t chk = calc_checksum(short_name);

    int name_len = strlen(name);
    int lfn_entries = (name_len + 12) / 13;

    for (int l = lfn_entries; l >= 1; l--) {
        fat_lfn_dirent_t lfn;
        memset(&lfn, 0xFF, sizeof(lfn));
        lfn.seq_num = (uint8_t)l | (l == lfn_entries ? 0x40 : 0x00);
        lfn.attr = 0x0F;
        lfn.type = 0;
        lfn.checksum = chk;
        lfn.fst_clus_lo = 0;

        int base = (l - 1) * 13;
        for (int i = 0; i < 5; i++) {
            int char_idx = base + i;
            if (char_idx < name_len) lfn.name1[i] = (uint8_t)name[char_idx];
            else if (char_idx == name_len) lfn.name1[i] = 0;
            else lfn.name1[i] = 0xFFFF;
        }
        for (int i = 0; i < 6; i++) {
            int char_idx = base + 5 + i;
            if (char_idx < name_len) lfn.name2[i] = (uint8_t)name[char_idx];
            else if (char_idx == name_len) lfn.name2[i] = 0;
            else lfn.name2[i] = 0xFFFF;
        }
        for (int i = 0; i < 2; i++) {
            int char_idx = base + 11 + i;
            if (char_idx < name_len) lfn.name3[i] = (uint8_t)name[char_idx];
            else if (char_idx == name_len) lfn.name3[i] = 0;
            else lfn.name3[i] = 0xFFFF;
        }

        int off = dir->entry_count * 32;
        if (off + 32 <= CLUSTER_SIZE) {
            memcpy(dir->data + off, &lfn, 32);
            dir->entry_count++;
        }
    }

    fat_dirent_t de;
    memset(&de, 0, sizeof(de));
    memcpy(de.name, short_name, 11);
    de.attr = attr;
    de.fst_clus_hi = (uint16_t)(cluster >> 16);
    de.fst_clus_lo = (uint16_t)(cluster & 0xFFFF);
    de.file_size = size;

    int off = dir->entry_count * 32;
    if (off + 32 <= CLUSTER_SIZE) {
        memcpy(dir->data + off, &de, 32);
        dir->entry_count++;
    }
}

static void init_fat_dir(fat_dir_t *dir, uint32_t cluster, uint32_t parent_cluster) {
    dir->cluster = cluster;
    dir->entry_count = 0;
    memset(dir->data, 0, CLUSTER_SIZE);

    if (cluster != 2) {
        fat_dirent_t dot;
        memset(&dot, 0, sizeof(dot));
        memset(dot.name, ' ', 11);
        dot.name[0] = '.';
        dot.attr = 0x10;
        dot.fst_clus_hi = (uint16_t)(cluster >> 16);
        dot.fst_clus_lo = (uint16_t)(cluster & 0xFFFF);
        memcpy(dir->data, &dot, 32);
        dir->entry_count++;

        fat_dirent_t dotdot;
        memset(&dotdot, 0, sizeof(dotdot));
        memset(dotdot.name, ' ', 11);
        dotdot.name[0] = '.'; dotdot.name[1] = '.';
        dotdot.attr = 0x10;
        dotdot.fst_clus_hi = (uint16_t)(parent_cluster >> 16);
        dotdot.fst_clus_lo = (uint16_t)(parent_cluster & 0xFFFF);
        memcpy(dir->data + 32, &dotdot, 32);
        dir->entry_count++;
    }
}

static uint32_t fat_write_file_data(const void *data, uint32_t size) {
    if (size == 0) return 0;
    uint32_t num_clusters = (size + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
    uint32_t start_cluster = g_next_cluster;

    for (uint32_t i = 0; i < num_clusters; i++) {
        uint32_t cur = start_cluster + i;
        g_fat_table[cur] = (i == num_clusters - 1) ? 0x0FFFFFFF : (cur + 1);

        uint32_t lba = g_data_lba + (cur - 2) * CLUSTER_SECTORS;
        lseek(g_dfd, (int64_t)lba * SECTOR_SIZE, SEEK_SET);

        uint8_t clus_buf[CLUSTER_SIZE];
        memset(clus_buf, 0, sizeof(clus_buf));

        uint32_t off = i * CLUSTER_SIZE;
        uint32_t chunk = (size > off) ? (size - off) : 0;
        if (chunk > CLUSTER_SIZE) chunk = CLUSTER_SIZE;
        if (chunk > 0) {
            memcpy(clus_buf, (const uint8_t *)data + off, chunk);
        }
        write(g_dfd, clus_buf, CLUSTER_SIZE);
    }

    g_next_cluster += num_clusters;
    return start_cluster;
}

static int install_file_to_fat(const char *src_path, fat_dir_t *parent_dir, const char *dst_name) {
    int sfd = open(src_path, O_RDONLY);
    if (sfd < 0 && strncmp(src_path, "/boot/", 6) == 0) {
        char alt1[128];
        snprintf(alt1, sizeof(alt1), "/%s", src_path + 6);
        sfd = open(alt1, O_RDONLY);
    }
    if (sfd < 0) {
        char alt2[128];
        snprintf(alt2, sizeof(alt2), "/limine/%s", dst_name);
        sfd = open(alt2, O_RDONLY);
    }
    if (sfd < 0) {
        char alt3[128];
        snprintf(alt3, sizeof(alt3), "/%s", dst_name);
        sfd = open(alt3, O_RDONLY);
    }
    if (sfd < 0) {
        return -1;
    }

    struct stat st;
    uint32_t size = 0;
    if (fstat(sfd, &st) == 0) {
        size = st.st_size;
    }

    void *buf = malloc(size > 0 ? size : 4);
    if (!buf) {
        close(sfd);
        return -1;
    }

    if (size > 0) {
        read(sfd, buf, size);
    }
    close(sfd);

    uint32_t clus = fat_write_file_data(buf, size);
    free(buf);

    dir_add_entry(parent_dir, dst_name, 0x20, clus, size);
    return 0;
}

static int install_buffer_to_fat(const void *data, uint32_t size, fat_dir_t *parent_dir, const char *dst_name) {
    uint32_t clus = fat_write_file_data(data, size);
    dir_add_entry(parent_dir, dst_name, 0x20, clus, size);
    return 0;
}

static void flush_fat_dir(fat_dir_t *dir) {
    uint32_t lba = g_data_lba + (dir->cluster - 2) * CLUSTER_SECTORS;
    lseek(g_dfd, (int64_t)lba * SECTOR_SIZE, SEEK_SET);
    write(g_dfd, dir->data, CLUSTER_SIZE);
}

// ============================================================================
// KyroFS Formatter (Native Filesystem Formatter on /dev/sda2)
// ============================================================================
static int format_kyrofs_partition(int fd, uint32_t part2_lba, uint32_t part2_sectors) {
    uint64_t total_blocks = part2_sectors / (KYROFS_BLOCK_SIZE / SECTOR_SIZE);
    if (total_blocks < 64) total_blocks = 64;

    uint32_t group_count = (uint32_t)((total_blocks + KYROFS_BLOCKS_PER_GROUP - 1) / KYROFS_BLOCKS_PER_GROUP);
    if (group_count == 0) group_count = 1;

    uint64_t total_inodes = total_blocks / 4;
    if (total_inodes < 64) total_inodes = 64;
    uint32_t inodes_per_group = (uint32_t)(total_inodes / group_count);

    kyrofs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = KYROFS_MAGIC;
    sb.version = 2;
    sb.block_size = KYROFS_BLOCK_SIZE;
    sb.total_blocks = total_blocks;
    sb.blocks_per_group = KYROFS_BLOCKS_PER_GROUP;
    sb.group_count = group_count;
    sb.total_inodes = total_inodes;
    sb.inodes_per_group = inodes_per_group;
    sb.root_inode = KYROFS_ROOT_INODE;
    sb.journal_blocks = KYROFS_JOURNAL_SLOTS * 2; // 128 blocks
    sb.journal_seq = 0;
    sb.clean_unmount = 1;

    uint64_t cursor = 1;
    sb.group_desc_block = cursor++;
    sb.journal_start = cursor;
    cursor += sb.journal_blocks;

    kyrofs_group_desc_t *groups = (kyrofs_group_desc_t *)malloc(sizeof(kyrofs_group_desc_t) * group_count);
    if (!groups) return -1;
    memset(groups, 0, sizeof(kyrofs_group_desc_t) * group_count);

    uint32_t per_inode_block = KYROFS_BLOCK_SIZE / sizeof(kyrofs_inode_t);
    uint64_t inode_table_blocks_per_group = (inodes_per_group + per_inode_block - 1) / per_inode_block;

    uint64_t blocks_left = total_blocks - cursor;
    for (uint32_t g = 0; g < group_count; g++) {
        uint64_t group_span = sb.blocks_per_group;
        if (group_span > blocks_left) group_span = blocks_left;
        groups[g].block_bitmap_block = cursor++;
        groups[g].inode_bitmap_block = cursor++;
        groups[g].inode_table_block = cursor;
        cursor += inode_table_blocks_per_group;
        groups[g].data_block_start = cursor;

        uint64_t meta_blocks = 2 + inode_table_blocks_per_group;
        uint64_t data_blocks = (group_span > meta_blocks) ? (group_span - meta_blocks) : 0;
        groups[g].group_blocks = (uint32_t)data_blocks;
        groups[g].group_inodes = inodes_per_group;
        groups[g].free_blocks_in_group = (uint32_t)data_blocks;
        groups[g].free_inodes_in_group = inodes_per_group;
        cursor += data_blocks;
        blocks_left = (total_blocks > cursor) ? (total_blocks - cursor) : 0;
    }

    sb.free_blocks = 0;
    sb.free_inodes = sb.total_inodes;
    for (uint32_t g = 0; g < group_count; g++) sb.free_blocks += groups[g].free_blocks_in_group;

    uint8_t zero_block[KYROFS_BLOCK_SIZE];
    memset(zero_block, 0, sizeof(zero_block));

    // Zero out metadata and bitmaps on disk
    for (uint32_t g = 0; g < group_count; g++) {
        int64_t off1 = ((int64_t)part2_lba + groups[g].block_bitmap_block * 8) * SECTOR_SIZE;
        lseek(fd, off1, SEEK_SET);
        write(fd, zero_block, KYROFS_BLOCK_SIZE);

        int64_t off2 = ((int64_t)part2_lba + groups[g].inode_bitmap_block * 8) * SECTOR_SIZE;
        lseek(fd, off2, SEEK_SET);
        write(fd, zero_block, KYROFS_BLOCK_SIZE);

        for (uint64_t i = 0; i < inode_table_blocks_per_group; i++) {
            int64_t off3 = ((int64_t)part2_lba + (groups[g].inode_table_block + i) * 8) * SECTOR_SIZE;
            lseek(fd, off3, SEEK_SET);
            write(fd, zero_block, KYROFS_BLOCK_SIZE);
        }
    }

    // Zero out journal blocks
    for (uint64_t i = 0; i < sb.journal_blocks; i++) {
        int64_t j_off = ((int64_t)part2_lba + (sb.journal_start + i) * 8) * SECTOR_SIZE;
        lseek(fd, j_off, SEEK_SET);
        write(fd, zero_block, KYROFS_BLOCK_SIZE);
    }

    // Allocate root inode (bit 0 in inode bitmap)
    uint8_t inode_bm[KYROFS_BLOCK_SIZE];
    memset(inode_bm, 0, sizeof(inode_bm));
    inode_bm[0] |= 1; // Bit 0
    lseek(fd, ((int64_t)part2_lba + groups[0].inode_bitmap_block * 8) * SECTOR_SIZE, SEEK_SET);
    write(fd, inode_bm, KYROFS_BLOCK_SIZE);
    groups[0].free_inodes_in_group--;
    sb.free_inodes--;

    // Allocate root data block (bit 0 in block bitmap)
    uint8_t block_bm[KYROFS_BLOCK_SIZE];
    memset(block_bm, 0, sizeof(block_bm));
    block_bm[0] |= 1; // Bit 0
    lseek(fd, ((int64_t)part2_lba + groups[0].block_bitmap_block * 8) * SECTOR_SIZE, SEEK_SET);
    write(fd, block_bm, KYROFS_BLOCK_SIZE);
    groups[0].free_blocks_in_group--;
    sb.free_blocks--;

    uint64_t root_data_blk = groups[0].data_block_start;

    // Write root directory entries: "." and ".."
    uint8_t dir_buf[KYROFS_BLOCK_SIZE];
    memset(dir_buf, 0, sizeof(dir_buf));
    kyrofs_dirent_t *de1 = (kyrofs_dirent_t *)dir_buf;
    de1->inode = 1;
    de1->rec_len = sizeof(kyrofs_dirent_t);
    de1->name_len = 1;
    de1->file_type = 2; // dir
    strcpy(de1->name, ".");

    kyrofs_dirent_t *de2 = (kyrofs_dirent_t *)(dir_buf + sizeof(kyrofs_dirent_t));
    de2->inode = 1;
    de2->rec_len = sizeof(kyrofs_dirent_t);
    de2->name_len = 2;
    de2->file_type = 2; // dir
    strcpy(de2->name, "..");

    lseek(fd, ((int64_t)part2_lba + root_data_blk * 8) * SECTOR_SIZE, SEEK_SET);
    write(fd, dir_buf, KYROFS_BLOCK_SIZE);

    // Write root inode in Inode Table
    uint8_t inotbl_buf[KYROFS_BLOCK_SIZE];
    memset(inotbl_buf, 0, sizeof(inotbl_buf));
    kyrofs_inode_t *r_ino = (kyrofs_inode_t *)inotbl_buf;
    r_ino->mode = KYROFS_MODE_DIR | 0755;
    r_ino->uid = 0;
    r_ino->gid = 0;
    r_ino->size = sizeof(kyrofs_dirent_t) * 2;
    r_ino->blocks = 1;
    r_ino->atime = 1773000000ULL;
    r_ino->mtime = 1773000000ULL;
    r_ino->ctime = 1773000000ULL;
    r_ino->nlinks = 2;
    r_ino->extent_count = 1;
    r_ino->extents[0].start_block = root_data_blk;
    r_ino->extents[0].length = 1;

    lseek(fd, ((int64_t)part2_lba + groups[0].inode_table_block * 8) * SECTOR_SIZE, SEEK_SET);
    write(fd, inotbl_buf, KYROFS_BLOCK_SIZE);

    // Write group descriptors
    uint8_t gd_buf[KYROFS_BLOCK_SIZE];
    memset(gd_buf, 0, sizeof(gd_buf));
    memcpy(gd_buf, groups, sizeof(kyrofs_group_desc_t) * group_count);
    lseek(fd, ((int64_t)part2_lba + sb.group_desc_block * 8) * SECTOR_SIZE, SEEK_SET);
    write(fd, gd_buf, KYROFS_BLOCK_SIZE);

    // Compute Superblock CRC32 and write Superblock to block 0
    uint8_t sb_buf[KYROFS_BLOCK_SIZE];
    memset(sb_buf, 0, sizeof(sb_buf));
    sb.crc32 = 0;
    sb.crc32 = kyrofs_crc32(&sb, sizeof(sb));
    memcpy(sb_buf, &sb, sizeof(sb));
    lseek(fd, (int64_t)part2_lba * SECTOR_SIZE, SEEK_SET);
    write(fd, sb_buf, KYROFS_BLOCK_SIZE);

    free(groups);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: installer\n");
        print("KyroOS System Installation Wizard for partitioning, KyroFS formatting, and Limine setup.\n");
        print_flush();
        return 0;
    }

    print("\033[2J\033[H");
    print("\033[1;36m============================================================\033[0m\n");
    print("\033[1;32m         KyroOS System Installation Wizard v3.0            \033[0m\n");
    print("\033[1;36m============================================================\033[0m\n\n");
    print("Welcome! This installer will partition your drive, install\n");
    print("the native KyroFS filesystem, copy system binaries, and configure\n");
    print("the Limine multi-boot loader.\n\n");
    print_flush();

    // Scan for available target drive
    const char *candidate_drives[] = {"/dev/sda", "/dev/sdb", "/dev/hda", NULL};
    const char *target_drive = NULL;
    uint64_t target_size = 0;

    for (int i = 0; candidate_drives[i]; i++) {
        struct stat st;
        if (stat(candidate_drives[i], &st) == 0) {
            target_drive = candidate_drives[i];
            target_size = st.st_size;
            break;
        }
    }

    if (!target_drive) {
        print("\033[1;31mError: No valid block device detected in /dev!\033[0m\n");
        print("Please check that your hard disk controller is attached.\n");
        print_flush();
        return 1;
    }

    uint64_t size_mb = target_size / (1024 * 1024);
    if (size_mb == 0) size_mb = 4095; // Default 4GB estimate if size unbounded

    print("Detected primary storage drive:\n");
    printf("  Device: \033[1;33m%s\033[0m (~%u MB)\n\n", target_drive, (uint32_t)size_mb);
    printf("Do you want to install KyroOS to \033[1;33m%s\033[0m? [Y/n]: ", target_drive);
    print_flush();

    char ans[16];
    read_input(ans, sizeof(ans));
    if (ans[0] == 'n' || ans[0] == 'N') {
        print("Installation canceled by user.\n");
        print_flush();
        return 0;
    }

    printf("\n\033[1;31m[WARNING] All existing partitions on %s will be ERASED!\033[0m\n", target_drive);
    print("Are you sure you want to proceed? [yes/NO]: ");
    print_flush();

    read_input(ans, sizeof(ans));
    if (strcmp(ans, "yes") != 0 && strcmp(ans, "y") != 0 && strcmp(ans, "YES") != 0) {
        print("Aborted. No changes were made.\n");
        print_flush();
        return 0;
    }

    g_dfd = open(target_drive, O_RDWR);
    if (g_dfd < 0) {
        printf("\033[1;31mError: Cannot open %s for writing!\033[0m\n", target_drive);
        print_flush();
        return 1;
    }

    uint32_t total_sectors = (uint32_t)(target_size / SECTOR_SIZE);
    if (total_sectors <= 133120) total_sectors = 8388608; // 4GB fallback

    g_part1_lba = 2048;
    g_part1_sectors = 131072; // 64 MB Bootloader ESP/BIOS partition
    g_part2_lba = g_part1_lba + g_part1_sectors; // LBA 133120
    g_part2_sectors = total_sectors - g_part2_lba;

    print("\n\033[1;34m[1/5]\033[0m Writing Master Boot Record (MBR) and Partition Table...\n");
    print_flush();

    // 1. Write Limine Stage 1 MBR to Sector 0
    uint8_t mbr_sec[SECTOR_SIZE];
    memset(mbr_sec, 0, sizeof(mbr_sec));
    memcpy(mbr_sec, binary_limine_hdd_bin_data, 512);

    // Hardcode Stage 2 byte location at 0x1A4 (512 = LBA 1)
    uint64_t stage2_loc = 512;
    memcpy(mbr_sec + 0x1A4, &stage2_loc, sizeof(uint64_t));

    // Partition 1: Boot Partition (64MB, Active Bootable, Type 0x0C)
    mbr_partition_t *part1 = (mbr_partition_t *)(mbr_sec + 446);
    part1->boot_indicator = 0x80; // Active bootable
    part1->start_head = 1;
    part1->start_sector = 1;
    part1->start_cylinder = 0;
    part1->partition_type = 0x0C; // FAT32 LBA Boot partition
    part1->end_head = 254;
    part1->end_sector = 63;
    part1->end_cylinder = 15;
    part1->start_lba = g_part1_lba;
    part1->sector_count = g_part1_sectors;

    // Partition 2: KyroFS System Partition (Type 0x83 = Linux / KyroFS native)
    mbr_partition_t *part2 = (mbr_partition_t *)(mbr_sec + 446 + 16);
    part2->boot_indicator = 0x00;
    part2->start_head = 0;
    part2->start_sector = 1;
    part2->start_cylinder = 16;
    part2->partition_type = 0x83; // Linux / KyroFS Native
    part2->end_head = 254;
    part2->end_sector = 63;
    part2->end_cylinder = 255;
    part2->start_lba = g_part2_lba;
    part2->sector_count = g_part2_sectors;

    // Standard MBR Boot Signature
    mbr_sec[510] = 0x55;
    mbr_sec[511] = 0xAA;

    lseek(g_dfd, 0, SEEK_SET);
    write(g_dfd, mbr_sec, SECTOR_SIZE);

    // 2. Write Limine Stage 2 into post-MBR gap (Sectors 1..44)
    size_t stage2_size = sizeof(binary_limine_hdd_bin_data) - 512;
    uint32_t stage2_sectors = (uint32_t)((stage2_size + 511) / 512);
    uint8_t *stage2_buf = (uint8_t *)calloc(stage2_sectors, 512);
    if (!stage2_buf) {
        printf("\033[1;31mError: Out of memory allocating stage2 buffer!\033[0m\n");
        return 1;
    }
    memcpy(stage2_buf, &binary_limine_hdd_bin_data[512], stage2_size);
    lseek(g_dfd, 512, SEEK_SET);
    write(g_dfd, stage2_buf, stage2_sectors * 512);
    free(stage2_buf);

    print("\033[1;32m      Limine MBR Stage 1 and Stage 2 successfully written to drive.\033[0m\n");
    printf("\033[1;32m      Partition 1 (/dev/sda1): Bootloader ESP/BIOS (64 MB)\033[0m\n");
    printf("\033[1;32m      Partition 2 (/dev/sda2): KyroFS System Partition (~%u MB, Type 0x83)\033[0m\n",
           (uint32_t)(g_part2_sectors / 2048));
    print_flush();

    // ------------------------------------------------------------------------
    // Step 2: Format Partition 2 with native KyroFS (KYROFS02)
    // ------------------------------------------------------------------------
    print("\033[1;34m[2/5]\033[0m Formatting KyroFS filesystem on /dev/sda2...\n");
    print_flush();

    if (format_kyrofs_partition(g_dfd, g_part2_lba, g_part2_sectors) == 0) {
        print("\033[1;32m      KyroFS filesystem (KYROFS02) formatted successfully.\033[0m\n");
        print("      Created Superblock, 64 Journal Slots, Inode Extents & Root Directory.\n");
    } else {
        print("\033[1;31m      Warning: KyroFS format encountered an error!\033[0m\n");
    }
    print_flush();

    // ------------------------------------------------------------------------
    // Step 3: Setup Bootloader Partition and Directory Hierarchy
    // ------------------------------------------------------------------------
    print("\033[1;34m[3/5]\033[0m Preparing directory hierarchy on KyroFS and Boot partition...\n");
    print_flush();

    g_sectors_per_fat = 1024;
    g_fat1_lba = g_part1_lba + RESERVED_SECTORS;
    g_fat2_lba = g_fat1_lba + g_sectors_per_fat;
    g_data_lba = g_fat2_lba + g_sectors_per_fat;
    g_next_cluster = 3;

    // Boot partition BPB
    uint8_t vbr[SECTOR_SIZE];
    memset(vbr, 0, sizeof(vbr));
    vbr[0] = 0xEB; vbr[1] = 0x58; vbr[2] = 0x90;
    memcpy(vbr + 3, "MSWIN4.1", 8);
    *(uint16_t *)(vbr + 11) = SECTOR_SIZE;
    vbr[13] = CLUSTER_SECTORS;
    *(uint16_t *)(vbr + 14) = RESERVED_SECTORS;
    vbr[16] = 2;
    vbr[21] = 0xF8;
    *(uint16_t *)(vbr + 24) = 63;
    *(uint16_t *)(vbr + 26) = 255;
    *(uint32_t *)(vbr + 28) = g_part1_lba;
    *(uint32_t *)(vbr + 32) = g_part1_sectors;
    *(uint32_t *)(vbr + 36) = g_sectors_per_fat;
    *(uint32_t *)(vbr + 44) = 2; // Root cluster
    *(uint16_t *)(vbr + 48) = 1; // FSInfo
    *(uint16_t *)(vbr + 50) = 6; // Backup boot
    vbr[64] = 0x80;
    vbr[66] = 0x29;
    *(uint32_t *)(vbr + 67) = 0x4B59524F;
    memcpy(vbr + 71, "KYROOS_BOOT", 11);
    memcpy(vbr + 82, "FAT32   ", 8);
    vbr[510] = 0x55; vbr[511] = 0xAA;

    lseek(g_dfd, (int64_t)g_part1_lba * SECTOR_SIZE, SEEK_SET);
    write(g_dfd, vbr, SECTOR_SIZE);
    lseek(g_dfd, (int64_t)(g_part1_lba + 6) * SECTOR_SIZE, SEEK_SET);
    write(g_dfd, vbr, SECTOR_SIZE);

    uint8_t fsinfo[SECTOR_SIZE];
    memset(fsinfo, 0, sizeof(fsinfo));
    *(uint32_t *)(fsinfo + 0) = 0x41615252;
    *(uint32_t *)(fsinfo + 484) = 0x61417272;
    *(uint32_t *)(fsinfo + 488) = 0xFFFFFFFF;
    *(uint32_t *)(fsinfo + 492) = 3;
    *(uint32_t *)(fsinfo + 508) = 0xAA550000;
    lseek(g_dfd, (int64_t)(g_part1_lba + 1) * SECTOR_SIZE, SEEK_SET);
    write(g_dfd, fsinfo, SECTOR_SIZE);

    memset(g_fat_table, 0, sizeof(g_fat_table));
    g_fat_table[0] = 0x0FFFFFF8;
    g_fat_table[1] = 0x0FFFFFFF;
    g_fat_table[2] = 0x0FFFFFFF;

    init_fat_dir(&g_dir_root, 2, 2);

    uint32_t clus_boot = g_next_cluster++;
    uint32_t clus_limine = g_next_cluster++;
    uint32_t clus_efi = g_next_cluster++;
    uint32_t clus_efi_boot = g_next_cluster++;
    uint32_t clus_bin = g_next_cluster++;
    uint32_t clus_etc = g_next_cluster++;
    uint32_t clus_lib = g_next_cluster++;
    uint32_t clus_modules = g_next_cluster++;
    uint32_t clus_home = g_next_cluster++;
    uint32_t clus_root_u = g_next_cluster++;
    uint32_t clus_tmp = g_next_cluster++;
    uint32_t clus_var = g_next_cluster++;
    uint32_t clus_dev = g_next_cluster++;
    uint32_t clus_proc = g_next_cluster++;

    g_fat_table[clus_boot] = 0x0FFFFFFF;
    g_fat_table[clus_limine] = 0x0FFFFFFF;
    g_fat_table[clus_efi] = 0x0FFFFFFF;
    g_fat_table[clus_efi_boot] = 0x0FFFFFFF;
    g_fat_table[clus_bin] = 0x0FFFFFFF;
    g_fat_table[clus_etc] = 0x0FFFFFFF;
    g_fat_table[clus_lib] = 0x0FFFFFFF;
    g_fat_table[clus_modules] = 0x0FFFFFFF;
    g_fat_table[clus_home] = 0x0FFFFFFF;
    g_fat_table[clus_root_u] = 0x0FFFFFFF;
    g_fat_table[clus_tmp] = 0x0FFFFFFF;
    g_fat_table[clus_var] = 0x0FFFFFFF;
    g_fat_table[clus_dev] = 0x0FFFFFFF;
    g_fat_table[clus_proc] = 0x0FFFFFFF;

    init_fat_dir(&g_dir_boot, clus_boot, 2);
    init_fat_dir(&g_dir_limine, clus_limine, clus_boot);
    init_fat_dir(&g_dir_efi, clus_efi, 2);
    init_fat_dir(&g_dir_efi_boot, clus_efi_boot, clus_efi);
    init_fat_dir(&g_dir_bin, clus_bin, 2);
    init_fat_dir(&g_dir_etc, clus_etc, 2);
    init_fat_dir(&g_dir_lib, clus_lib, 2);
    init_fat_dir(&g_dir_modules, clus_modules, clus_lib);
    init_fat_dir(&g_dir_home, clus_home, 2);
    init_fat_dir(&g_dir_root_user, clus_root_u, 2);
    init_fat_dir(&g_dir_tmp, clus_tmp, 2);
    init_fat_dir(&g_dir_var, clus_var, 2);
    init_fat_dir(&g_dir_dev, clus_dev, 2);
    init_fat_dir(&g_dir_proc, clus_proc, 2);

    dir_add_entry(&g_dir_root, "boot", 0x10, clus_boot, 0);
    dir_add_entry(&g_dir_root, "EFI", 0x10, clus_efi, 0);
    dir_add_entry(&g_dir_root, "bin", 0x10, clus_bin, 0);
    dir_add_entry(&g_dir_root, "etc", 0x10, clus_etc, 0);
    dir_add_entry(&g_dir_root, "lib", 0x10, clus_lib, 0);
    dir_add_entry(&g_dir_root, "home", 0x10, clus_home, 0);
    dir_add_entry(&g_dir_root, "root", 0x10, clus_root_u, 0);
    dir_add_entry(&g_dir_root, "tmp", 0x10, clus_tmp, 0);
    dir_add_entry(&g_dir_root, "var", 0x10, clus_var, 0);
    dir_add_entry(&g_dir_root, "dev", 0x10, clus_dev, 0);
    dir_add_entry(&g_dir_root, "proc", 0x10, clus_proc, 0);

    dir_add_entry(&g_dir_boot, "limine", 0x10, clus_limine, 0);
    dir_add_entry(&g_dir_efi, "BOOT", 0x10, clus_efi_boot, 0);
    dir_add_entry(&g_dir_lib, "modules", 0x10, clus_modules, 0);

    print("\033[1;32m      Directory hierarchy prepared successfully.\033[0m\n");
    print_flush();

    // ------------------------------------------------------------------------
    // Step 4: Installing system files
    // ------------------------------------------------------------------------
    print("\033[1;34m[4/5]\033[0m Installing system binaries and configuration files...\n");
    print_flush();

    print("      Installing kernel & Limine stage files...\n");
    print_flush();
    install_file_to_fat("/boot/kernel.elf", &g_dir_boot, "kernel.elf");
    install_file_to_fat("/boot/kernel.elf", &g_dir_root, "kernel.elf");
    install_file_to_fat("/boot/limine/limine-bios.sys", &g_dir_limine, "limine-bios.sys");
    install_file_to_fat("/boot/limine/limine-bios.sys", &g_dir_root, "limine-bios.sys");
    install_file_to_fat("/boot/limine/BOOTX64.EFI", &g_dir_limine, "BOOTX64.EFI");
    install_file_to_fat("/boot/limine/BOOTX64.EFI", &g_dir_efi_boot, "BOOTX64.EFI");
    install_file_to_fat("/boot/limine/BOOTX64.EFI", &g_dir_root, "BOOTX64.EFI");
    install_file_to_fat("/boot/limine/limine-bios-cd.bin", &g_dir_limine, "limine-bios-cd.bin");

    // Copy core system binaries
    const char *system_binaries[] = {
        "alias", "cat", "chmod", "chown", "clear", "cp", "curl", "cursor_test",
        "date", "debugmode", "df", "doom", "du", "echo", "env", "export",
        "false", "fdisk", "find", "free", "game", "gears", "gpuinfo", "gradient_demo",
        "grep", "groupadd", "groupdel", "groups", "halt", "head", "hello", "history",
        "hostname", "httpd", "id", "ifconfig", "init", "insmod", "installer", "kill", "killall",
        "kpm", "kyro", "ln", "ls", "lsblk", "lsmod", "lua", "mkdir", "mkfs", "mount", "mv",
        "nice", "passwd", "poweroff", "pwd", "reboot", "renice", "reset", "rm",
        "rmdir", "rmmod", "sh", "sleep", "sort", "su", "sync", "sysmon",
        "tail", "test", "time", "touch", "true", "umount", "unalias", "uname",
        "uniq", "unset", "uptime", "useradd", "userdel", "users", "vkcube", "wc",
        "wget", "whoami", "yes", NULL
    };

    char limine_conf_buf[8192];
    int conf_len = snprintf(limine_conf_buf, sizeof(limine_conf_buf),
        "timeout: 3\n\n"
        "/KyroOS Carbon\n"
        "    protocol: limine\n"
        "    path: boot():/boot/kernel.elf\n"
    );

    int installed_count = 0;
    for (int i = 0; system_binaries[i]; i++) {
        char src[64];
        snprintf(src, sizeof(src), "/bin/%s", system_binaries[i]);
        if (install_file_to_fat(src, &g_dir_bin, system_binaries[i]) == 0) {
            installed_count++;
            conf_len += snprintf(limine_conf_buf + conf_len, sizeof(limine_conf_buf) - conf_len,
                "    module_path: boot():/bin/%s\n", system_binaries[i]);
        }
    }
    printf("      Installed %d system binaries to /bin.\n", installed_count);
    print_flush();

    // Populate /etc/ files
    const char *osrel = "NAME=\"KyroOS\"\nVERSION=\"26.11.13 (Carbon)\"\nID=kyroos\nPRETTY_NAME=\"KyroOS Carbon\"\n";
    install_buffer_to_fat(osrel, strlen(osrel), &g_dir_etc, "os-release");

    const char *issue = "\n\033[1;36m========================================\033[0m\n\033[1;32m      Welcome to KyroOS Carbon!         \033[0m\n\033[1;36m========================================\033[0m\n\n";
    install_buffer_to_fat(issue, strlen(issue), &g_dir_etc, "issue");

    const char *hname = "kyroos\n";
    install_buffer_to_fat(hname, strlen(hname), &g_dir_etc, "hostname");

    const char *passwd = "root:x:0:0:root:/root:/bin/sh\nkyro:x:1000:1000:Kyro User:/home/kyro:/bin/sh\n";
    install_buffer_to_fat(passwd, strlen(passwd), &g_dir_etc, "passwd");

    const char *shadow = "root:*:19000:0:99999:7:::\nkyro:*:19000:0:99999:7:::\n";
    install_buffer_to_fat(shadow, strlen(shadow), &g_dir_etc, "shadow");

    const char *group = "root:x:0:\nusers:x:100:\nwheel:x:10:\nkyro:x:1000:\n";
    install_buffer_to_fat(group, strlen(group), &g_dir_etc, "group");

    const char *resolv = "nameserver 8.8.8.8\nnameserver 1.1.1.1\n";
    install_buffer_to_fat(resolv, strlen(resolv), &g_dir_etc, "resolv.conf");

    const char *etc_files[] = {"os-release", "issue", "hostname", "passwd", "shadow", "group", "resolv.conf", NULL};
    for (int i = 0; etc_files[i]; i++) {
        conf_len += snprintf(limine_conf_buf + conf_len, sizeof(limine_conf_buf) - conf_len,
            "    module_path: boot():/etc/%s\n", etc_files[i]);
    }

    // Kernel modules
    if (install_file_to_fat("/lib/modules/hello.ko", &g_dir_modules, "hello.ko") == 0) {
        conf_len += snprintf(limine_conf_buf + conf_len, sizeof(limine_conf_buf) - conf_len,
            "    module_path: boot():/lib/modules/hello.ko\n");
    }

    // Write final limine.conf into /boot/limine/limine.conf and /limine.conf on target disk
    install_buffer_to_fat(limine_conf_buf, strlen(limine_conf_buf), &g_dir_limine, "limine.conf");
    install_buffer_to_fat(limine_conf_buf, strlen(limine_conf_buf), &g_dir_root, "limine.conf");

    print("\033[1;32m      System files installed successfully to KyroFS.\033[0m\n");
    print_flush();

    // ------------------------------------------------------------------------
    // Step 5: Finalizing bootloader configuration
    // ------------------------------------------------------------------------
    print("\033[1;34m[5/5]\033[0m Finalizing Limine bootloader configuration...\n");
    print_flush();

    flush_fat_dir(&g_dir_root);
    flush_fat_dir(&g_dir_boot);
    flush_fat_dir(&g_dir_limine);
    flush_fat_dir(&g_dir_efi);
    flush_fat_dir(&g_dir_efi_boot);
    flush_fat_dir(&g_dir_bin);
    flush_fat_dir(&g_dir_etc);
    flush_fat_dir(&g_dir_lib);
    flush_fat_dir(&g_dir_modules);
    flush_fat_dir(&g_dir_home);
    flush_fat_dir(&g_dir_root_user);
    flush_fat_dir(&g_dir_tmp);
    flush_fat_dir(&g_dir_var);
    flush_fat_dir(&g_dir_dev);
    flush_fat_dir(&g_dir_proc);

    lseek(g_dfd, (int64_t)g_fat1_lba * SECTOR_SIZE, SEEK_SET);
    write(g_dfd, g_fat_table, sizeof(g_fat_table));

    lseek(g_dfd, (int64_t)g_fat2_lba * SECTOR_SIZE, SEEK_SET);
    write(g_dfd, g_fat_table, sizeof(g_fat_table));

    close(g_dfd);

    print("\n\033[1;32m============================================================\033[0m\n");
    print("\033[1;32m   CONGRATULATIONS! KyroOS has been installed successfully! \033[0m\n");
    print("\033[1;32m============================================================\033[0m\n\n");
    print("You can now reboot your machine and boot directly from your drive:\n");
    print("  1. Remove the ISO installation media.\n");
    printf("  2. Select '%s' as the primary boot device.\n\n", target_drive);
    print_flush();

    return 0;
}
