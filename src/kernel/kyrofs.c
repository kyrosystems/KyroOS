#include "kyrofs.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "vfs.h"
#include "ide.h"
#include "thread.h"
#include "mutex.h"
#include <string.h>
#include <stddef.h>
#include "tty.h"

typedef struct kyrofs_ino_dirent {
    vfs_node_t node;
    struct vfs_node *parent;
    struct kyrofs_ino_dirent *next;
} kyrofs_ino_dirent_t;

typedef struct {
    uint8_t *content;
    uint32_t size;
    uint32_t capacity;
} kyrofs_file_content_t;

static int kyrofs_mkdir(vfs_node_t *node, char *name, uint16_t mode);
static int kyrofs_create(vfs_node_t *node, char *name, uint16_t mode);
static int kyrofs_remove(vfs_node_t *node, char *name);
static int kyrofs_rmdir(vfs_node_t *node, char *name, uint16_t mode);
static int kyrofs_stat(vfs_node_t *node, struct stat *stat_buf);
static int kyrofs_ioctl(vfs_node_t *node, int request, void *argp);

static void kyrofs_open(vfs_node_t *node, int flags) {
    if (node->flags & VFS_FILE) {
        if (flags & O_TRUNC) {
            kyrofs_file_content_t *fc = (kyrofs_file_content_t *)node->ptr;
            if (fc->content) kfree(fc->content);
            fc->content = NULL;
            fc->size = 0;
            node->length = 0;
        }
    }
}

static uint32_t kyrofs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    kyrofs_file_content_t *fc = (kyrofs_file_content_t *)node->ptr;
    if (!fc || !fc->content) return 0;
    if (offset >= fc->size) return 0;
    if (offset + size > fc->size) size = fc->size - offset;
    memcpy(buffer, fc->content + offset, size);
    return size;
}

static uint32_t kyrofs_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    kyrofs_file_content_t *fc = (kyrofs_file_content_t *)node->ptr;
    if (offset + size > fc->capacity) {
        uint32_t new_cap = (offset + size) * 2;
        uint8_t *new_cont = (uint8_t *)kmalloc(new_cap);
        if (!new_cont) panic("kyrofs_write: kmalloc failed", NULL);
        if (fc->content) { memcpy(new_cont, fc->content, fc->size); kfree(fc->content); }
        fc->content = new_cont;
        fc->capacity = new_cap;
    }
    memcpy(fc->content + offset, buffer, size);
    if (offset + size > fc->size) fc->size = offset + size;
    node->length = fc->size;
    return size;
}

static vfs_node_t *kyrofs_finddir(vfs_node_t *node, char *name) {
    if (strcmp(name, ".") == 0) return node;
    if (strcmp(name, "..") == 0) {
        kyrofs_ino_dirent_t *de = (kyrofs_ino_dirent_t *)node;
        return de->parent ? de->parent : node;
    }
    kyrofs_ino_dirent_t *cur = (kyrofs_ino_dirent_t *)node->ptr;
    while (cur) {
        if (strcmp(cur->node.name, name) == 0) return &cur->node;
        cur = cur->next;
    }
    return NULL;
}

static int kyrofs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
    kyrofs_ino_dirent_t *cur = (kyrofs_ino_dirent_t *)node->ptr;
    uint32_t i = 0;
    while (cur && i < index) { cur = cur->next; i++; }
    if (cur) {
        strncpy(dir_entry->name, cur->node.name, MAX_FILENAME_LEN);
        dir_entry->ino = cur->node.inode;
        return 1;
    }
    return 0;
}

int kyrofs_create_node(vfs_node_t *parent, char *name, uint32_t flags) {
    kyrofs_ino_dirent_t *new_de = (kyrofs_ino_dirent_t *)kmalloc(sizeof(kyrofs_ino_dirent_t));
    if (!new_de) panic("kyrofs_create_node: kmalloc failed", NULL);
    memset(new_de, 0, sizeof(kyrofs_ino_dirent_t));
    strncpy(new_de->node.name, name, MAX_FILENAME_LEN - 1);
    new_de->node.name[MAX_FILENAME_LEN - 1] = '\0';
    new_de->node.flags = flags;
    new_de->node.inode = vfs_get_next_inode();

    if (flags & VFS_FILE) {
        kyrofs_file_content_t *content = (kyrofs_file_content_t *)kmalloc(sizeof(kyrofs_file_content_t));
        if (!content) panic("kyrofs_create_node: kmalloc failed", NULL);
        content->size = 0;
        content->capacity = 128;
        content->content = (uint8_t *)kmalloc(content->capacity);
        if (!content->content) panic("kyrofs_create_node: kmalloc failed", NULL);
        new_de->node.ptr = content;
        new_de->node.read = kyrofs_read;
        new_de->node.write = kyrofs_write;
        new_de->node.open = kyrofs_open;
        new_de->node.close = NULL;
    } else {
        new_de->node.ptr = NULL;
        new_de->node.finddir = kyrofs_finddir;
        new_de->node.readdir = kyrofs_readdir;
        new_de->node.mkdir = kyrofs_mkdir;
        new_de->node.create = kyrofs_create;
        new_de->node.remove = kyrofs_remove;
        new_de->node.rmdir = kyrofs_rmdir;
        new_de->node.stat = kyrofs_stat;
        new_de->node.ioctl = kyrofs_ioctl;
    }

    new_de->parent = parent;
    new_de->next = (kyrofs_ino_dirent_t *)parent->ptr;
    parent->ptr = new_de;
    return 0;
}

static int kyrofs_mkdir(vfs_node_t *node, char *name, uint16_t mode) {
    (void)mode;
    if (vfs_finddir(node, name)) return -1;
    return kyrofs_create_node(node, name, VFS_DIRECTORY);
}

