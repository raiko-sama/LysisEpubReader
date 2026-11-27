#include "config.h"
#include "library.h"
#include "state.h"
#include "util.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>

static int cmp_items(const void* a, const void* b)
{
    const LibItem* x = (const LibItem*)a;
    const LibItem* y = (const LibItem*)b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static bool is_epub(const char* name)
{
    const char* d = strrchr(name, '.');
    if (!d) return false;
    return !strcasecmp(d, ".epub");
}

static int progress_for(const char* path)
{
    BookState st;
    if (st_book_get(path, &st) != 0) return -1;
    if (st.last_ch > 0 || st.last_pg > 0) {
        int est = 0;
        int done = 0;
        for (int i = 0; i < 256; i++) {
            if (st.page_counts[i] > 0) est += st.page_counts[i];
            if (i < st.furthest_ch && st.page_counts[i] > 0) done += st.page_counts[i];
            else if (i == st.furthest_ch) done += st.furthest_pg;
        }
        if (est > 0) return done * 100 / est;
        return 1;
    }
    return -1;
}

static void add(LibItem* items, int* n, int max, const char* path, const char* name,
                bool dir, bool with_progress)
{
    if (*n >= max) return;
    LibItem* it = &items[*n];
    snprintf(it->path, sizeof(it->path), "%s", path);
    snprintf(it->name, sizeof(it->name), "%s", name);
    it->is_dir = dir;
    /* The library screen shows names only: the UI scan skips the per-book
       state-file open (lib_scan_fast). */
    it->progress = (dir || !with_progress) ? -1 : progress_for(path);
    it->size = 0;
    it->mtime = 0;
    (*n)++;
}

static int scan_dir(const char* dir, LibItem* items, int max_items, bool with_progress)
{
    int n = 0;
    u_sd_lock();
    DIR* d = opendir(dir);
    if (!d) { u_sd_unlock(); return 0; }
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[300];
        u_path_join(path, sizeof(path), dir, de->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (items && n < max_items) add(items, &n, max_items, path, de->d_name, true, with_progress);
            else n++;
        } else if (is_epub(de->d_name)) {
            char name[96];
            snprintf(name, sizeof(name), "%s", de->d_name);
            u_strip_ext(name);
            if (items && n < max_items) add(items, &n, max_items, path, name, false, with_progress);
            else n++;
        }
    }
    closedir(d);
    if (items) {
        int sort_n = n < max_items ? n : max_items;
        if (sort_n > 0) qsort(items, sort_n, sizeof(LibItem), cmp_items);
    }
    u_sd_unlock();
    return n;
}

/* items may be NULL: the scan then only counts entries (max_items unused) */
int lib_scan(const char* dir, LibItem* items, int max_items)
{
    return scan_dir(dir, items, max_items, true);
}

/* UI scan: metadata only, zero state-file I/O (progress not displayed) */
int lib_scan_fast(const char* dir, LibItem* items, int max_items)
{
    return scan_dir(dir, items, max_items, false);
}


