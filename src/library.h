// library.h - /sd/books scanning (collections = subfolders)
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char path[300];
    char name[96];
    bool is_dir;
    int  progress;      // 0..100, -1 unknown
    uint32_t size;      // metadata snapshot for the persistent index
    uint32_t mtime;
} LibItem;

/* items may be NULL: then only the entry count is returned (no state I/O) */
int lib_scan(const char* dir, LibItem* items, int max_items);

/* same scan without per-book progress (zero state-file opens): the library
   screen displays names only, so the boot path uses this variant */
int lib_scan_fast(const char* dir, LibItem* items, int max_items);