static int kyrofs_create(vfs_node_t *node, char *name, uint16_t mode) {
    (void)mode;
    return kyrofs_create_node(node, name, VFS_FILE);
}

static int kyrofs_remove(vfs_node_t *node, char *name) {
    kyrofs_ino_dirent_t *cur = (kyrofs_ino_dirent_t *)node->ptr;
    kyrofs_ino_dirent_t *prev = NULL;
    while (cur) {
        if (strcmp(cur->node.name, name) == 0) {
            if ((cur->node.flags & VFS_DIRECTORY) && cur->node.ptr != NULL) return -1;
            if (prev) prev->next = cur->next; else node->ptr = cur->next;
            if (cur->node.flags & VFS_FILE) {
                kyrofs_file_content_t *content = (kyrofs_file_content_t *)cur->node.ptr;
                if (content->content) kfree(content->content);
                kfree(content);
            }
            kfree(cur);
            return 0;
        }
        prev = cur; cur = cur->next;
    }
    return -1;
}

static int kyrofs_rmdir(vfs_node_t *node, char *name, uint16_t mode) {
    (void)mode;
    kyrofs_ino_dirent_t *cur = (kyrofs_ino_dirent_t *)node->ptr;
    kyrofs_ino_dirent_t *prev = NULL;
    while (cur) {
        if (strcmp(cur->node.name, name) == 0) {
            if (!(cur->node.flags & VFS_DIRECTORY)) return -1;
            if (cur->node.ptr != NULL) return -1;
            if (prev) prev->next = cur->next; else node->ptr = cur->next;
            kfree(cur);
            return 0;
        }
        prev = cur; cur = cur->next;
    }
    return -1;
}

static int kyrofs_stat(vfs_node_t *node, struct stat *stat_buf) {
    if (!node || !stat_buf) return -1;
    memset(stat_buf, 0, sizeof(struct stat));
    stat_buf->st_size = node->length;
    stat_buf->st_ino = node->inode;
    if (node->flags & VFS_FILE) stat_buf->st_mode |= S_IFREG;
    else if (node->flags & VFS_DIRECTORY) stat_buf->st_mode |= S_IFDIR;
    else if (node->flags & VFS_CHARDEVICE) stat_buf->st_mode |= S_IFREG;
    stat_buf->st_mode |= 0755;
    return 0;
}

static int kyrofs_ioctl(vfs_node_t *node, int request, void *argp) {
    (void)node; (void)request; (void)argp;
    return -1;
}

static kyrofs_ino_dirent_t *root_node = NULL;

vfs_node_t *kyrofs_create_dir_recursive(vfs_node_t *current_root, const char *path) {
    if (!path || !*path || strcmp(path, "/") == 0) return current_root;
    char temp_path[MAX_FILENAME_LEN];
    strncpy(temp_path, path, MAX_FILENAME_LEN - 1);
    temp_path[MAX_FILENAME_LEN - 1] = '\0';
    char *token_start = temp_path;
    if (*token_start == '/') token_start++;
    vfs_node_t *current_node = current_root;
    while (*token_start) {
        char *token_end = token_start;
        while (*token_end && *token_end != '/') token_end++;
        char name[MAX_FILENAME_LEN];
        int len = token_end - token_start;
        if (len >= MAX_FILENAME_LEN) return NULL;
        strncpy(name, token_start, len);
        name[len] = '\0';
        if (len == 0) { token_start = token_end; if (*token_start == '/') token_start++; continue; }
        vfs_node_t *child = vfs_finddir(current_node, name);
        if (!child) {
            kyrofs_create_node(current_node, name, VFS_DIRECTORY);
            child = vfs_finddir(current_node, name);
            if (!child) return NULL;
        } else if (!(child->flags & VFS_DIRECTORY)) return NULL;
        current_node = child;
        token_start = token_end;
        if (*token_start == '/') token_start++;
    }
    return current_node;
}

int kyrofs_add_file(const char *full_path, void *data, uint32_t size) {
    if (!full_path || !*full_path || full_path[0] != '/') return -1;
    char path_copy[MAX_FILENAME_LEN];
    strncpy(path_copy, full_path, MAX_FILENAME_LEN - 1);
    path_copy[MAX_FILENAME_LEN - 1] = '\0';
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) return -1;
    char *filename = last_slash + 1;
    *last_slash = '\0';
    const char *parent_dir_path = path_copy;
    if (parent_dir_path[0] == '\0') parent_dir_path = "/";
    vfs_node_t *parent_node = kyrofs_create_dir_recursive(vfs_root, parent_dir_path);
    if (!parent_node) return -1;
    if (vfs_finddir(parent_node, filename)) kyrofs_remove(parent_node, filename);
    kyrofs_create_node(parent_node, filename, VFS_FILE);
    vfs_node_t *file_node = vfs_finddir(parent_node, filename);
    if (!file_node) return -1;
    kyrofs_write(file_node, 0, size, (uint8_t *)data);
    file_node->length = size;
    return 0;
}

void kyrofs_init(struct limine_module_response *mod_resp) {
    (void)mod_resp;
    root_node = (kyrofs_ino_dirent_t *)kmalloc(sizeof(kyrofs_ino_dirent_t));
    if (!root_node) panic("kyrofs_init: kmalloc failed", NULL);
    memset(root_node, 0, sizeof(kyrofs_ino_dirent_t));
    strncpy(root_node->node.name, "/", 2);
    root_node->node.flags = VFS_DIRECTORY;
    root_node->node.finddir = kyrofs_finddir;
    root_node->node.readdir = kyrofs_readdir;
    root_node->node.mkdir = kyrofs_mkdir;
    root_node->node.create = kyrofs_create;
    root_node->node.remove = kyrofs_remove;
    root_node->node.rmdir = kyrofs_rmdir;
    root_node->node.stat = kyrofs_stat;
    root_node->node.ioctl = kyrofs_ioctl;
    root_node->parent = NULL;
}

