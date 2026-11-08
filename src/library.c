#include "config.h"
#include "library.h"
#include "state.h"
#include "util.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>

#if OPT_LIB_INDEX
/* I1: persistent library index.
   File: magic "LYSX" | ver | n | sig | {crc32(path), size, mtime, progress,
   is_dir} x n | crc32(payload). `sig` is the CRC over the sorted entry
   metadata: if it matches, the progress values are taken from the index
   with ZERO state-file opens. */
#define LIBIDX_MAGIC 0x4C595358   /* "LYSX" */
typedef struct { int32_t magic; int32_t ver; int32_t n; uint32_t sig; } LibIdxHdr;
typedef struct { uint32_t crc; uint32_t size; uint32_t mtime; int32_t progress; uint8_t is_dir; } LibIdxEnt;
#endif

static int cmp_items(const void* a, const void* b)
{
    const LibItem* x = (const LibItem*)a;
    const LibItem* y = (const LibItem*)b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

int lib_cmp_items(const void* a, const void* b) { return cmp_items(a, b); }

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

static void add(LibItem* items, int* n, int max, const char* path, const char* name, bool dir)
{
    if (*n >= max) return;
    LibItem* it = &items[*n];
    snprintf(it->path, sizeof(it->path), "%s", path);
    snprintf(it->name, sizeof(it->name), "%s", name);
    it->is_dir = dir;
    it->progress = dir ? -1 : progress_for(path);
    it->size = 0;
    it->mtime = 0;
    (*n)++;
}

/* items may be NULL: the scan then only counts entries (max_items unused) */
int lib_scan(const char* dir, LibItem* items, int max_items)
{
    int n = 0;
    DIR* d = opendir(dir);
    if (!d) return 0;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[300];
        u_path_join(path, sizeof(path), dir, de->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (items && n < max_items) add(items, &n, max_items, path, de->d_name, true);
            else n++;
        } else if (is_epub(de->d_name)) {
            char name[96];
            snprintf(name, sizeof(name), "%s", de->d_name);
            u_strip_ext(name);
            if (items && n < max_items) add(items, &n, max_items, path, name, false);
            else n++;
        }
    }
    closedir(d);
    if (items) {
        int sort_n = n < max_items ? n : max_items;
        if (sort_n > 0) qsort(items, sort_n, sizeof(LibItem), cmp_items);
    }
    return n;
}

#if OPT_LIB_INDEX
static uint32_t lib_sig_of(const LibItem* items, int n)
{
    /* running CRC over the entry metadata, in the sorted order */
    uint32_t c = 0;
    for (int i = 0; i < n; i++) {
        uint32_t pc = u_crc32(items[i].path, strlen(items[i].path));
        c = u_crc32_core(c, (const uint8_t*)&pc, sizeof(pc));
        uint32_t fields[2] = { items[i].size, items[i].mtime };
        c = u_crc32_core(c, (const uint8_t*)fields, sizeof(fields));
        uint8_t d = items[i].is_dir ? 1 : 0;
        c = u_crc32_core(c, &d, 1);
    }
    return c;
}

static void lib_idx_path(const char* dir, char* out, int outlen)
{
    /* one index per scanned folder, keyed by the dir path */
    uint32_t id = u_crc32(dir, strlen(dir));
    snprintf(out, outlen, ST_DIR "/_libidx_%08x.dat", id);
}

