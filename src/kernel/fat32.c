#include "fat32.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"
#include "ide.h"
#include "vfs.h"

typedef struct {
    vfs_node_t *dev;
    uint32_t fat_begin_lba;
    uint32_t cluster_begin_lba;
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t bytes_per_cluster;
} fat32_fs_t;

typedef struct {
    fat32_fs_t *fs;
    uint32_t first_cluster;
    uint32_t size_bytes;
} fat32_file_info_t;

static int fat32_read_sector(fat32_fs_t *fs, uint32_t lba, uint8_t *buf) {
    ide_read_blocks_t rb = {.lba = lba, .count = 1, .buffer = buf};
    return fs->dev->ioctl(fs->dev, IDE_IOCTL_READ_BLOCKS, &rb);
}

static uint32_t fat32_next_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs || cluster < 2) return 0x0FFFFFFF;
    uint32_t fat_sector = fs->fat_begin_lba + ((cluster * 4) / 512);
    uint32_t fat_offset = (cluster * 4) % 512;

    uint8_t sec[512];
    if (fat32_read_sector(fs, fat_sector, sec) != 0) return 0x0FFFFFFF;

    uint32_t next = *(uint32_t *)(sec + fat_offset) & 0x0FFFFFFF;
    return next;
}

static int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, uint8_t *buf) {
    if (!fs || cluster < 2 || !buf) return -1;
    uint32_t lba = fs->cluster_begin_lba + (cluster - 2) * fs->sectors_per_cluster;
    ide_read_blocks_t rb = {.lba = lba, .count = fs->sectors_per_cluster, .buffer = buf};
    return fs->dev->ioctl(fs->dev, IDE_IOCTL_READ_BLOCKS, &rb);
}

static void fat32_format_83_name(const char *raw, char *out) {
    int i, j = 0;
    for (i = 0; i < 8 && raw[i] != ' '; i++) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') c += 32; // Lowercase
        out[j++] = c;
    }
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++) {
            char c = raw[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

static uint32_t fat32_vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !node->ptr || (node->flags & VFS_DIRECTORY)) return (uint32_t)-1;
    fat32_file_info_t *fi = (fat32_file_info_t *)node->ptr;
    fat32_fs_t *fs = fi->fs;

    if (offset >= fi->size_bytes) return 0;
    size_t to_read = (offset + size > fi->size_bytes) ? (fi->size_bytes - offset) : size;

    uint32_t cluster = fi->first_cluster;
    uint32_t cur_offset = 0;

    // Skip clusters until reaching offset
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && cur_offset + fs->bytes_per_cluster <= offset) {
        cur_offset += fs->bytes_per_cluster;
        cluster = fat32_next_cluster(fs, cluster);
    }

    uint8_t *clus_buf = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!clus_buf) return (uint32_t)-1;

    size_t bytes_done = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && bytes_done < to_read) {
        if (fat32_read_cluster(fs, cluster, clus_buf) != 0) break;

        size_t clus_off = (offset + bytes_done) - cur_offset;
        size_t avail = fs->bytes_per_cluster - clus_off;
        size_t chunk = (to_read - bytes_done < avail) ? (to_read - bytes_done) : avail;

        memcpy(buffer + bytes_done, clus_buf + clus_off, chunk);
        bytes_done += chunk;
        cur_offset += fs->bytes_per_cluster;
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(clus_buf);
    return (uint32_t)bytes_done;
}

static vfs_node_t *fat32_vfs_finddir(vfs_node_t *dir, char *name);
static int fat32_vfs_readdir(vfs_node_t *dir, uint32_t index, struct dirent *entry);

static vfs_node_t *fat32_create_node(fat32_fs_t *fs, const char *name, uint32_t cluster, uint32_t size, uint8_t attr) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(vfs_node_t));

    strncpy(node->name, name, MAX_FILENAME_LEN - 1);
    node->length = size;
    node->flags = (attr & FAT32_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    node->uid = 0;
    node->gid = 0;
    node->mode = (node->flags & VFS_DIRECTORY) ? 0755 : 0644;

    fat32_file_info_t *fi = (fat32_file_info_t *)kmalloc(sizeof(fat32_file_info_t));
    if (!fi) { kfree(node); return NULL; }
    fi->fs = fs;
    fi->first_cluster = cluster;
    fi->size_bytes = size;
    node->ptr = fi;

    node->read = fat32_vfs_read;
    if (node->flags & VFS_DIRECTORY) {
        node->finddir = fat32_vfs_finddir;
        node->readdir = fat32_vfs_readdir;
    }
    return node;
}