vfs_node_t *get_kyrofs_root(void) { return root_node ? &root_node->node : NULL; }

static vfs_node_t *kd_device = NULL;
static uint32_t kd_part_lba = 0;
static kyrofs_superblock_t kd_sb;
static kyrofs_group_desc_t *kd_groups = NULL;
static mutex_t kd_lock;

static uint32_t kd_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1) - 1)));
    }
    return ~crc;
}

static int kd_read_block(uint64_t rel_lba, void *buf) {
    if (!kd_device) return -1;
    ide_read_blocks_t rb = { .lba = kd_part_lba + (uint32_t)rel_lba, .count = 1, .buffer = buf };
    return kd_device->ioctl(kd_device, IDE_IOCTL_READ_BLOCKS, &rb);
}

static int kd_write_block_raw(uint64_t rel_lba, const void *buf) {
    if (!kd_device) return -1;
    ide_write_blocks_t wb = { .lba = kd_part_lba + (uint32_t)rel_lba, .count = 1, .buffer = (void *)buf };
    return kd_device->ioctl(kd_device, IDE_IOCTL_WRITE_BLOCKS, &wb);
}

static uint64_t kd_journal_slot_block(uint32_t slot) { return kd_sb.journal_start + (uint64_t)slot * 2; }

int kyrofs_journal_write(uint64_t target_block, const void *data) {
    uint32_t slot = kd_sb.journal_seq % KYROFS_JOURNAL_SLOTS;
    uint64_t hdr_blk = kd_journal_slot_block(slot);
    uint64_t data_blk = hdr_blk + 1;
    kyrofs_journal_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = KYROFS_JOURNAL_MAGIC;
    hdr.seq = ++kd_sb.journal_seq;
    hdr.target_block = target_block;
    hdr.payload_crc32 = kd_crc32(data, KYROFS_BLOCK_SIZE);
    hdr.committed = 1;
    hdr.applied = 0;
    if (kd_write_block_raw(data_blk, data) != 0) return -1;
    if (kd_write_block_raw(hdr_blk, &hdr) != 0) return -1;
    if (kd_write_block_raw(target_block, data) != 0) return -1;
    hdr.applied = 1;
    return kd_write_block_raw(hdr_blk, &hdr);
}

int kyrofs_journal_replay(void) {
    int replayed = 0;
    for (uint32_t slot = 0; slot < KYROFS_JOURNAL_SLOTS; slot++) {
        uint64_t hdr_blk = kd_journal_slot_block(slot);
        uint64_t data_blk = hdr_blk + 1;
        uint8_t hdr_buf[KYROFS_BLOCK_SIZE];
        if (kd_read_block(hdr_blk, hdr_buf) != 0) continue;
        kyrofs_journal_header_t *hdr = (kyrofs_journal_header_t *)hdr_buf;
        if (hdr->magic != KYROFS_JOURNAL_MAGIC) continue;
        if (hdr->committed && !hdr->applied) {
            uint8_t data_buf[KYROFS_BLOCK_SIZE];
            if (kd_read_block(data_blk, data_buf) != 0) continue;
            if (kd_crc32(data_buf, KYROFS_BLOCK_SIZE) != hdr->payload_crc32) continue;
            kd_write_block_raw(hdr->target_block, data_buf);
            hdr->applied = 1;
            kd_write_block_raw(hdr_blk, hdr_buf);
            replayed++;
        }
    }
    return replayed;
}

static int kd_write_block(uint64_t rel_lba, const void *buf) { return kyrofs_journal_write(rel_lba, buf); }

static inline int bit_test(const uint8_t *bm, uint32_t i) { return (bm[i / 8] >> (i % 8)) & 1; }
static inline void bit_set(uint8_t *bm, uint32_t i) { bm[i / 8] |= (uint8_t)(1u << (i % 8)); }
static inline void bit_clear(uint8_t *bm, uint32_t i) { bm[i / 8] &= (uint8_t)~(1u << (i % 8)); }

static int kd_flush_superblock(void) {
    kd_sb.crc32 = 0;
    kd_sb.crc32 = kd_crc32(&kd_sb, sizeof(kd_sb));
    uint8_t buf[KYROFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, &kd_sb, sizeof(kd_sb));
    return kd_write_block(0, buf);
}

