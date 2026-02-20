#ifndef KPM_H
#define KPM_H

#define PKG_DB_PATH "/var/lib/kpm/installed.txt"

// Header for each file entry in a .kpkg archive
#include <stdint.h>

typedef struct {
  char filename[256];
  uint32_t size;
  uint8_t is_meta; // 1 if meta file, 0 if data file
} kpkg_entry_t;

// install.c
int pkg_install_file(const char *path);

// fetch.c
int pkg_fetch(const char *pkg_name);

// remove.c
int pkg_remove(const char *pkg_name);

// list.c
int pkg_list();

#endif // KPM_H