static vfs_node_t *fat32_vfs_finddir(vfs_node_t *dir, char *name) {
    if (!dir || !dir->ptr || !(dir->flags & VFS_DIRECTORY)) return NULL;
    fat32_file_info_t *fi = (fat32_file_info_t *)dir->ptr;
    fat32_fs_t *fs = fi->fs;

    uint32_t cluster = fi->first_cluster;
    uint8_t *clus_buf = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!clus_buf) return NULL;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(fs, cluster, clus_buf) != 0) break;

        fat32_dirent_t *d = (fat32_dirent_t *)clus_buf;
        uint32_t entries_per_clus = fs->bytes_per_cluster / sizeof(fat32_dirent_t);

        for (uint32_t i = 0; i < entries_per_clus; i++) {
            if ((uint8_t)d[i].name[0] == 0x00) { cluster = 0x0FFFFFFF; break; } // End
            if ((uint8_t)d[i].name[0] == 0xE5) continue; // Deleted
            if (d[i].attr == FAT32_ATTR_LFN) continue;

            char fname[64];
            fat32_format_83_name(d[i].name, fname);
            if (strcmp(fname, name) == 0) {
                uint32_t file_cluster = ((uint32_t)d[i].first_cluster_hi << 16) | d[i].first_cluster_lo;
                vfs_node_t *found = fat32_create_node(fs, fname, file_cluster, d[i].file_size, d[i].attr);
                kfree(clus_buf);
                return found;
            }
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(clus_buf);
    return NULL;
}

static int fat32_vfs_readdir(vfs_node_t *dir, uint32_t index, struct dirent *entry) {
    if (!dir || !dir->ptr || !entry || !(dir->flags & VFS_DIRECTORY)) return 0;
    fat32_file_info_t *fi = (fat32_file_info_t *)dir->ptr;
    fat32_fs_t *fs = fi->fs;

    uint32_t cluster = fi->first_cluster;
    uint8_t *clus_buf = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!clus_buf) return 0;

    uint32_t count = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(fs, cluster, clus_buf) != 0) break;

        fat32_dirent_t *d = (fat32_dirent_t *)clus_buf;
        uint32_t entries_per_clus = fs->bytes_per_cluster / sizeof(fat32_dirent_t);

        for (uint32_t i = 0; i < entries_per_clus; i++) {
            if ((uint8_t)d[i].name[0] == 0x00) { cluster = 0x0FFFFFFF; break; }
            if ((uint8_t)d[i].name[0] == 0xE5) continue;
            if (d[i].attr == FAT32_ATTR_LFN || (d[i].attr & FAT32_ATTR_VOLUME_ID)) continue;

            if (count == index) {
                char fname[64];
                fat32_format_83_name(d[i].name, fname);
                strncpy(entry->name, fname, sizeof(entry->name) - 1);
                entry->name[sizeof(entry->name) - 1] = '\0';
                entry->ino = ((uint32_t)d[i].first_cluster_hi << 16) | d[i].first_cluster_lo;
                kfree(clus_buf);
                return 1;
            }
            count++;
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(clus_buf);
    return 0;
}

int fat32_mount(vfs_node_t *mount_point, vfs_node_t *device_node) {
    if (!mount_point || !device_node || !device_node->ioctl) return -1;

    uint8_t sector0[512];
    ide_read_blocks_t rb = {.lba = 0, .count = 1, .buffer = sector0};
    if (device_node->ioctl(device_node, IDE_IOCTL_READ_BLOCKS, &rb) != 0) {
        klog(LOG_ERROR, "FAT32: Failed to read sector 0 from device.");
        return -1;
    }

    fat32_bpb_t *bpb = (fat32_bpb_t *)sector0;
    if (bpb->bytes_per_sector != 512 || bpb->sectors_per_cluster == 0 ||
        bpb->fat_size_32 == 0 || bpb->root_cluster < 2) {
        klog(LOG_ERROR, "FAT32: Device does not contain a valid FAT32 filesystem.");
        return -1;
    }

    fat32_fs_t *fs = (fat32_fs_t *)kmalloc(sizeof(fat32_fs_t));
    if (!fs) return -1;

    fs->dev = device_node;
    fs->fat_begin_lba = bpb->reserved_sector_count;
    fs->cluster_begin_lba = bpb->reserved_sector_count + (bpb->num_fats * bpb->fat_size_32);
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->root_cluster = bpb->root_cluster;
    fs->bytes_per_cluster = bpb->sectors_per_cluster * 512;
    if (fs->bytes_per_cluster == 0) {
        kfree(fs);
        return -1;
    }

    fat32_file_info_t *root_fi = (fat32_file_info_t *)kmalloc(sizeof(fat32_file_info_t));
    if (!root_fi) {
        kfree(fs);
        return -1;
    }
    root_fi->fs = fs;
    root_fi->first_cluster = fs->root_cluster;
    root_fi->size_bytes = 0;

    mount_point->flags |= VFS_DIRECTORY | VFS_MOUNTPOINT;
    mount_point->ptr = root_fi;
    mount_point->finddir = fat32_vfs_finddir;
    mount_point->readdir = fat32_vfs_readdir;
    mount_point->read = NULL;

    klog(LOG_INFO, "FAT32: Mounted successfully on %s (cluster size: %u bytes).",
         mount_point->name, fs->bytes_per_cluster);
    return 0;
}

void fat32_init(void) {
    static struct filesystem_type fat32_fs = {
        .name = "fat32",
        .mount_func = fat32_mount,
        .next = NULL
    };
    register_filesystem(&fat32_fs);
    klog(LOG_INFO, "FAT32: Driver registered with VFS.");
}