static int kd_flush_group(uint32_t g) {
    uint8_t buf[KYROFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    kd_groups[g].crc32 = 0;
    kd_groups[g].crc32 = kd_crc32(&kd_groups[g], sizeof(kyrofs_group_desc_t));
    memcpy(buf, kd_groups, sizeof(kyrofs_group_desc_t) * kd_sb.group_count);
    return kd_write_block(kd_sb.group_desc_block, buf);
}

static int64_t kd_alloc_block(void) {
    for (uint32_t g = 0; g < kd_sb.group_count; g++) {
        if (kd_groups[g].free_blocks_in_group == 0) continue;
        uint8_t bm[KYROFS_BLOCK_SIZE];
        if (kd_read_block(kd_groups[g].block_bitmap_block, bm) != 0) return -1;
        for (uint32_t i = 0; i < kd_groups[g].group_blocks; i++) {
            if (!bit_test(bm, i)) {
                bit_set(bm, i);
                kd_write_block(kd_groups[g].block_bitmap_block, bm);
                kd_groups[g].free_blocks_in_group--;
                kd_sb.free_blocks--;
                kd_flush_group(g);
                kd_flush_superblock();
                return (int64_t)(kd_groups[g].data_block_start + i);
            }
        }
    }
    return -1;
}

static void kd_free_block(uint64_t block) {
    for (uint32_t g = 0; g < kd_sb.group_count; g++) {
        uint64_t start = kd_groups[g].data_block_start;
        uint64_t end = start + kd_groups[g].group_blocks;
        if (block >= start && block < end) {
            uint32_t idx = (uint32_t)(block - start);
            uint8_t bm[KYROFS_BLOCK_SIZE];
            if (kd_read_block(kd_groups[g].block_bitmap_block, bm) != 0) return;
            if (bit_test(bm, idx)) {
                bit_clear(bm, idx);
                kd_write_block(kd_groups[g].block_bitmap_block, bm);
                kd_groups[g].free_blocks_in_group++;
                kd_sb.free_blocks++;
                kd_flush_group(g);
                kd_flush_superblock();
            }
            return;
        }
    }
}

static int64_t kd_alloc_inode(void) {
    for (uint32_t g = 0; g < kd_sb.group_count; g++) {
        if (kd_groups[g].free_inodes_in_group == 0) continue;
        uint8_t bm[KYROFS_BLOCK_SIZE];
        if (kd_read_block(kd_groups[g].inode_bitmap_block, bm) != 0) return -1;
        for (uint32_t i = 0; i < kd_groups[g].group_inodes; i++) {
            if (!bit_test(bm, i)) {
                bit_set(bm, i);
                kd_write_block(kd_groups[g].inode_bitmap_block, bm);
                kd_groups[g].free_inodes_in_group--;
                kd_sb.free_inodes--;
                kd_flush_group(g);
                kd_flush_superblock();
                return (int64_t)(1 + g * kd_sb.inodes_per_group + i);
            }
        }
    }
    return -1;
}

static void kd_free_inode(uint64_t ino) {
    uint32_t g = (uint32_t)((ino - 1) / kd_sb.inodes_per_group);
    uint32_t idx = (uint32_t)((ino - 1) % kd_sb.inodes_per_group);
    if (g >= kd_sb.group_count) return;
    uint8_t bm[KYROFS_BLOCK_SIZE];
    if (kd_read_block(kd_groups[g].inode_bitmap_block, bm) != 0) return;
    if (bit_test(bm, idx)) {
        bit_clear(bm, idx);
        kd_write_block(kd_groups[g].inode_bitmap_block, bm);
        kd_groups[g].free_inodes_in_group++;
        kd_sb.free_inodes++;
        kd_flush_group(g);
        kd_flush_superblock();
    }
}

static int kd_read_inode(uint64_t ino, kyrofs_inode_t *out) {
    if (ino < 1) return -1;
    uint32_t g = (uint32_t)((ino - 1) / kd_sb.inodes_per_group);
    uint32_t idx = (uint32_t)((ino - 1) % kd_sb.inodes_per_group);
    if (g >= kd_sb.group_count) return -1;
    uint32_t per_block = KYROFS_BLOCK_SIZE / sizeof(kyrofs_inode_t);
    uint64_t blk = kd_groups[g].inode_table_block + idx / per_block;
    uint32_t off = (idx % per_block) * sizeof(kyrofs_inode_t);
    uint8_t buf[KYROFS_BLOCK_SIZE];
    if (kd_read_block(blk, buf) != 0) return -1;
    memcpy(out, buf + off, sizeof(kyrofs_inode_t));
    return 0;
}

static int kd_write_inode(uint64_t ino, const kyrofs_inode_t *in) {
    if (ino < 1) return -1;
    uint32_t g = (uint32_t)((ino - 1) / kd_sb.inodes_per_group);
    uint32_t idx = (uint32_t)((ino - 1) % kd_sb.inodes_per_group);
    if (g >= kd_sb.group_count) return -1;
    uint32_t per_block = KYROFS_BLOCK_SIZE / sizeof(kyrofs_inode_t);
    uint64_t blk = kd_groups[g].inode_table_block + idx / per_block;
    uint32_t off = (idx % per_block) * sizeof(kyrofs_inode_t);
    uint8_t buf[KYROFS_BLOCK_SIZE];
    if (kd_read_block(blk, buf) != 0) return -1;
    kyrofs_inode_t tmp = *in;
    tmp.crc32 = 0;
    tmp.crc32 = kd_crc32(&tmp, sizeof(tmp));
    memcpy(buf + off, &tmp, sizeof(tmp));
    return kd_write_block(blk, buf);
}

static int64_t kd_inode_map_block(kyrofs_inode_t *ino, uint32_t logical_blk, int allocate) {
    uint64_t seen = 0;
    for (uint32_t e = 0; e < ino->extent_count; e++) {
        uint64_t len = ino->extents[e].length;
        if (logical_blk < seen + len)
            return (int64_t)(ino->extents[e].start_block + (logical_blk - seen));
        seen += len;
    }

    if (!allocate) return -1;

    int64_t new_block = kd_alloc_block();
    if (new_block < 0) return -1;

    if (ino->extent_count > 0) {
        kyrofs_extent_t *last = &ino->extents[ino->extent_count - 1];
        if (last->start_block + last->length == (uint64_t)new_block) {
            last->length++;
            ino->blocks++;
            return new_block;
        }
    }

    if (ino->extent_count >= KYROFS_MAX_EXTENTS) {
        kd_free_block((uint64_t)new_block);
        return -1;
    }

    ino->extents[ino->extent_count].start_block = (uint64_t)new_block;
    ino->extents[ino->extent_count].length = 1;
    ino->extent_count++;
    ino->blocks++;
    return new_block;
}

static void kd_inode_free_all_blocks(kyrofs_inode_t *ino) {
    for (uint32_t e = 0; e < ino->extent_count; e++) {
        for (uint64_t b = 0; b < ino->extents[e].length; b++)
            kd_free_block(ino->extents[e].start_block + b);
    }
    ino->extent_count = 0;
    ino->blocks = 0;
    ino->size = 0;
}

static int64_t kd_file_read(kyrofs_inode_t *ino, uint64_t offset, uint32_t size, uint8_t *buf) {
    if (offset >= ino->size) return 0;
    if (offset + size > ino->size) size = (uint32_t)(ino->size - offset);
    uint32_t done = 0;
    while (done < size) {
        uint32_t lblk = (uint32_t)((offset + done) / KYROFS_BLOCK_SIZE);
        uint32_t in_blk_off = (uint32_t)((offset + done) % KYROFS_BLOCK_SIZE);
        int64_t pblk = kd_inode_map_block(ino, lblk, 0);
        uint8_t block_buf[KYROFS_BLOCK_SIZE];
        if (pblk >= 0) { if (kd_read_block((uint64_t)pblk, block_buf) != 0) break; }
        else memset(block_buf, 0, sizeof(block_buf));
        uint32_t chunk = KYROFS_BLOCK_SIZE - in_blk_off;
        if (chunk > size - done) chunk = size - done;
        memcpy(buf + done, block_buf + in_blk_off, chunk);
        done += chunk;
    }
    return done;
}

static int64_t kd_file_write(kyrofs_inode_t *ino, uint64_t offset, uint32_t size, const uint8_t *buf) {
    uint32_t done = 0;
    while (done < size) {
        uint32_t lblk = (uint32_t)((offset + done) / KYROFS_BLOCK_SIZE);
        uint32_t in_blk_off = (uint32_t)((offset + done) % KYROFS_BLOCK_SIZE);
        int64_t pblk = kd_inode_map_block(ino, lblk, 1);
        if (pblk < 0) break;
        uint8_t block_buf[KYROFS_BLOCK_SIZE];
        uint32_t chunk = KYROFS_BLOCK_SIZE - in_blk_off;
        if (chunk > size - done) chunk = size - done;
        if (chunk < KYROFS_BLOCK_SIZE) {
            if (kd_read_block((uint64_t)pblk, block_buf) != 0) memset(block_buf, 0, sizeof(block_buf));
        }
        memcpy(block_buf + in_blk_off, buf + done, chunk);
        if (kd_write_block_raw((uint64_t)pblk, block_buf) != 0) break;
        done += chunk;
    }
    if (offset + done > ino->size) ino->size = offset + done;
    return done;
}

static int kd_dir_find(kyrofs_inode_t *dir_ino, const char *name, kyrofs_dirent_t *out, uint64_t *out_off) {
    uint64_t off = 0;
    while (off < dir_ino->size) {
        kyrofs_dirent_t de;
        if (kd_file_read(dir_ino, off, sizeof(de), (uint8_t *)&de) != sizeof(de)) break;
        if (de.inode != 0 && strncmp(de.name, name, de.name_len) == 0 && name[de.name_len] == '\0') {
            if (out) *out = de;
            if (out_off) *out_off = off;
            return 0;
        }
        off += de.rec_len ? de.rec_len : sizeof(de);
    }
    return -1;
}

static int kd_dir_add(kyrofs_inode_t *dir_ino, uint64_t dir_num, const char *name, uint64_t child_ino, uint8_t file_type) {
    kyrofs_dirent_t de;
    memset(&de, 0, sizeof(de));
    de.inode = child_ino;
    de.name_len = (uint16_t)strlen(name);
    de.rec_len = sizeof(de);
    de.file_type = file_type;
    strncpy(de.name, name, sizeof(de.name) - 1);
    int64_t w = kd_file_write(dir_ino, dir_ino->size, sizeof(de), (uint8_t *)&de);
    if (w != (int64_t)sizeof(de)) return -1;
    dir_ino->mtime++;
    return kd_write_inode(dir_num, dir_ino);
}

static int kd_dir_remove(kyrofs_inode_t *dir_ino, uint64_t dir_num, const char *name) {
    uint64_t off;
    kyrofs_dirent_t de;
    if (kd_dir_find(dir_ino, name, &de, &off) != 0) return -1;
    de.inode = 0;
    kd_file_write(dir_ino, off, sizeof(de), (uint8_t *)&de);
    return kd_write_inode(dir_num, dir_ino);
}

static vfs_node_t *kd_make_vfs_node(uint64_t ino_num, kyrofs_inode_t *ino, const char *name);

static uint32_t kd_vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    kyrofs_inode_info_t *ii = (kyrofs_inode_info_t *)node->ptr;
    return (uint32_t)kd_file_read(&ii->inode, offset, size, buffer);
}