int lib_scan_idx(const char* dir, LibItem* items, int max_items, bool* matched)
{
    if (matched) *matched = false;
    if (!items) return lib_scan(dir, NULL, 0);   /* count pass: metadata only */

    /* metadata-only fill first (no state I/O) */
    int n = 0;
    {
        DIR* d = opendir(dir);
        if (!d) return 0;
        struct dirent* de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char path[300];
            u_path_join(path, sizeof(path), dir, de->d_name);
            struct stat st;
            bool have = stat(path, &st) == 0;
            if (have && S_ISDIR(st.st_mode)) {
                if (n < max_items) {
                    snprintf(items[n].path, sizeof(items[n].path), "%s", path);
                    snprintf(items[n].name, sizeof(items[n].name), "%s", de->d_name);
                    items[n].is_dir = true;
                    items[n].progress = -1;
                    items[n].size = (uint32_t)st.st_size;
                    items[n].mtime = (uint32_t)st.st_mtime;
                    n++;
                }
            } else if (is_epub(de->d_name)) {
                if (n < max_items) {
                    char name[96];
                    snprintf(name, sizeof(name), "%s", de->d_name);
                    u_strip_ext(name);
                    snprintf(items[n].path, sizeof(items[n].path), "%s", path);
                    snprintf(items[n].name, sizeof(items[n].name), "%s", name);
                    items[n].is_dir = false;
                    items[n].progress = -1;
                    items[n].size = have ? (uint32_t)st.st_size : 0;
                    items[n].mtime = have ? (uint32_t)st.st_mtime : 0;
                    n++;
                }
            }
        }
        closedir(d);
        qsort(items, n, sizeof(LibItem), cmp_items);
    }

    uint32_t sig = lib_sig_of(items, n);
    char idxp[256];
    lib_idx_path(dir, idxp, sizeof(idxp));

    /* try the index */
    {
        FILE* f = fopen(idxp, "rb");
        if (f) {
            LibIdxHdr h;
            if (fread(&h, sizeof(h), 1, f) == 1 && h.magic == LIBIDX_MAGIC &&
                h.ver == 1 && h.n == n && h.sig == sig) {
                size_t pay = sizeof(LibIdxHdr) + (size_t)h.n * sizeof(LibIdxEnt);
                uint8_t* buf = (uint8_t*)u_alloc(pay);
                if (buf) {
                    fseek(f, 0, SEEK_SET);
                    if (fread(buf, 1, pay, f) == pay) {
                        uint32_t crc = 0;
                        if (fread(&crc, 4, 1, f) == 1 && crc == u_crc32(buf, pay)) {
                            const LibIdxEnt* e = (const LibIdxEnt*)(buf + sizeof(LibIdxHdr));
                            for (int i = 0; i < n; i++) {
                                if (!items[i].is_dir && e[i].progress >= 0) items[i].progress = e[i].progress;
                            }
                            if (matched) *matched = true;
                        }
                    }
                    free(buf);
                }
            }
            fclose(f);
            if (matched && *matched) return n;
        }
    }

    /* index miss or invalid: full scan with state reads + index rewrite */
    {
        for (int i = 0; i < n; i++) {
            if (!items[i].is_dir) items[i].progress = progress_for(items[i].path);
        }
        /* persist the index (atomic) */
        size_t entsz = (size_t)n * sizeof(LibIdxEnt);
        size_t pay = sizeof(LibIdxHdr) + entsz;
        uint8_t* buf = (uint8_t*)u_alloc(pay + 4);
        if (buf) {
            LibIdxHdr h;
            h.magic = LIBIDX_MAGIC;
            h.ver = 1;
            h.n = n;
            h.sig = sig;
            memcpy(buf, &h, sizeof(h));
            LibIdxEnt* e = (LibIdxEnt*)(buf + sizeof(h));
            for (int i = 0; i < n; i++) {
                e[i].crc = u_crc32(items[i].path, strlen(items[i].path));
                e[i].size = items[i].size;
                e[i].mtime = items[i].mtime;
                e[i].progress = items[i].progress;
                e[i].is_dir = items[i].is_dir ? 1 : 0;
            }
            uint32_t crc = u_crc32(buf, pay);
            memcpy(buf + pay, &crc, 4);
            u_file_write_atomic(idxp, buf, pay + 4);
            free(buf);
        }
    }
    return n;
}
#endif

int lib_scan_root(const char* root, LibItem* items, int max_items)
{
    int n = 0;
    if (!items || max_items <= 0) return 0;
    DIR* d = opendir(root);
    if (!d) return 0;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!is_epub(de->d_name)) continue;
        if (n >= max_items) break;
        char path[300];
        u_path_join(path, sizeof(path), root, de->d_name);
        struct stat st;
        bool have = stat(path, &st) == 0;
        if (have && S_ISDIR(st.st_mode)) continue;   /* no recursion at the root */
        char name[96];
        snprintf(name, sizeof(name), "%s", de->d_name);
        u_strip_ext(name);
        snprintf(items[n].path, sizeof(items[n].path), "%s", path);
        snprintf(items[n].name, sizeof(items[n].name), "%s", name);
        items[n].is_dir = false;
        items[n].progress = progress_for(path);
        items[n].size = have ? (uint32_t)st.st_size : 0;
        items[n].mtime = have ? (uint32_t)st.st_mtime : 0;
        n++;
    }
    closedir(d);
    return n;
}
