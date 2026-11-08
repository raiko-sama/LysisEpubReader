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

/* I1: scan with the persistent index; *matched=true when the index was
   valid and the progress values come from it (zero state-file opens) */
int lib_scan_idx(const char* dir, LibItem* items, int max_items, bool* matched);

/* EPUB files directly in `root` (outside the books dir): appended to the
   top-level listing so cards from other readers still show their books */
int lib_scan_root(const char* root, LibItem* items, int max_items);

/* sort order for LibItem arrays (folders first, then case-insensitive name) */
int lib_cmp_items(const void* a, const void* b);