static uint32_t kd_vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    kyrofs_inode_info_t *ii = (kyrofs_inode_info_t *)node->ptr;
    int64_t w = kd_file_write(&ii->inode, offset, size, buffer);
    if (w > 0) {
        kd_write_inode(ii->inode_num, &ii->inode);
        node->length = (uint32_t)ii->inode.size;
    }
    return (uint32_t)(w < 0 ? 0 : w);
}

static void kd_vfs_open(vfs_node_t *node, int flags) {
    kyrofs_inode_info_t *ii = (kyrofs_inode_info_t *)node->ptr;
    if (flags & O_TRUNC) {
        kd_inode_free_all_blocks(&ii->inode);
        kd_write_inode(ii->inode_num, &ii->inode);
        node->length = 0;
    }
}

static void kd_vfs_close(vfs_node_t *node) {
    kyrofs_inode_info_t *ii = (kyrofs_inode_info_t *)node->ptr;
    if (ii->dirty) { kd_write_inode(ii->inode_num, &ii->inode); ii->dirty = 0; }
}

static vfs_node_t *kd_vfs_finddir(vfs_node_t *node, char *name) {
    kyrofs_inode_info_t *dir_ii = (kyrofs_inode_info_t *)node->ptr;
    if (strcmp(name, ".") == 0) return node;
    kyrofs_dirent_t de;
    if (kd_dir_find(&dir_ii->inode, name, &de, NULL) != 0) return NULL;
    kyrofs_inode_t child;
    if (kd_read_inode(de.inode, &child) != 0) return NULL;
    return kd_make_vfs_node(de.inode, &child, name);
}

static int kd_vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
    kyrofs_inode_info_t *dir_ii = (kyrofs_inode_info_t *)node->ptr;
    uint64_t off = 0;
    uint32_t count = 0;
    while (off < dir_ii->inode.size) {
        kyrofs_dirent_t de;
        if (kd_file_read(&dir_ii->inode, off, sizeof(de), (uint8_t *)&de) != sizeof(de)) break;
        if (de.inode != 0) {
            if (count == index) {
                strncpy(dir_entry->name, de.name, MAX_FILENAME_LEN - 1);
                dir_entry->ino = (uint32_t)de.inode;
                return 1;
            }
            count++;
        }
        off += de.rec_len ? de.rec_len : sizeof(de);
    }
    return 0;
}

static int kd_create_generic(vfs_node_t *node, char *name, uint32_t mode) {
    kyrofs_inode_info_t *dir_ii = (kyrofs_inode_info_t *)node->ptr;
    if (kd_dir_find(&dir_ii->inode, name, NULL, NULL) == 0) return -1;
    int64_t new_ino_num = kd_alloc_inode();
    if (new_ino_num < 0) return -1;
    kyrofs_inode_t new_ino;
    memset(&new_ino, 0, sizeof(new_ino));
    new_ino.mode = mode;
    new_ino.nlinks = 1;
    if (kd_write_inode((uint64_t)new_ino_num, &new_ino) != 0) return -1;
    uint8_t ftype = (mode == KYROFS_MODE_DIR) ? 2 : 1;
    if (kd_dir_add(&dir_ii->inode, dir_ii->inode_num, name, (uint64_t)new_ino_num, ftype) != 0) return -1;
    return 0;
}

static int kd_vfs_mkdir(vfs_node_t *node, char *name, uint16_t mode) { (void)mode; return kd_create_generic(node, name, KYROFS_MODE_DIR); }
static int kd_vfs_create(vfs_node_t *node, char *name, uint16_t mode) { (void)mode; return kd_create_generic(node, name, KYROFS_MODE_FILE); }

static int kd_vfs_remove(vfs_node_t *node, char *name) {
    kyrofs_inode_info_t *dir_ii = (kyrofs_inode_info_t *)node->ptr;
    kyrofs_dirent_t de;
    if (kd_dir_find(&dir_ii->inode, name, &de, NULL) != 0) return -1;
    kyrofs_inode_t target;
    if (kd_read_inode(de.inode, &target) == 0) {
        kd_inode_free_all_blocks(&target);
        kd_write_inode(de.inode, &target);
        kd_free_inode(de.inode);
    }
    return kd_dir_remove(&dir_ii->inode, dir_ii->inode_num, name);
}

static int kd_vfs_rmdir(vfs_node_t *node, char *name, uint16_t mode) { (void)mode; return kd_vfs_remove(node, name); }

static int kd_vfs_stat(vfs_node_t *node, struct stat *stat_buf) {
    kyrofs_inode_info_t *ii = (kyrofs_inode_info_t *)node->ptr;
    memset(stat_buf, 0, sizeof(*stat_buf));
    stat_buf->st_size = (uint32_t)ii->inode.size;
    stat_buf->st_ino = (uint32_t)ii->inode_num;
    stat_buf->st_mode = (ii->inode.mode == KYROFS_MODE_DIR) ? S_IFDIR : S_IFREG;
    stat_buf->st_mode |= 0755;
    return 0;
}

static int kd_vfs_ioctl(vfs_node_t *node, int request, void *argp) { (void)node; (void)request; (void)argp; return -1; }

static vfs_node_t *kd_make_vfs_node(uint64_t ino_num, kyrofs_inode_t *ino, const char *name) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    strncpy(node->name, name, MAX_FILENAME_LEN - 1);
    node->inode = (uint32_t)ino_num;
    node->length = (uint32_t)ino->size;
    node->flags = (ino->mode == KYROFS_MODE_DIR) ? VFS_DIRECTORY : VFS_FILE;

    kyrofs_inode_info_t *ii = (kyrofs_inode_info_t *)kmalloc(sizeof(kyrofs_inode_info_t));
    if (!ii) { kfree(node); return NULL; }
    ii->inode = *ino;
    ii->inode_num = ino_num;
    ii->refcount = 1;
    ii->dirty = 0;
    node->ptr = ii;

    node->read = kd_vfs_read;
    node->write = kd_vfs_write;
    node->open = kd_vfs_open;
    node->close = kd_vfs_close;
    node->stat = kd_vfs_stat;
    node->ioctl = kd_vfs_ioctl;
    if (node->flags & VFS_DIRECTORY) {
        node->finddir = kd_vfs_finddir;
        node->readdir = kd_vfs_readdir;
        node->mkdir = kd_vfs_mkdir;
        node->create = kd_vfs_create;
        node->remove = kd_vfs_remove;
        node->rmdir = kd_vfs_rmdir;
    }
    return node;
}


int kyrofs_format(vfs_node_t *disk_device_node, uint32_t partition_lba, uint64_t partition_blocks) {
    klog(LOG_INFO, "KyroFS: formatting partition LBA %u, %llu blocks", partition_lba, (unsigned long long)partition_blocks);

    kyrofs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = KYROFS_MAGIC;
    sb.version = 2;
    sb.block_size = KYROFS_BLOCK_SIZE;
    sb.total_blocks = partition_blocks;
    sb.blocks_per_group = KYROFS_BLOCKS_PER_GROUP;
    sb.group_count = (uint32_t)((partition_blocks + KYROFS_BLOCKS_PER_GROUP - 1) / KYROFS_BLOCKS_PER_GROUP);
    if (sb.group_count == 0) sb.group_count = 1;
    sb.total_inodes = partition_blocks / 4;
    if (sb.total_inodes < 64) sb.total_inodes = 64;
    sb.inodes_per_group = (uint32_t)(sb.total_inodes / sb.group_count);
    sb.root_inode = KYROFS_ROOT_INODE;
    sb.journal_blocks = KYROFS_JOURNAL_SLOTS * 2;
    sb.journal_seq = 0;
    sb.clean_unmount = 1;

    uint64_t cursor = 1;
    sb.group_desc_block = cursor;
    cursor += 1;
    sb.journal_start = cursor;
    cursor += sb.journal_blocks;

    kyrofs_group_desc_t *groups = (kyrofs_group_desc_t *)kmalloc(sizeof(kyrofs_group_desc_t) * sb.group_count);
    if (!groups) return -1;
    memset(groups, 0, sizeof(kyrofs_group_desc_t) * sb.group_count);

    uint32_t per_inode_block = KYROFS_BLOCK_SIZE / sizeof(kyrofs_inode_t);
    uint64_t inode_table_blocks_per_group = (sb.inodes_per_group + per_inode_block - 1) / per_inode_block;

    uint64_t blocks_left = partition_blocks - cursor;
    for (uint32_t g = 0; g < sb.group_count; g++) {
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
        groups[g].group_inodes = sb.inodes_per_group;
        groups[g].free_blocks_in_group = (uint32_t)data_blocks;
        groups[g].free_inodes_in_group = sb.inodes_per_group;
        cursor += data_blocks;
        blocks_left = (partition_blocks > cursor) ? (partition_blocks - cursor) : 0;
    }

    sb.free_blocks = 0;
    sb.free_inodes = sb.total_inodes;
    for (uint32_t g = 0; g < sb.group_count; g++) sb.free_blocks += groups[g].free_blocks_in_group;

    uint8_t zero_block[KYROFS_BLOCK_SIZE];
    memset(zero_block, 0, sizeof(zero_block));
    ide_write_blocks_t wb;
    for (uint32_t g = 0; g < sb.group_count; g++) {
        wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)groups[g].block_bitmap_block, .count = 1, .buffer = zero_block };
        disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);
        wb.lba = partition_lba + (uint32_t)groups[g].inode_bitmap_block;
        disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);
        for (uint64_t i = 0; i < inode_table_blocks_per_group; i++) {
            wb.lba = partition_lba + (uint32_t)(groups[g].inode_table_block + i);
            disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);
        }
    }

    for (uint64_t i = 0; i < sb.journal_blocks; i++) {
        wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)(sb.journal_start + i), .count = 1, .buffer = zero_block };
        disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);
    }

    {
        uint8_t bm[KYROFS_BLOCK_SIZE];
        memset(bm, 0, sizeof(bm));
        bit_set(bm, 0);
        wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)groups[0].inode_bitmap_block, .count = 1, .buffer = bm };
        disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);
        groups[0].free_inodes_in_group--;
        sb.free_inodes--;
    }

    int64_t root_data_blk;
    {
        uint8_t bm[KYROFS_BLOCK_SIZE];
        memset(bm, 0, sizeof(bm));
        bit_set(bm, 0);
        wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)groups[0].block_bitmap_block, .count = 1, .buffer = bm };
        disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);
        groups[0].free_blocks_in_group--;
        sb.free_blocks--;
        root_data_blk = (int64_t)groups[0].data_block_start;
    }

    kyrofs_inode_t root_ino;
    memset(&root_ino, 0, sizeof(root_ino));
    root_ino.mode = KYROFS_MODE_DIR;
    root_ino.nlinks = 1;
    root_ino.extent_count = 1;
    root_ino.extents[0].start_block = (uint64_t)root_data_blk;
    root_ino.extents[0].length = 1;
    root_ino.blocks = 1;

    kyrofs_dirent_t dot;
    memset(&dot, 0, sizeof(dot));
    dot.inode = KYROFS_ROOT_INODE;
    dot.name_len = 1;
    dot.rec_len = sizeof(dot);
    dot.file_type = 2;
    strncpy(dot.name, ".", sizeof(dot.name) - 1);
    root_ino.size = sizeof(dot);

    uint8_t data_buf[KYROFS_BLOCK_SIZE];
    memset(data_buf, 0, sizeof(data_buf));
    memcpy(data_buf, &dot, sizeof(dot));
    wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)root_data_blk, .count = 1, .buffer = data_buf };
    disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);

    uint8_t inode_block[KYROFS_BLOCK_SIZE];
    memset(inode_block, 0, sizeof(inode_block));
    root_ino.crc32 = 0;
    root_ino.crc32 = kd_crc32(&root_ino, sizeof(root_ino));
    memcpy(inode_block, &root_ino, sizeof(root_ino));
    wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)groups[0].inode_table_block, .count = 1, .buffer = inode_block };
    disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);

    uint8_t gd_buf[KYROFS_BLOCK_SIZE];
    memset(gd_buf, 0, sizeof(gd_buf));
    for (uint32_t g = 0; g < sb.group_count; g++) {
        groups[g].crc32 = 0;
        groups[g].crc32 = kd_crc32(&groups[g], sizeof(kyrofs_group_desc_t));
    }
    memcpy(gd_buf, groups, sizeof(kyrofs_group_desc_t) * sb.group_count);
    wb = (ide_write_blocks_t){ .lba = partition_lba + (uint32_t)sb.group_desc_block, .count = 1, .buffer = gd_buf };
    disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);

    sb.crc32 = 0;
    sb.crc32 = kd_crc32(&sb, sizeof(sb));
    uint8_t sb_buf[KYROFS_BLOCK_SIZE];
    memset(sb_buf, 0, sizeof(sb_buf));
    memcpy(sb_buf, &sb, sizeof(sb));
    wb = (ide_write_blocks_t){ .lba = partition_lba + 0, .count = 1, .buffer = sb_buf };
    disk_device_node->ioctl(disk_device_node, IDE_IOCTL_WRITE_BLOCKS, &wb);

    kfree(groups);
    klog(LOG_INFO, "KyroFS: format complete. %llu groups, %llu total blocks, %llu inodes",
         (unsigned long long)sb.group_count, (unsigned long long)sb.total_blocks, (unsigned long long)sb.total_inodes);
    return 0;
}

int kyrofs_mount(vfs_node_t *mount_point, vfs_node_t *device_node) {
    if (!device_node || !device_node->ioctl) {
        klog(LOG_ERROR, "KyroFS: invalid device node for mount");
        return -1;
    }

    mutex_init(&kd_lock);

    uint32_t partition_lba = (uint32_t)(uintptr_t)device_node->ptr;

    uint8_t sb_buf[KYROFS_BLOCK_SIZE];
    ide_read_blocks_t rb = { .lba = partition_lba + 0, .count = 1, .buffer = sb_buf };
    if (device_node->ioctl(device_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        klog(LOG_ERROR, "KyroFS: failed to read superblock");
        return -1;
    }

    memcpy(&kd_sb, sb_buf, sizeof(kd_sb));
    if (kd_sb.magic != KYROFS_MAGIC) {
        klog(LOG_ERROR, "KyroFS: bad magic on partition, not a KyroFS volume");
        return -1;
    }

    uint32_t stored_crc = kd_sb.crc32;
    kyrofs_superblock_t tmp_sb = kd_sb;
    tmp_sb.crc32 = 0;
    if (kd_crc32(&tmp_sb, sizeof(tmp_sb)) != stored_crc) {
        klog(LOG_WARN, "KyroFS: superblock checksum mismatch, volume may be corrupt");
    }

    if (!kd_sb.clean_unmount) {
        klog(LOG_WARN, "KyroFS: unclean unmount detected, journal replay required");
    }

    kd_device = device_node;
    kd_part_lba = partition_lba;

    kd_groups = (kyrofs_group_desc_t *)kmalloc(sizeof(kyrofs_group_desc_t) * kd_sb.group_count);
    if (!kd_groups) return -1;
    uint8_t gd_buf[KYROFS_BLOCK_SIZE];
    if (kd_read_block(kd_sb.group_desc_block, gd_buf) != 0) { kfree(kd_groups); return -1; }
    memcpy(kd_groups, gd_buf, sizeof(kyrofs_group_desc_t) * kd_sb.group_count);

    kyrofs_journal_replay();

    kd_sb.clean_unmount = 0;
    kd_flush_superblock();

    kyrofs_inode_t root_ino;
    if (kd_read_inode(kd_sb.root_inode, &root_ino) != 0) {
        klog(LOG_ERROR, "KyroFS: failed to read root inode");
        return -1;
    }

    vfs_node_t *root_vfs = kd_make_vfs_node(kd_sb.root_inode, &root_ino, "/");
    if (!root_vfs) return -1;

    mount_point->flags |= VFS_MOUNTPOINT | VFS_DIRECTORY;
    mount_point->ptr = root_vfs->ptr;
    mount_point->length = root_vfs->length;
    mount_point->read = NULL;
    mount_point->write = NULL;
    mount_point->open = kd_vfs_open;
    mount_point->close = kd_vfs_close;
    mount_point->finddir = kd_vfs_finddir;
    mount_point->readdir = kd_vfs_readdir;
    mount_point->mkdir = kd_vfs_mkdir;
    mount_point->create = kd_vfs_create;
    mount_point->remove = kd_vfs_remove;
    mount_point->rmdir = kd_vfs_rmdir;
    mount_point->stat = kd_vfs_stat;
    mount_point->ioctl = kd_vfs_ioctl;
    kfree(root_vfs);

    klog(LOG_INFO, "KyroFS: mounted. total=%llu free=%llu blocks, inodes free=%llu",
         (unsigned long long)kd_sb.total_blocks, (unsigned long long)kd_sb.free_blocks, (unsigned long long)kd_sb.free_inodes);
    return 0;
}

int kyrofs_unmount(vfs_node_t *mount_point) {
    (void)mount_point;
    if (!kd_device) return 0;
    kd_sb.clean_unmount = 1;
    kd_flush_superblock();
    if (kd_groups) { kfree(kd_groups); kd_groups = NULL; }
    kd_device = NULL;
    kd_part_lba = 0;
    klog(LOG_INFO, "KyroFS: unmounted cleanly.");
    return 0;
}

static int kyrofs_mount_func(vfs_node_t *mount_point, vfs_node_t *device_node) {
    return kyrofs_mount(mount_point, device_node);
}

static struct filesystem_type kyrofs_filesystem = {
    .name = "kyrofs",
    .mount_func = kyrofs_mount_func,
    .next = NULL
};

void kyrofs_register(void) {
    register_filesystem(&kyrofs_filesystem);
    klog(LOG_INFO, "KyroFS v2 (journaled, extent-based) registered as 'kyrofs'.");
}
// 999 lines!!!