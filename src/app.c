// app.c - Lysis Epub Reader: new UI implementation (from screenshots + Ghidra)
// Core services (EPUB, pagination, state, SD) remain hardware/back-end only.
#ifndef HOST_GUI
#include <Arduino.h>
#endif
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "config.h"
#include "bridge.h"
#include "epub.h"
#include "paginate.h"
#include "state.h"
#include "library.h"
#include "util.h"
#include "battery.h"
#include "boot_image.h"

// ---------------------------------------------------------------- screens
typedef enum {
    SCR_LIBRARY = 0, SCR_LIBMENU, SCR_READER, SCR_MENU, SCR_CHAPTERS,
    SCR_SETTINGS, SCR_CLOCK, SCR_ALARMSET, SCR_WIFI, SCR_SEARCH,
    SCR_RESULTS, SCR_GOTO, SCR_USB, SCR_POWEROFF, SCR_ALARMRING
} Screen;

static Screen g_scr = SCR_LIBRARY;
static bool   g_force = true;
static bool   g_font_dirty;
static bool   g_font_preview;
static uint32_t g_libmenu_guard;

const char* app_screen_name(void);
static void alarm_test_now(void);

enum { GEST_NONE = 0, GEST_TAP, GEST_HOLD, GEST_SWIPE_UP, GEST_SWIPE_DOWN,
       GEST_SWIPE_LEFT, GEST_SWIPE_RIGHT };

static void time_str(char* out, int n);

static bool furthest_ahead(void);

/* ---- lightweight perf telemetry (DEV_INJECT / benchmark builds) ---- */
enum { PERF_PAGE = 1, PERF_MENU, PERF_MENU_CLOSE, PERF_OPEN,
       PERF_LAYOUT, PERF_STATE, PERF_COUNT, PERF_REFRESH };
typedef struct {
    uint32_t kind, total_us, draw_us, pack_us, push_us, panel_us, aux;
} PerfEvent;
#define PERF_MAX 96
static PerfEvent g_perf[PERF_MAX];
static int g_perf_n;
static uint32_t g_evt_t0;
static int g_evt_kind;

static void perf_reset(void) { g_perf_n = 0; g_evt_kind = 0; }
static void perf_add(int kind, uint32_t total_us, uint32_t draw_us, uint32_t aux)
{
    brd_perf_refresh_t r;
    brd_perf_last_refresh(&r);
    PerfEvent* e = &g_perf[g_perf_n < PERF_MAX ? g_perf_n : PERF_MAX - 1];
    if (g_perf_n < PERF_MAX) g_perf_n++;
    e->kind = (uint32_t)kind;
    e->total_us = total_us;
    e->draw_us = draw_us;
    e->pack_us = r.pack_us;
    e->push_us = r.push_us;
    e->panel_us = r.panel_us;
    e->aux = aux;
}
static void perf_dump(void)
{
    char b[192];
    snprintf(b, sizeof(b), "PERF n=%d", g_perf_n); brd_log(b);
    for (int i = 0; i < g_perf_n; i++) {
        PerfEvent* e = &g_perf[i];
        snprintf(b, sizeof(b),
                 "PERF %lu %lu %lu %lu %lu %lu %lu",
                 (unsigned long)e->kind, (unsigned long)e->total_us,
                 (unsigned long)e->draw_us, (unsigned long)e->pack_us,
                 (unsigned long)e->push_us, (unsigned long)e->panel_us,
                 (unsigned long)e->aux);
        brd_log(b);
    }
}
static void perf_start(int kind) { g_evt_kind = kind; g_evt_t0 = (uint32_t)brd_us(); }

static int g_display_mode;

static bool ui_hit(int bx, int by, int bw, int bh, int x, int y)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// ---------------------------------------------------------------- theme
typedef struct { uint16_t bg, fg, dim, panel, border, accent; } Theme;
static Theme th;

static void theme_init(void)
{
    if (g_cfg.dark_mode) {
        th.bg = BRD_BLACK; th.fg = BRD_WHITE; th.dim = 0x8410;
        th.panel = 0x2104; th.border = 0x8410; th.accent = BRD_WHITE;
    } else {
        th.bg = BRD_WHITE; th.fg = BRD_BLACK; th.dim = 0x7BEF;
        th.panel = 0xE71C; th.border = 0x7BEF; th.accent = BRD_BLACK;
    }
}

// ---------------------------------------------------------------- UI primitives
static void ui_clear(void) { brd_clear(th.bg); }

static void ui_text(int x, int y, const char* s, uint16_t c, int font)
{
    brd_text(x, y, s, c, font);
}

static void ui_text_trunc(int x, int y, int w, const char* s, uint16_t c, int font)
{
    char buf[512];
    brd_text_fit(buf, sizeof(buf), s, w, font);
    brd_text(x, y, buf, c, font);
}

static void ui_center(const char* s, int y, int font, uint16_t c)
{
    int w = brd_text_w(s, font);
    brd_text((brd_width() - w) / 2, y, s, c, font);
}

static void ui_button(int x, int y, int w, int h, const char* label, bool active, int font)
{
    uint16_t fg = active ? th.bg : th.fg;
    uint16_t bg = active ? th.fg : th.bg;
    brd_fill_rect(x, y, w, h, bg);
    brd_draw_rect(x, y, w, h, th.fg);
    brd_draw_rect(x + 1, y + 1, w - 2, h - 2, th.fg);
    if (label && label[0]) {
        int tw = brd_text_w(label, font);
        int ty = y + (h - brd_font_h(font)) / 2;
        brd_text(x + (w - tw) / 2, ty, label, fg, font);
    }
}

static void ui_toggle(int x, int y, int w, int h, bool on)
{
    brd_draw_rect(x, y, w, h, th.border);
    uint16_t track = on ? th.fg : th.panel;
    uint16_t knob  = on ? th.bg : th.dim;
    brd_fill_rect(x + 4, y + 2, w - 8, h - 4, track);
    brd_fill_rect(x + 2, y + 4, w - 4, h - 8, track);
    int kd = h - 8;
    int kx = on ? x + w - kd - 4 : x + 4;
    brd_fill_rect(kx + 2, y + 4, kd - 4, kd, knob);
    brd_fill_rect(kx, y + 6, kd, kd - 4, knob);
}

static int battery_pct(void)
{
    return batt_sample_count() > 0 ? batt_percent() : brd_battery();
}

static void draw_bookmark_icon(int x, int y, bool on)
{
    const int w = 16, h = 18, notch = 6;
    if (on) {
        brd_fill_rect(x + 1, y + 1, w - 2, h - 2, th.fg);
        /* Cut the central V from the bottom edge: the tab stays open. */
        for (int i = 0; i < notch; i++) {
            int nw = notch - i;
            int yy = y + h - 1 - i;
            brd_fill_rect(x + w / 2 - nw / 2, yy, nw, 1, th.bg);
        }
    } else {
        brd_hline(x, y, w, th.fg);
        brd_line(x, y, x, y + h - 1, th.fg);
        brd_line(x + w - 1, y, x + w - 1, y + h - 1, th.fg);
        brd_line(x, y + h - 1, x + w / 2, y + h - 1 - notch, th.fg);
        brd_line(x + w - 1, y + h - 1, x + w / 2, y + h - 1 - notch, th.fg);
    }
}

static void ui_statusbar(const char* title, const char* page, bool bookmark, bool bm_on)
{
    int W = brd_width();
    int h = 32;
    brd_fill_rect(0, 0, W, h, th.panel);
    int b = battery_pct();
    int bx = 6, by = 7, bw = 24, bh = 16;
    brd_draw_rect(bx, by, bw, bh, th.fg);
    brd_fill_rect(bx + bw, by + 4, 3, 8, th.fg);
    if (b >= 0) {
        int fill = 20 * b / 100;
        if (fill > 0) brd_fill_rect(bx + 2, by + 2, fill, bh - 4, th.fg);
        char bs[8]; snprintf(bs, sizeof(bs), "%d%%", b);
        brd_text(36, 3, bs, th.fg, 2);
    }
    if (title && title[0]) {
        char tb[160];
        brd_text_fit(tb, sizeof(tb), title, W - 180, 2);
        int tw = brd_text_w(tb, 2);
        brd_text((W - tw) / 2, 2, tb, th.fg, 2);
    }
    if (page && page[0]) {
        int tw = brd_text_w(page, 2);
        brd_text((W - tw) / 2, 2, page, th.fg, 2);
    }
    char t[16]; time_str(t, sizeof(t));
    int tw = brd_text_w(t, 2);
    int tx = W - 8 - tw;
    brd_text(tx, 3, t, th.fg, 2);
    if (bookmark) draw_bookmark_icon(tx - 30, (h - 18) / 2, bm_on);
    brd_hline(0, h, W, th.border);
}

static void time_str(char* out, int n)
{
    int y, mo, d, h, mi, s;
    if (brd_get_localtime(&y, &mo, &d, &h, &mi, &s))
        snprintf(out, n, "%02d:%02d", h, mi);
    else
        snprintf(out, n, "--:--");
}

static void ui_list_row(int x, int y, int w, int h, const char* text, const char* right,
                        bool selected, int font)
{
    if (selected) brd_fill_rect(x, y, w, h, th.panel);
    brd_hline(x, y + h, w, th.border);
    int fh = brd_font_h(font);
    int ty = y + (h - fh) / 2;
    int rw = (right && right[0]) ? brd_text_w(right, font) : 0;
    ui_text_trunc(x + 42, ty, w - 54 - rw, text, th.fg, font);
    if (rw) brd_text(x + w - 12 - rw, ty, right, th.dim, font);
}

static void ui_busy(const char* msg)
{
    brd_log("BUSY begin");
    int W = brd_width(), H = brd_height();
    int y = H / 2 - 45;
    brd_fill_rect(0, y, W, 90, th.bg);
    brd_hline(0, y, W, th.border);
    brd_hline(0, y + 89, W, th.border);
    ui_center(msg, H / 2 - brd_font_h(2) / 2, 2, th.fg);
    brd_log("BUSY refresh");
    brd_refresh_region(0, y, W, 90, 2);
    brd_log("BUSY done");
}

// ---------------------------------------------------------------- data
typedef struct {
    char path[300];
    char title[96];
} Entry;

static Entry* g_entries;
static int    g_nitems;
static int    g_scroll;

/* Metadata cache: opening every EPUB on every boot is the main library
   latency source. Titles are immutable for a given path; cache them on SD. */
#define TC_MAX 128
static char g_tc_path[TC_MAX][300];
static char g_tc_title[TC_MAX][96];
static int  g_tc_n;
static bool g_tc_dirty;

static void title_cache_swap(int a, int b)
{
    char p[sizeof(g_tc_path[0])];
    char t[sizeof(g_tc_title[0])];
    memcpy(p, g_tc_path[a], sizeof(p));
    memcpy(t, g_tc_title[a], sizeof(t));
    memcpy(g_tc_path[a], g_tc_path[b], sizeof(p));
    memcpy(g_tc_title[a], g_tc_title[b], sizeof(t));
    memcpy(g_tc_path[b], p, sizeof(p));
    memcpy(g_tc_title[b], t, sizeof(t));
}

static void title_cache_load(void)
{
    g_tc_n = 0;
    FILE* f = fopen(ST_DIR "/_titles.txt", "r");
    if (!f) return;
    char line[420];
    while (g_tc_n < TC_MAX && fgets(line, sizeof(line), f)) {
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab++ = 0;
        char* nl = strchr(tab, '\n');
        if (nl) *nl = 0;
        snprintf(g_tc_path[g_tc_n], sizeof(g_tc_path[0]), "%s", line);
        snprintf(g_tc_title[g_tc_n], sizeof(g_tc_title[0]), "%s", tab);
        g_tc_n++;
    }
    fclose(f);
    for (int i = 1; i < g_tc_n; i++) {
        int j = i;
        while (j > 0 && strcmp(g_tc_path[j - 1], g_tc_path[j]) > 0) {
            title_cache_swap(j - 1, j);
            j--;
        }
    }
}

static const char* title_cache_find(const char* path)
{
    int lo = 0, hi = g_tc_n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(g_tc_path[mid], path);
        if (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid;
        else return g_tc_title[mid];
    }
    return NULL;
}

static void title_cache_add(const char* path, const char* title)
{
    if (g_tc_n >= TC_MAX) return;
    int pos = 0;
    while (pos < g_tc_n && strcmp(g_tc_path[pos], path) < 0) pos++;
    if (pos < g_tc_n && !strcmp(g_tc_path[pos], path)) {
        snprintf(g_tc_title[pos], sizeof(g_tc_title[0]), "%s", title);
        g_tc_dirty = true;
        return;
    }
    if (pos < g_tc_n) {
        memmove(&g_tc_path[pos + 1], &g_tc_path[pos],
                sizeof(g_tc_path[0]) * (size_t)(g_tc_n - pos));
        memmove(&g_tc_title[pos + 1], &g_tc_title[pos],
                sizeof(g_tc_title[0]) * (size_t)(g_tc_n - pos));
    }
    snprintf(g_tc_path[pos], sizeof(g_tc_path[0]), "%s", path);
    snprintf(g_tc_title[pos], sizeof(g_tc_title[0]), "%s", title);
    g_tc_n++;
    g_tc_dirty = true;
}

static void title_cache_save(void)
{
    if (!g_tc_dirty) return;
    FILE* f = fopen(ST_DIR "/_titles.txt", "w");
    if (!f) return;
    for (int i = 0; i < g_tc_n; i++)
        fprintf(f, "%s\t%s\n", g_tc_path[i], g_tc_title[i]);
    fclose(f);
    g_tc_dirty = false;
}

static int cmp_entry(const void* a, const void* b)
{
    const Entry* x = (const Entry*)a;
    const Entry* y = (const Entry*)b;
    return strcasecmp(x->title, y->title);
}

static void library_scan(void)
{
    int cap = lib_scan(BOOKS_DIR, NULL, 0);
    if (cap < 4) cap = 4;
    LibItem* tmp = (LibItem*)u_alloc((size_t)cap * sizeof(LibItem));
    int n = tmp ? lib_scan(BOOKS_DIR, tmp, cap) : 0;
    g_entries = (Entry*)u_alloc((size_t)(n ? n : 1) * sizeof(Entry));
    g_nitems = 0;
    title_cache_load();
    if (!tmp || !g_entries) { free(tmp); return; }
    for (int i = 0; i < n; i++) {
        if (tmp[i].is_dir) continue;
        Entry* e = &g_entries[g_nitems];
        snprintf(e->path, sizeof(e->path), "%s", tmp[i].path);
        snprintf(e->title, sizeof(e->title), "%s", tmp[i].name);
        const char* cached = title_cache_find(tmp[i].path);
        if (cached && cached[0]) {
            snprintf(e->title, sizeof(e->title), "%s", cached);
        } else {
            Epub* ep = epub_open(tmp[i].path);
            if (ep) {
                const char* t = epub_title(ep);
                if (t && t[0]) snprintf(e->title, sizeof(e->title), "%s", t);
                epub_close(ep);
            }
            title_cache_add(tmp[i].path, e->title);
        }
        g_nitems++;
    }
    free(tmp);
    title_cache_save();
    if (g_nitems > 1) qsort(g_entries, (size_t)g_nitems, sizeof(Entry), cmp_entry);
    g_scroll = 0;
}

// ---------------------------------------------------------------- reader
typedef struct {
    Epub* ep;
    char  path[300];
    char  title[96];
    BookState bs;
    int   chapter, page;
    char* text;
    size_t text_len;
    Layout* lay;
} Reader;
static Reader R;
static bool g_reader_full = true;
static bool g_count_active;
static int g_partial_count;
static uint32_t g_last_ghost_ms;
static bool g_state_dirty;
static uint32_t g_state_dirty_at;
static int g_search_origin_ch;
static int g_search_origin_pg;
static bool g_search_origin_valid;

static void reader_state_flush(void)
{
    if (!g_state_dirty) return;
    uint32_t wt0 = (uint32_t)brd_us();
    st_book_put(R.path, &R.bs);
    perf_add(PERF_STATE, (uint32_t)brd_us() - wt0, 0, 0);
    g_state_dirty = false;
}
static int  g_count_ch;
static uint32_t g_count_pause_until;

static int reader_font_id(void)
{
    static const int map[6] = { 6, 7, 8, 9, 10, 14 };
    int i = g_cfg.rd_font;
    if (i < 0) i = 0;
    if (i > 5) i = 5;
    return map[i];
}

static int reading_layout_key(void)
{
    return (reader_font_id() << 16) | ((g_cfg.rd_spacing & 3) << 8) |
           ((g_cfg.rd_margins & 3) << 4) | ((brd_width() / 8) & 15);
}

static const char* pct_str(void)
{
    static const char* p[6] = { "80%", "100%", "140%", "160%", "200%", "120%" };
    int i = g_cfg.rd_font;
    if (i < 0) i = 0;
    if (i > 5) i = 5;
    return p[i];
}

static void font_step(int dir)
{
    static const int order[6] = { 0, 1, 5, 2, 3, 4 };
    int pos = 1;
    for (int i = 0; i < 6; i++) if (order[i] == g_cfg.rd_font) { pos = i; break; }
    int next = pos + (dir < 0 ? -1 : 1);
    if (next < 0 || next > 5) return;
    g_cfg.rd_font = order[next];
}

static int reader_margin(void) { return g_cfg.rd_margins == 0 ? 28 : (g_cfg.rd_margins == 2 ? 60 : 44); }

static void img_cache_clear(void);
static void search_cache_clear(void);

static void reader_free_layout(void)
{
    if (R.lay) { pg_free(R.lay); R.lay = NULL; }
}

static void reader_free(void)
{
    reader_state_flush();
    g_count_active = false;
    reader_free_layout();
    if (R.text) { free(R.text); R.text = NULL; }
    if (R.ep) { epub_close(R.ep); R.ep = NULL; }
    R.text_len = 0;
    img_cache_clear();
    search_cache_clear();
}

static bool reader_load_chapter(int ch)
{
    if (!R.ep) return false;
    int n = epub_chapter_count(R.ep);
    if (ch < 0 || ch >= n) return false;
    brd_cpu_freq(240);
    ui_busy("Opening chapter...");
    brd_log("CH free");
    if (R.text) { free(R.text); R.text = NULL; }
    reader_free_layout();
    brd_log("CH text start");
    R.text = epub_chapter_text(R.ep, ch, &R.text_len);
    brd_log("CH text done");
    if (!R.text) { brd_log("reader: chapter text null"); brd_cpu_freq(80); return false; }
    R.chapter = ch;
    R.page = 0;
    int W = brd_width(), H = brd_height();
    int hdr = 32;
    int margin = reader_margin();
    brd_log("CH layout start");
    R.lay = pg_layout(R.text, R.text_len, W - 2 * margin, H - hdr - 40,
                      reader_font_id(), g_cfg.rd_spacing * 4);
    brd_log("CH layout done");
    if (!R.lay) { brd_log("reader: layout null"); brd_cpu_freq(80); return false; }
    brd_cpu_freq(80);
    return true;
}

static void book_count_start(void)
{
    if (!R.ep) return;
    if (R.bs.lkey == reading_layout_key() && R.bs.page_counts[R.chapter] > 0) {
        g_count_active = false;
        return;
    }
    g_count_ch = 0;
    g_count_active = true;
}

/* Count one chapter per app-loop iteration. The page is interactive
   immediately; this also avoids the long blocking pass that starved the
   idle task on large books. */
static void book_count_step(void)
{
    if (!g_count_active || !R.ep) return;
    if ((int32_t)(g_count_pause_until - brd_millis()) > 0) return;
    int nch = epub_chapter_count(R.ep);
    if (g_count_ch >= nch || g_count_ch >= 256) {
        g_count_active = false;
        R.bs.lkey = reading_layout_key();
        st_book_put(R.path, &R.bs);
        g_force = true;
        return;
    }
    int c = g_count_ch++;
    if (c == R.chapter && R.lay) {
        R.bs.page_counts[c] = R.lay->npages;
        return;
    }
    size_t tl = 0;
    uint32_t ct0 = (uint32_t)brd_us();
    char* t = epub_chapter_text(R.ep, c, &tl);
    if (!t) { R.bs.page_counts[c] = 1; brd_delay(1); return; }
    int W = brd_width(), H = brd_height();
    Layout* l = pg_layout(t, tl, W - 2 * reader_margin(), H - 32 - 40,
                          reader_font_id(), g_cfg.rd_spacing * 4);
    R.bs.page_counts[c] = l ? l->npages : 1;
    if (l) pg_free(l);
    free(t);
    perf_add(PERF_COUNT, (uint32_t)brd_us() - ct0, 0, (uint32_t)c);
    brd_delay(1);
}

static void reader_open(const char* path)
{
    uint32_t perf_t0 = (uint32_t)brd_us();
    g_search_origin_valid = false;
    brd_log("reader: opening");
    brd_cpu_freq(240);
    ui_busy("Opening book...");
    reader_free();
    R.ep = epub_open(path);
    if (!R.ep) {
        brd_log("reader: epub_open failed");
        ui_busy("Failed to open!");
        brd_delay(1200);
        g_scr = SCR_LIBRARY;
        g_force = true;
        return;
    }
    snprintf(R.path, sizeof(R.path), "%s", path);
    const char* t = epub_title(R.ep);
    snprintf(R.title, sizeof(R.title), "%s", (t && t[0]) ? t : u_basename(path));
    st_book_get(path, &R.bs);
    int ch = R.bs.last_ch;
    if (ch < 0 || ch >= epub_chapter_count(R.ep)) ch = 0;
    if (!reader_load_chapter(ch)) {
        brd_log("reader: load_chapter failed");
        ui_busy("Failed to open!");
        brd_delay(1200);
        reader_free();
        g_scr = SCR_LIBRARY;
        g_force = true;
        return;
    }
    R.page = R.bs.last_pg;
    if (R.page < 0 || R.page >= R.lay->npages) R.page = 0;
    if (R.bs.lkey != reading_layout_key() || R.bs.page_counts[R.chapter] <= 0)
        book_count_start();
    else
        R.bs.page_counts[R.chapter] = R.lay->npages;
    R.bs.last_pg = R.page;
    if (R.chapter > R.bs.furthest_ch ||
        (R.chapter == R.bs.furthest_ch && R.page > R.bs.furthest_pg)) {
        R.bs.furthest_ch = R.chapter; R.bs.furthest_pg = R.page;
    }
    st_book_put(R.path, &R.bs);
    g_state_dirty = false;
    perf_add(PERF_OPEN, (uint32_t)brd_us() - perf_t0, 0, (uint32_t)epub_chapter_count(R.ep));
    g_reader_full = true;
    g_scr = SCR_READER;
    g_force = true;
}

static int bookmark_find(int ch, int pg)
{
    for (int i = 0; i < R.bs.bm_count; i++)
        if (R.bs.bm[i].ch == ch && R.bs.bm[i].pg == pg) return i;
    return -1;
}

static void reader_progress(void)
{
    if (R.chapter > R.bs.furthest_ch ||
        (R.chapter == R.bs.furthest_ch && R.page > R.bs.furthest_pg)) {
        R.bs.furthest_ch = R.chapter;
        R.bs.furthest_pg = R.page;
    }
    R.bs.last_ch = R.chapter;
    R.bs.last_pg = R.page;
    g_state_dirty = true;
    g_state_dirty_at = brd_millis();
}

static void bookmark_toggle(void)
{
    int i = bookmark_find(R.chapter, R.page);
    if (i >= 0) {
        for (int k = i; k + 1 < R.bs.bm_count; k++) R.bs.bm[k] = R.bs.bm[k + 1];
        R.bs.bm_count--;
    } else if (R.bs.bm_count < BM_MAX) {
        R.bs.bm[R.bs.bm_count].ch = R.chapter;
        R.bs.bm[R.bs.bm_count].pg = R.page;
        R.bs.bm_count++;
    }
    st_book_put(R.path, &R.bs);
    g_state_dirty = false;
}

static void total_pages(int* total, int* before)
{
    int sum = 0, pre = 0;
    int n = R.ep ? epub_chapter_count(R.ep) : 256;
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        if (i < R.chapter && R.bs.page_counts[i] > 0) pre += R.bs.page_counts[i];
        if (R.bs.page_counts[i] > 0) sum += R.bs.page_counts[i];
    }
    if (sum <= 0 && R.lay) { sum = R.lay->npages; pre = 0; }
    *total = sum > 0 ? sum : 1;
    *before = pre;
}

/* ---------------------------------------------------------------- images */
#define IMG_CACHE_N 4
static struct { char key[240]; char* data; size_t len; } g_imgc[IMG_CACHE_N];
static int g_imgc_rot;

static void img_cache_clear(void)
{
    for (int i = 0; i < IMG_CACHE_N; i++) {
        free(g_imgc[i].data); g_imgc[i].data = NULL; g_imgc[i].len = 0; g_imgc[i].key[0] = 0;
    }
    g_imgc_rot = 0;
}

#define SEARCH_CACHE_N 3
#define SEARCH_CACHE_MAX (256u * 1024u)
static struct { int ch; char* data; size_t len; uint32_t tick; } g_sc[SEARCH_CACHE_N];
static uint32_t g_sc_tick;

static void search_cache_clear(void)
{
    for (int i = 0; i < SEARCH_CACHE_N; i++) {
        free(g_sc[i].data);
        g_sc[i].data = NULL;
        g_sc[i].len = 0;
        g_sc[i].ch = -1;
        g_sc[i].tick = 0;
    }
    g_sc_tick = 0;
}

static char* search_cache_get(int ch, size_t* len)
{
    for (int i = 0; i < SEARCH_CACHE_N; i++) {
        if (g_sc[i].data && g_sc[i].ch == ch) {
            g_sc[i].tick = ++g_sc_tick;
            *len = g_sc[i].len;
            return g_sc[i].data;
        }
    }
    return NULL;
}

static bool search_cache_put(int ch, char* text, size_t len)
{
    if (!text || len == 0 || len > SEARCH_CACHE_MAX) return false;
    int slot = 0;
    for (int i = 0; i < SEARCH_CACHE_N; i++) {
        if (!g_sc[i].data) { slot = i; break; }
        if (g_sc[i].tick < g_sc[slot].tick) slot = i;
    }
    free(g_sc[slot].data);
    g_sc[slot].data = text;
    g_sc[slot].len = len;
    g_sc[slot].ch = ch;
    g_sc[slot].tick = ++g_sc_tick;
    return true;
}

static char* img_fetch(const char* href, size_t* len)
{
    char key[240];
    snprintf(key, sizeof(key), "%s|%d|%s", R.path, R.chapter, href);
    for (int i = 0; i < IMG_CACHE_N; i++) {
        if (g_imgc[i].data && !strcmp(g_imgc[i].key, key)) {
            *len = g_imgc[i].len;
            return g_imgc[i].data;
        }
    }
    char* img = epub_image_from(R.ep, R.chapter, href, len);
    if (!img) img = epub_image(R.ep, href, len);
    if (!img) {
        char alt[160];
        snprintf(alt, sizeof(alt), "OEBPS/%s", href);
        img = epub_image(R.ep, alt, len);
    }
    if (!img) {
        char m[280];
        snprintf(m, sizeof(m), "IMG miss %s", href);
        brd_log(m);
        return NULL;
    }
    {
        char m[280];
        snprintf(m, sizeof(m), "IMG ok %s len=%u", href, (unsigned)*len);
        brd_log(m);
    }
    int slot = g_imgc_rot++ % IMG_CACHE_N;
    free(g_imgc[slot].data);
    snprintf(g_imgc[slot].key, sizeof(g_imgc[slot].key), "%s", key);
    g_imgc[slot].data = img;
    g_imgc[slot].len = *len;
    return img;
}

static bool cover_page(void)
{
    if (R.chapter != 0 || R.page != 0 || !R.text) return false;
    if (u_stristr(R.text, "Copertina") || u_stristr(R.text, "Cover")) return true;
    const char* t = epub_chapter_title(R.ep, 0);
    return t && (u_stristr(t, "cover") || u_stristr(t, "copertina"));
}

static void reader_draw(void)
{
    if (!R.ep || !R.lay) return;
    brd_cpu_freq(240);
    int W = brd_width(), H = brd_height();
    ui_clear();
    int total = 1, pre = 0;
    total_pages(&total, &pre);
    char pg[32];
    snprintf(pg, sizeof(pg), "%d/%d", pre + R.page + 1, total);
    ui_statusbar(NULL, pg, true, bookmark_find(R.chapter, R.page) >= 0);
    int first = R.lay->page_first[R.page];
    int last = R.lay->page_first[R.page + 1];
    bool empty_page = first >= last;
    bool cover = cover_page();
    if (R.chapter == 0 && R.page == 0) {
        char m[96];
        snprintf(m, sizeof(m), "P1 cover=%d images=%d", cover, g_cfg.images);
        brd_log(m);
    }
    if (cover && g_cfg.images) {
        size_t clen = 0;
        char* cov = epub_cover(R.ep, &clen);
        if (cov) {
            char m[128];
            snprintf(m, sizeof(m), "COVER len=%u hdr=%02x%02x%02x%02x",
                     (unsigned)clen, (unsigned char)cov[0], (unsigned char)cov[1],
                     (unsigned char)cov[2], (unsigned char)cov[3]);
            brd_log(m);
            int dw = 0, dh = 0;
            uint16_t* px = brd_img_decode((const uint8_t*)cov, clen, W - 8, H - 40, th.bg, &dw, &dh);
            free(cov);
            if (px) {
                char dm[64];
                snprintf(dm, sizeof(dm), "COVER decode %dx%d", dw, dh);
                brd_log(dm);
                brd_draw_rgb565((W - dw) / 2, 30 + (H - 30 - dh) / 2, dw, dh, px);
                free(px);
                brd_refresh(0);
                g_reader_full = false;
                return;
            }
            else brd_log("COVER decode fail");
        }
        else brd_log("COVER null");
    }
    if (empty_page) {
        ui_center(R.title, H / 2 - 28, 2, th.fg);
        const char* auth = epub_author(R.ep);
        if (auth && auth[0]) ui_center(auth, H / 2 + 16, 1, th.dim);
        brd_refresh(0);
        return;
    }
    int margin = reader_margin();
    int hdr = 32;
    int img_h = (H - hdr - 40) / 3;
    if (img_h < 60) img_h = 60;
    char line[1024];
    int y = hdr + 10;
    int f = reader_font_id();
    int lh = R.lay->line_h;
    for (int i = first; i < last && i < R.lay->nlines; i++) {
        PgLine* ln = &R.lay->lines[i];
        size_t n = ln->end - ln->start;
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, R.text + ln->start, n);
        line[n] = 0;
        if (ln->flags & PGL_IMAGE) {
            int len2 = (int)n - 2;
            if (len2 > 0) { memmove(line, line + 1, (size_t)len2); line[len2] = 0; }
            else line[0] = 0;
            if (g_cfg.images && line[0]) {
                size_t ilen = 0;
                char* img = img_fetch(line, &ilen);
                if (img) {
                    int dw = 0, dh = 0;
                    uint16_t* px = brd_img_decode((const uint8_t*)img, ilen, W - 2 * margin,
                                                  img_h, th.bg, &dw, &dh);
                    if (px) {
                        brd_draw_rgb565(margin + (W - 2 * margin - dw) / 2, y, dw, dh, px);
                        free(px);
                    } else {
                        char dm[280];
                        snprintf(dm, sizeof(dm), "IMG decode fail %s len=%u hdr=%02x%02x%02x%02x",
                                 line, (unsigned)ilen, (unsigned char)img[0], (unsigned char)img[1],
                                 (unsigned char)img[2], (unsigned char)img[3]);
                        brd_log(dm);
                    }
                }
            }
            y += img_h + 8;
        } else if (ln->flags & PGL_HEAD) {
            brd_text(margin, y, line, th.fg, 2);
            y += lh;
        } else {
            brd_text(margin, y, line, th.fg, f);
            y += lh;
        }
    }
    int mode = g_display_mode == 1 ? 2 : (g_display_mode == 2 ? 0 : 1);
    if (g_reader_full) {
        brd_refresh(0);
        g_reader_full = false;
        g_partial_count = 0;
    } else if (mode == 1) {
        brd_refresh_region(0, 0, W, 30, 1);
        brd_refresh_region(0, 30, W, H - 30, 1);
        g_partial_count++;
    } else {
        brd_refresh(mode);
        g_partial_count = 0;
    }
    brd_cpu_freq(80);   /* idle/reading cadence is set by app_loop */
}

// ---------------------------------------------------------------- library drawing
static void library_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar(NULL, NULL, false, false);
    int rows = 11;
    int y0 = 56;
    int rh = 75;
    if (g_scroll >= g_nitems) g_scroll = 0;
    int page = g_nitems - g_scroll;
    if (page > rows) page = rows;
    for (int i = 0; i < page; i++) {
        Entry* e = &g_entries[g_scroll + i];
        char label[160];
        snprintf(label, sizeof(label), "%d. %s", g_scroll + i + 1, e->title);
        ui_list_row(0, y0 + i * rh, W, rh, label, "", false, 2);
    }
    if (g_nitems == 0) ui_center("No books found", H / 2, 1, th.dim);
    int pages = (g_nitems + rows - 1) / rows;
    if (pages > 1) {
        char p[16];
        snprintf(p, sizeof(p), "%d/%d >", g_scroll / rows + 1, pages);
        ui_center(p, H - 34, 2, th.fg);
    }
    brd_refresh(0);
}

static void library_input(int g, int x, int y)
{
    int W = brd_width(), H = brd_height();
    int rows = 11;
    int y0 = 56;
    int rh = 75;
    brd_log("DBG library_input entered");
    if (g == GEST_SWIPE_UP) { brd_log("DBG library -> libmenu"); g_scr = SCR_LIBMENU; g_libmenu_guard = brd_millis() + 500; g_force = true; return; }
    if (g != GEST_TAP) return;
    if (y > H - 38) {
        g_scroll += rows;
        if (g_scroll >= g_nitems) g_scroll = 0;
        g_force = true;
        return;
    }
    if (g_scroll >= g_nitems) g_scroll = 0;
    int page = g_nitems - g_scroll;
    if (page > rows) page = rows;
    for (int i = 0; i < page; i++) {
        if (x >= 0 && x < W && y >= y0 + i * rh && y < y0 + (i + 1) * rh) {
            reader_open(g_entries[g_scroll + i].path);
            return;
        }
    }
}

static void boot_screen_draw(void)
{
    int W = brd_width(), H = brd_height();
    brd_fill_rect(0, 0, W, H, 0xFFFF);
    int iw = 480, ih = 360;
    int block = ih + 50 + brd_font_h(4) + 50 + brd_font_h(2);
    int y = (H - block) / 2;
    if (y < 40) y = 40;
    int dw = 0, dh = 0;
    uint16_t* px = brd_img_decode(boot_img_png, boot_img_png_len, iw, ih, 0xFFFF, &dw, &dh);
    if (px) {
        brd_draw_rgb565((W - dw) / 2, y, dw, dh, px);
        free(px);
    }
    const char* title = "Lysis epub reader";
    const char* sub = "da Luca con <3";
    int ty = y + ih + 50;
    int tx = (W - brd_text_w(title, 4)) / 2;
    brd_text(tx, ty, title, 0x0000, 4);
    brd_text(tx + 1, ty, title, 0x0000, 4);
    brd_text(tx, ty + 1, title, 0x0000, 4);
    brd_text(tx + 1, ty + 1, title, 0x0000, 4);
    int sx = (W - brd_text_w(sub, 2)) / 2;
    brd_text(sx, ty + 50, sub, 0x0000, 2);
    brd_refresh(0);
}

static void poweroff_now(void)
{
    reader_state_flush();
    batt_flush();
    st_save_cfg();
    boot_screen_draw();
    brd_delay(250);
    brd_poweroff();
}

// ---------------------------------------------------------------- library menu
static void libmenu_draw(void)
{
    int W = brd_width(), H = brd_height();
    int y0 = H - 174;
    brd_fill_rect(0, y0, W, H - y0, th.bg);
    brd_hline(0, y0 - 1, W, th.border);
    int bh = 50, bw = (W - 32 - 16) / 3;
    ui_button(16, y0 + 6, bw, bh, "Power off", false, 2);
    ui_button(16 + bw + 8, y0 + 6, bw, bh, "USB disk", false, 2);
    ui_button(16 + 2 * (bw + 8), y0 + 6, bw, bh, "Settings", false, 2);
    ui_button(16, y0 + 64, W - 32, bh, "Clear cover cache", false, 2);
    ui_button(16, y0 + 122, W - 32, bh, "Clock", false, 2);
    brd_refresh_region(0, y0, W, H - y0, 2);
}

static void libmenu_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    if ((int32_t)(g_libmenu_guard - brd_millis()) > 0) return;
    int W = brd_width(), H = brd_height();
    int y0 = H - 174;
    int bw = (W - 32 - 16) / 3;
    if (y < y0) { g_scr = SCR_LIBRARY; g_force = true; return; }
    if (y < y0 + 64) {
        if (x >= 16 && x < 16 + bw) { u_file_write_int(ST_POWERED_OFF_AT, 1); poweroff_now(); }
        if (x >= 16 + bw + 8 && x < 16 + 2 * bw + 8) { g_scr = SCR_USB; g_force = true; }
        if (x >= 16 + 2 * (bw + 8)) { g_scr = SCR_SETTINGS; g_force = true; }
        return;
    }
    if (y < y0 + 122) {
        remove(ST_COVER_RGB); remove(ST_COVER_RGBZ); remove(ST_LAST_COVER);
        g_scr = SCR_LIBRARY; g_force = true; return;
    }
    if (y < y0 + 174) { g_scr = SCR_CLOCK; g_force = true; return; }
}

// ---------------------------------------------------------------- reader menu
static void menu_draw(void)
{
    int W = brd_width(), H = brd_height();
    int y0 = H - 286;
    if (g_font_preview && R.lay && R.text) {
        brd_fill_rect(0, 30, W, y0 - 30, th.bg);
        int f = reader_font_id();
        int m = reader_margin();
        int first = R.lay->page_first[R.page];
        if (first >= 0 && first < R.lay->nlines) {
            size_t start = R.lay->lines[first].start;
            size_t avail = R.text_len > start ? R.text_len - start : 0;
            if (avail > 6000) avail = 6000;
            int py = 40;
            Layout* pl = pg_layout(R.text + start, avail, W - 2 * m,
                                    y0 - py - 8, f, g_cfg.rd_spacing * 4);
            if (pl) {
                char lb[1024];
                int yy = py;
                for (int i = 0; i < pl->nlines && yy < y0 - 8; i++) {
                    PgLine* ln = &pl->lines[i];
                    if (ln->flags & PGL_IMAGE) continue;
                    size_t n = ln->end - ln->start;
                    if (n >= sizeof(lb)) n = sizeof(lb) - 1;
                    memcpy(lb, R.text + start + ln->start, n);
                    lb[n] = 0;
                    brd_text(m, yy, lb, th.fg, f);
                    yy += pl->line_h;
                }
                pg_free(pl);
            }
        }
        brd_refresh_region(0, 30, W, y0 - 30, 2);
    }
    brd_fill_rect(0, y0, W, H - y0, th.bg);
    brd_hline(0, y0 - 1, W, th.border);
    const int bh = 50, hw = 251;
    ui_button(16, y0 + 6, 165, bh, "A-", false, 2);
    ui_button(187, y0 + 6, 165, bh, pct_str(), false, 2);
    ui_button(358, y0 + 6, 166, bh, "A+", false, 2);
    ui_button(16, y0 + 62, hw, bh, "", false, 1);
    brd_text(28, y0 + 62 + (bh - brd_font_h(2)) / 2, "Display:", th.fg, 2);
    {
        const char* d = g_display_mode == 0 ? "Balanced" : (g_display_mode == 1 ? "Fast" : "Quality");
        brd_text(16 + hw - 12 - brd_text_w(d, 2), y0 + 62 + (bh - brd_font_h(2)) / 2, d, th.dim, 2);
    }
    ui_button(273, y0 + 62, hw, bh, "", false, 1);
    brd_text(285, y0 + 62 + (bh - brd_font_h(2)) / 2, "Images:", th.fg, 2);
    brd_text(273 + hw - 12 - brd_text_w(g_cfg.images ? "On" : "Off", 2),
             y0 + 62 + (bh - brd_font_h(2)) / 2, g_cfg.images ? "On" : "Off", th.dim, 2);
    ui_button(16, y0 + 118, hw, bh, "Chapters", false, 2);
    ui_button(273, y0 + 118, hw, bh, "Library", false, 2);
    if (furthest_ahead()) {
        ui_button(16, y0 + 174, hw, bh, "Search", false, 2);
        ui_button(273, y0 + 174, hw, bh, "Go to furthest read", false, 2);
    } else {
        ui_button(16, y0 + 174, W - 32, bh, "Search", false, 2);
    }
    ui_button(16, y0 + 230, W - 32, bh, "Power off", false, 2);
    brd_refresh_region(0, y0, W, H - y0, 2);
}

static void menu_commit_font(void)
{
    if (!g_font_dirty) return;
    int keep = R.page;
    if (reader_load_chapter(R.chapter)) {
        R.page = keep;
        if (R.page >= R.lay->npages) R.page = R.lay->npages - 1;
        R.bs.lkey = 0;
        book_count_start();
        R.bs.last_pg = R.page;
        st_book_put(R.path, &R.bs);
        g_reader_full = true;
    }
    g_font_dirty = false;
    g_font_preview = false;
}

static void menu_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int H = brd_height();
    int y0 = H - 286;
    const int bh = 50;
    if (y < y0) { menu_commit_font(); g_scr = SCR_READER; g_force = true; return; }
    if (y >= y0 + 6 && y < y0 + 6 + bh) {
        if (x < 187) font_step(-1);
        else if (x >= 358) font_step(1);
        else return;
        g_font_dirty = true;
        g_font_preview = true;
        st_save_cfg(); g_force = true; return;
    }
    if (y >= y0 + 62 && y < y0 + 62 + bh) {
        if (x < 273) g_display_mode = (g_display_mode + 1) % 3;
        else { g_cfg.images = !g_cfg.images; st_save_cfg(); }
        g_force = true; return;
    }
    if (y >= y0 + 118 && y < y0 + 118 + bh) {
        menu_commit_font();
        if (x < 273) { g_scr = SCR_CHAPTERS; }
        else { g_scr = SCR_LIBRARY; reader_free(); }
        g_force = true; return;
    }
    if (y >= y0 + 174 && y < y0 + 174 + bh) {
        if (furthest_ahead() && x >= 273) {
            if (reader_load_chapter(R.bs.furthest_ch)) {
                R.page = R.bs.furthest_pg;
                if (R.page >= R.lay->npages) R.page = R.lay->npages - 1;
                R.bs.last_pg = R.page;
                st_book_put(R.path, &R.bs);
                g_scr = SCR_READER;
            }
            g_force = true; return;
        }
        menu_commit_font();
        g_search_origin_ch = R.chapter;
        g_search_origin_pg = R.page;
        g_search_origin_valid = true;
        g_scr = SCR_SEARCH; g_force = true; return;
    }
    if (y >= y0 + 230 && y < y0 + 230 + bh) { poweroff_now(); return; }
    menu_commit_font();
    g_scr = SCR_READER; g_force = true;
}

// ---------------------------------------------------------------- chapters
static void chapters_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar(R.title, NULL, false, false);
    int n = epub_chapter_count(R.ep);
    int rows = 11, y0 = 56, rh = 75;
    int y = y0;
    if (R.bs.bm_count > 0) {
        ui_list_row(0, y, W, rh, "Bookmarks", "", true, 2); y += rh;
        int show = R.bs.bm_count < rows - 2 ? R.bs.bm_count : rows - 2;
        for (int i = 0; i < show; i++) {
            char b[32]; snprintf(b, sizeof(b), "Page %d", R.bs.bm[i].pg + 1);
            ui_list_row(0, y, W, rh, b, "", false, 2); y += rh;
        }
    }
    ui_list_row(0, y, W, rh, ">> Continue reading", "", true, 2); y += rh;
    ui_list_row(0, y, W, rh, "Chapters", "", true, 2); y += rh;
    int left = rows - (y - y0) / rh;
    if (left < 1) left = 1;
    for (int i = 0; i < left && i < n; i++) {
        char line[160];
        const char* t = epub_chapter_title(R.ep, i);
        if (t && t[0]) snprintf(line, sizeof(line), "%d. %s", i + 1, t);
        else snprintf(line, sizeof(line), "%d. Chapter %d", i + 1, i + 1);
        ui_list_row(0, y, W, rh, line, "", false, 2);
        y += rh;
    }
    brd_hline(0, H - 60, W, th.border);
    ui_center("Back to library", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void chapters_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int H = brd_height();
    int n = epub_chapter_count(R.ep);
    int rows = 11, y0 = 56, rh = 75;
    if (y > H - 38) { g_scr = SCR_LIBRARY; reader_free(); g_force = true; return; }
    int yy = y0;
    if (R.bs.bm_count > 0) {
        if (y >= yy && y < yy + rh) { yy += rh; }
        else {
            yy += rh;
            int show = R.bs.bm_count < rows - 2 ? R.bs.bm_count : rows - 2;
            for (int i = 0; i < show; i++) {
                if (y >= yy && y < yy + rh) {
                    if (reader_load_chapter(R.bs.bm[i].ch)) {
                        R.page = R.bs.bm[i].pg;
                        if (R.page >= R.lay->npages) R.page = R.lay->npages - 1;
                        reader_progress();
                    }
                    g_scr = SCR_READER; g_force = true; return;
                }
                yy += rh;
            }
        }
    }
    if (y >= yy && y < yy + rh) { g_scr = SCR_READER; g_force = true; return; }
    yy += rh;
    yy += rh;
    int left = rows - (yy - y0) / rh;
    for (int i = 0; i < left && i < n; i++) {
        if (y >= yy && y < yy + rh) {
            if (reader_load_chapter(i)) {
                reader_progress();
                g_scr = SCR_READER; g_force = true;
            }
            return;
        }
        yy += rh;
    }
}

// ---------------------------------------------------------------- settings
static const char* settings_items[] = {
    "Dark mode", "Cover on power-off", "Hourly battery refresh", "Auto power-off",
    "Flip display 180", "Auto-rotation", "Sync clock (NTP)", "Alarm"
};
#define SET_N 8

static void settings_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar("Settings", NULL, false, false);
    int y0 = 40, rh = 75;
    brd_hline(0, y0, W, th.border);
    for (int i = 0; i < SET_N; i++) {
        int y = y0 + i * rh;
        brd_hline(0, y + rh - 1, W, th.border);
        int ty = y + (rh - brd_font_h(2)) / 2;
        if (i == 2) ty = y + 8;
        ui_text(25, ty, settings_items[i], th.fg, 2);
        if (i == 2) {
            ui_text(25, y + 46, "wakes device hourly, shortens battery", th.fg, 13);
        }
        switch (i) {
        case 0: ui_toggle(W - 76, y + (rh - 30) / 2, 56, 30, g_cfg.dark_mode); break;
        case 1: ui_toggle(W - 76, y + (rh - 30) / 2, 56, 30, g_cfg.cover_poweroff); break;
        case 2: ui_toggle(W - 76, y + (rh - 30) / 2, 56, 30, g_cfg.hourly_refresh); break;
        case 3: {
            int bh = 40, by = y + (rh - bh) / 2;
            ui_button(315, by, 50, bh, "-", false, 2);
            char v[16]; snprintf(v, sizeof(v), g_cfg.auto_off_min ? "%dm" : "Off", g_cfg.auto_off_min);
            brd_text(415 - brd_text_w(v, 2) / 2, by + (bh - brd_font_h(2)) / 2, v, th.fg, 2);
            ui_button(465, by, 50, bh, "+", false, 2);
            break;
        }
        case 4: ui_toggle(W - 76, y + (rh - 30) / 2, 56, 30, g_cfg.flip); break;
        case 5: ui_toggle(W - 76, y + (rh - 30) / 2, 56, 30, g_cfg.autorot); break;
        case 6: brd_text(W - 40, y + (rh - brd_font_h(1)) / 2, ">", th.dim, 2); break;
        case 7: {
            char v[24]; snprintf(v, sizeof(v), g_cfg.alarm_on ? "%02d:%02d >" : "Off >", g_cfg.alarm_h, g_cfg.alarm_m);
            brd_text(W - 40 - brd_text_w(v, 1), y + (rh - brd_font_h(1)) / 2, v, th.dim, 1);
            break;
        }
        }
    }
    brd_hline(0, H - 60, W, th.border);
    ui_center("<< Back to library", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void settings_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int H = brd_height();
    int y0 = 40, rh = 75;
    if (y > H - 44) { g_scr = SCR_LIBRARY; g_force = true; return; }
    for (int i = 0; i < SET_N; i++) {
        if (y < y0 + i * rh || y >= y0 + (i + 1) * rh) continue;
        switch (i) {
        case 0: g_cfg.dark_mode = !g_cfg.dark_mode; theme_init(); break;
        case 1: g_cfg.cover_poweroff = !g_cfg.cover_poweroff; break;
        case 2: g_cfg.hourly_refresh = !g_cfg.hourly_refresh; break;
        case 3: {
            static const int c[] = { 0, 5, 10, 15, 30, 60 };
            int k = 0; for (int j = 0; j < 6; j++) if (c[j] == g_cfg.auto_off_min) k = j;
            if (x > 450) k = (k + 1) % 6;
            else if (x > 300) k = (k + 5) % 6;
            g_cfg.auto_off_min = c[k];
            break;
        }
        case 4:
            g_cfg.flip = !g_cfg.flip;
            g_cfg.orientation = g_cfg.flip ? 2 : 0;
            brd_rotation(g_cfg.orientation);
            break;
        case 5: g_cfg.autorot = !g_cfg.autorot; brd_imu_power(g_cfg.autorot); break;
        case 6: brd_wifi_off(); g_scr = SCR_WIFI; g_force = true; return;
        case 7: g_scr = SCR_ALARMSET; g_force = true; return;
        }
        st_save_cfg();
        g_force = true;
        return;
    }
}

// ---------------------------------------------------------------- clock/alarm
static void clock_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar("Clock", NULL, false, false);
    int y, mo, d, h, mi, s;
    char t[16] = "--:--", ds[32] = "";
    if (brd_get_localtime(&y, &mo, &d, &h, &mi, &s)) {
        snprintf(t, sizeof(t), "%02d:%02d", h, mi);
        snprintf(ds, sizeof(ds), "%04d-%02d-%02d", y, mo, d);
    }
    ui_center(t, 385, 11, th.fg);
    ui_center(ds, 480, 2, th.dim);
    char b[80]; snprintf(b, sizeof(b), "UTC %+d", g_cfg.utc_offset);
    ui_center(b, 548, 1, th.fg);
    if (g_cfg.ntp_synced && g_cfg.ntp_epoch > 1700000000) {
        time_t ep = (time_t)g_cfg.ntp_epoch; struct tm ti; localtime_r(&ep, &ti);
        snprintf(b, sizeof(b), "Last NTP sync: %04d-%02d-%02d %02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else snprintf(b, sizeof(b), "Last NTP sync: never");
    ui_center(b, 590, 1, th.fg);
    snprintf(b, sizeof(b), "Alarm: %s", g_cfg.alarm_on ? "On" : "Off");
    ui_center(b, 630, 1, th.fg);
    brd_hline(0, H - 60, W, th.border);
    ui_center("Tap to go back", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void clock_input(int g, int x, int y)
{
    (void)x; (void)y;
    if (g != GEST_TAP && g != GEST_SWIPE_DOWN) return;
    g_scr = SCR_LIBRARY; g_force = true;
}

static void alarmset_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar("Alarm", NULL, false, false);
    int y0 = brd_font_h(2) + 28, rh = 64;
    ui_text(24, y0, "Alarm", th.fg, 2);
    ui_toggle(W - 76, y0 + (rh - 30) / 2, 56, 30, g_cfg.alarm_on);
    brd_hline(0, y0 + rh - 10, W, th.border); y0 += rh;
    ui_text(24, y0, "Hour", th.dim, 2);
    ui_button(W - 204, y0 - 8, 52, 40, "-", false, 2);
    { char v[8]; snprintf(v, sizeof(v), "%02d", g_cfg.alarm_h); brd_text(W - 108 - brd_text_w(v, 1) / 2, y0 + 4, v, th.fg, 1); }
    ui_button(W - 64, y0 - 8, 52, 40, "+", false, 2);
    brd_hline(0, y0 + rh - 10, W, th.border); y0 += rh;
    ui_text(24, y0, "Minute", th.dim, 2);
    ui_button(W - 204, y0 - 8, 52, 40, "-", false, 2);
    { char v[8]; snprintf(v, sizeof(v), "%02d", g_cfg.alarm_m); brd_text(W - 108 - brd_text_w(v, 1) / 2, y0 + 4, v, th.fg, 1); }
    ui_button(W - 64, y0 - 8, 52, 40, "+", false, 2);
    brd_hline(0, H - 60, W, th.border);
    ui_center("<< Back to settings", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void alarmset_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int W = brd_width(), H = brd_height();
    int y0 = brd_font_h(2) + 28, rh = 64;
    if (y > H - 40) { g_scr = SCR_SETTINGS; g_force = true; return; }
    if (ui_hit(0, y0, W, rh, x, y)) { g_cfg.alarm_on = !g_cfg.alarm_on; }
    else if (y >= y0 + rh && y < y0 + 2 * rh) {
        if (x > W - 220) g_cfg.alarm_h = (g_cfg.alarm_h + 23) % 24;
        else if (x > W - 100) g_cfg.alarm_h = (g_cfg.alarm_h + 1) % 24;
    } else if (y >= y0 + 2 * rh && y < y0 + 3 * rh) {
        if (x > W - 220) g_cfg.alarm_m = (g_cfg.alarm_m + 59) % 60;
        else if (x > W - 100) g_cfg.alarm_m = (g_cfg.alarm_m + 1) % 60;
    }
    st_save_cfg(); g_force = true;
}

// ---------------------------------------------------------------- wifi
typedef struct { const char* label; char ch; int width; int action; } Key;
/* action: 0 char, 1 shift, 2 del, 3 space, 4 next, 6 symbols */
static const Key k_lower[] = {
 {"q",'q',1,0},{"w",'w',1,0},{"e",'e',1,0},{"r",'r',1,0},{"t",'t',1,0},{"y",'y',1,0},{"u",'u',1,0},{"i",'i',1,0},{"o",'o',1,0},{"p",'p',1,0},
 {"a",'a',1,0},{"s",'s',1,0},{"d",'d',1,0},{"f",'f',1,0},{"g",'g',1,0},{"h",'h',1,0},{"j",'j',1,0},{"k",'k',1,0},{"l",'l',1,0},
 {"^",0,1,1},{"z",'z',1,0},{"x",'x',1,0},{"c",'c',1,0},{"v",'v',1,0},{"b",'b',1,0},{"n",'n',1,0},{"m",'m',1,0},{"<",0,1,2},
 {"123",0,2,6},{"SPACE",' ',6,3},{"Next",0,2,4}
};
static const Key k_upper[] = {
 {"Q",'Q',1,0},{"W",'W',1,0},{"E",'E',1,0},{"R",'R',1,0},{"T",'T',1,0},{"Y",'Y',1,0},{"U",'U',1,0},{"I",'I',1,0},{"O",'O',1,0},{"P",'P',1,0},
 {"A",'A',1,0},{"S",'S',1,0},{"D",'D',1,0},{"F",'F',1,0},{"G",'G',1,0},{"H",'H',1,0},{"J",'J',1,0},{"K",'K',1,0},{"L",'L',1,0},
 {"^",0,1,1},{"Z",'Z',1,0},{"X",'X',1,0},{"C",'C',1,0},{"V",'V',1,0},{"B",'B',1,0},{"N",'N',1,0},{"M",'M',1,0},{"<",0,1,2},
 {"123",0,2,6},{"SPACE",' ',6,3},{"Next",0,2,4}
};
static const Key k_sym[] = {
 {"1",'1',1,0},{"2",'2',1,0},{"3",'3',1,0},{"4",'4',1,0},{"5",'5',1,0},{"6",'6',1,0},{"7",'7',1,0},{"8",'8',1,0},{"9",'9',1,0},{"0",'0',1,0},
 {"-",'-',1,0},{"_",'_',1,0},{"=",'=',1,0},{"+",'+',1,0},{"[",'[',1,0},{"]",']',1,0},{"{",'{',1,0},{"}",'}',1,0},{"|",'|',1,0},{"\\",'\\',1,0},
 {";",';',1,0},{":",':',1,0},{"'",'\'',1,0},{"\"",'"',1,0},{"<",'<',1,0},{">",'>',1,0},{"?",'?',1,0},{"/",'/',1,0},{"~",'~',1,0},{"`",'`',1,0},
 {"ABC",0,2,6},{"SPACE",' ',6,3},{"Next",0,2,4}
};
#define KEYN (int)(sizeof(k_lower)/sizeof(Key))
#define KEY_ROWS 4
#define KEY_Y 130
#define KEY_KH 42
#define KEY_GAP 8
#define KEY_COLS 10
#define KEY_W 50

static int  g_wifi_page;
static int  g_wifi_field;
static bool g_wifi_state; /* 0 edit, 1 failed */
static char g_wifi_ssid[64], g_wifi_pass[64];
static char g_wifi_err[32];

static const Key* keys_for_page(void)
{
    return g_wifi_page == 2 ? k_sym : (g_wifi_page == 1 ? k_upper : k_lower);
}

static void key_rows(const Key* k, int* start, int* nrows)
{
    int total = g_wifi_page == 2 ? (int)(sizeof(k_sym)/sizeof(Key))
              : g_wifi_page == 1 ? (int)(sizeof(k_upper)/sizeof(Key))
              : (int)(sizeof(k_lower)/sizeof(Key));
    static const int ends_lower[4] = { 10, 19, 28, 31 };
    static const int ends_sym[4]   = { 10, 20, 30, 33 };
    const int* ends = g_wifi_page == 2 ? ends_sym : ends_lower;
    int i = 0;
    for (int r = 0; r < 4; r++) {
        int e = ends[r];
        if (e > total) e = total;
        start[r] = i;
        i = e;
    }
    start[4] = total;
    *nrows = 4;
}

static void key_draw(const Key* k, int y0, int font)
{
    int W = brd_width(), kw = KEY_W;
    int start[8], nrows = 0;
    key_rows(k, start, &nrows);
    for (int r = 0; r < nrows; r++) {
        int i = start[r], ri = start[r + 1], used = 0;
        for (int j = i; j < ri; j++) used += k[j].width;
        int x = (W - used * kw) / 2;
        int y = y0 + r * (KEY_KH + KEY_GAP);
        for (int j = i; j < ri; j++) {
            int bw = k[j].width * kw - 4;
            ui_button(x, y, bw, KEY_KH, k[j].label, false, font);
            x += k[j].width * kw;
        }
    }
}

static int key_hit(const Key* k, int y0, int x, int y)
{
    int W = brd_width(), kw = KEY_W;
    int start[8], nrows = 0;
    key_rows(k, start, &nrows);
    for (int r = 0; r < nrows; r++) {
        int i = start[r], ri = start[r + 1], used = 0;
        for (int j = i; j < ri; j++) used += k[j].width;
        int kx = (W - used * kw) / 2;
        int ky = y0 + r * (KEY_KH + KEY_GAP);
        for (int j = i; j < ri; j++) {
            int bw = k[j].width * kw - 4;
            if (ui_hit(kx, ky, bw, KEY_KH, x, y)) return j;
            kx += k[j].width * kw;
        }
    }
    return -1;
}

static void wifi_field_draw(void)
{
    int W = brd_width();
    const char* field = g_wifi_field ? g_wifi_pass : g_wifi_ssid;
    brd_fill_rect(20, 46, W - 40, 38, th.bg);
    brd_draw_rect(20, 46, W - 40, 38, th.border);
    char shown[96];
    if (!field[0]) snprintf(shown, sizeof(shown), g_wifi_field ? "Enter password..." : "Enter network name...");
    else if (g_wifi_field) { int n = (int)strlen(field); if (n > 60) n = 60; memset(shown, '*', n); shown[n] = 0; }
    else snprintf(shown, sizeof(shown), "%s", field);
    ui_text_trunc(30, 56, W - 60, shown, th.fg, 2);
}

static void wifi_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar("WiFi Sync", NULL, false, false);
    if (g_wifi_state) {
        ui_center("Sync failed", H / 2 - 40, 2, th.fg);
        ui_center(g_wifi_err, H / 2 + 10, 1, th.dim);
        int bh = 44, bw = (W - 24 - 16) / 3;
        ui_button(12, H / 2 + 60, bw, bh, "Retry", false, 1);
        ui_button(12 + bw + 8, H / 2 + 60, bw, bh, "Edit", false, 1);
        ui_button(12 + 2 * (bw + 8), H / 2 + 60, bw, bh, "Back", false, 1);
        brd_refresh(0);
        return;
    }
    const char* label = g_wifi_field ? "WiFi Password" : "WiFi SSID";
    ui_text(24, 34, label, th.dim, 1);
    wifi_field_draw();
    ui_text(24, 98, "UTC offset:", th.fg, 2);
    ui_button(224, 92, 52, 40, "-", false, 2);
    { char u[8]; snprintf(u, sizeof(u), "%+d", g_cfg.utc_offset); brd_text(300 - brd_text_w(u, 2) / 2, 99, u, th.fg, 2); }
    ui_button(320, 92, 52, 40, "+", false, 2);
    key_draw(keys_for_page(), KEY_Y, 2);
    brd_hline(0, H - 60, W, th.border);
    ui_center("Back", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void wifi_connect(void)
{
    ui_busy("Connecting to WiFi...");
    int r = brd_wifi_sync(g_wifi_ssid, g_wifi_pass, g_cfg.utc_offset, 20, NULL, 0);
    if (r == 1) { g_wifi_state = 1; snprintf(g_wifi_err, sizeof(g_wifi_err), "SSID not found"); brd_wifi_off(); }
    else if (r == 2) { g_wifi_state = 1; snprintf(g_wifi_err, sizeof(g_wifi_err), "NTP sync timed out"); brd_wifi_off(); }
    else {
        st_wifi_save(g_wifi_ssid, g_wifi_pass);
        g_cfg.ntp_synced = true; g_cfg.ntp_epoch = brd_epoch();
        st_save_cfg(); brd_rtc_sync(); brd_wifi_off();
        g_scr = SCR_SETTINGS;
    }
    g_force = true;
}

static void wifi_test_now(const char* ssid, const char* pass)
{
    snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", ssid);
    snprintf(g_wifi_pass, sizeof(g_wifi_pass), "%s", pass);
    g_wifi_field = 1;
    g_wifi_state = 0;
    g_scr = SCR_WIFI;
    wifi_connect();
    if (g_wifi_state) { g_scr = SCR_WIFI; g_force = true; }
}

static void wifi_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int W = brd_width(), H = brd_height();
    if (g_wifi_state) {
        int bh = 44, bw = (W - 24 - 16) / 3;
        if (y >= H / 2 + 60 && y < H / 2 + 60 + bh) {
            if (x < 12 + bw) { g_wifi_state = 0; g_force = true; }
            else if (x < 12 + 2 * bw + 8) { g_wifi_state = 0; g_wifi_field = 0; g_force = true; }
            else { g_wifi_state = 0; g_scr = SCR_SETTINGS; g_force = true; }
        }
        return;
    }
    if (y > H - 40) { g_scr = SCR_SETTINGS; g_force = true; return; }
    if (ui_hit(224, 92, 52, 40, x, y)) { g_cfg.utc_offset--; brd_apply_tz(g_cfg.utc_offset); st_save_cfg(); g_force = true; return; }
    if (ui_hit(324, 92, 52, 40, x, y)) { g_cfg.utc_offset++; brd_apply_tz(g_cfg.utc_offset); st_save_cfg(); g_force = true; return; }
    const Key* k = keys_for_page();
    int idx = key_hit(k, KEY_Y, x, y);
    if (idx < 0) return;
    char* dst = g_wifi_field ? g_wifi_pass : g_wifi_ssid;
    int len = (int)strlen(dst);
    bool field_only = false;
    switch (k[idx].action) {
    case 1: g_wifi_page = g_wifi_page == 0 ? 1 : 0; break;
    case 2: if (len) { dst[len - 1] = 0; field_only = true; } break;
    case 3: if (len < 63) { dst[len] = ' '; dst[len + 1] = 0; field_only = true; } break;
    case 4:
        if (!g_wifi_field) { if (g_wifi_ssid[0]) { g_wifi_field = 1; g_wifi_page = 0; } }
        else wifi_connect();
        return;
    case 6: g_wifi_page = g_wifi_page == 2 ? 0 : 2; break;
    default:
        if (k[idx].ch && len < 63) { dst[len] = k[idx].ch; dst[len + 1] = 0; field_only = true; }
        break;
    }
    if (field_only) {
        wifi_field_draw();
        brd_refresh_region(20, 46, W - 40, 38, 2);
        g_force = false;
        return;
    }
    g_force = true;
}

// ---------------------------------------------------------------- search
static char g_query[64];
static int  g_nres;
typedef struct { int ch; uint32_t off; char snippet[96]; } Result;
static Result g_results[100];
static int  g_res_scroll;

static bool g_search_none;

static bool word_byte(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

static const char* stristr_word_len(const char* hay, const char* needle, size_t nl)
{
    const char* p = hay;
    while ((p = u_stristr_len(p, needle, nl)) != NULL) {
        bool left = (p == hay) || !word_byte((unsigned char)p[-1]);
        bool right = !word_byte((unsigned char)p[nl]);
        if (left && right) return p;
        p++;
    }
    return NULL;
}

static void search_run(void)
{
    u_trim(g_query);
    if (!g_query[0] || !R.ep) return;
    size_t qlen = strlen(g_query);
    g_nres = 0;
    ui_busy("Searching...");
    int nch = epub_chapter_count(R.ep);
    for (int c = 0; c < nch && g_nres < 100; c++) {
        size_t tl = 0;
        char* t = search_cache_get(c, &tl);
        bool owned = false;
        if (!t) {
            t = epub_chapter_text(R.ep, c, &tl);
            if (!t) continue;
            owned = !search_cache_put(c, t, tl);
        }
        const char* p = t;
        while (g_nres < 100) {
            const char* h = stristr_word_len(p, g_query, qlen);
            if (!h) break;
            int off = (int)(h - t);
            int st = off > 32 ? off - 32 : 0;
            int sn = 70; if (st + sn > (int)tl) sn = (int)tl - st;
            if (sn < 0) sn = 0;
            memcpy(g_results[g_nres].snippet, t + st, (size_t)sn);
            g_results[g_nres].snippet[sn] = 0;
            g_results[g_nres].ch = c;
            g_results[g_nres].off = (uint32_t)off;
            g_nres++;
            p = h + 1;
        }
        if (owned) free(t);
        brd_delay(1);
    }
    g_res_scroll = 0;
    g_search_none = g_nres == 0;
    if (g_nres > 0) g_scr = SCR_RESULTS;
}

static void search_back_to_origin(void)
{
    if (!R.ep || !g_search_origin_valid) {
        g_scr = SCR_MENU;
        g_force = true;
        return;
    }
    if (R.chapter != g_search_origin_ch || !R.lay) {
        if (!reader_load_chapter(g_search_origin_ch)) {
            g_scr = SCR_MENU;
            g_force = true;
            return;
        }
    }
    R.page = g_search_origin_pg;
    if (R.page < 0) R.page = 0;
    if (R.page >= R.lay->npages) R.page = R.lay->npages - 1;
    R.bs.last_ch = R.chapter;
    R.bs.last_pg = R.page;
    st_book_put(R.path, &R.bs);
    g_search_origin_valid = false;
    g_reader_full = false;
    g_scr = SCR_READER;
    g_force = true;
}

static void search_kbd_draw(void)
{
    static const char* r1 = "qwertyuiop";
    static const char* r2 = "asdfghjkl";
    static const char* r3 = "zxcvbnm<";
    int y = 94;
    for (int i = 0; i < 10; i++) ui_button(20 + i * 50, y, 46, 42, (char[]){r1[i], 0}, false, 2);
    for (int i = 0; i < 9; i++) ui_button(45 + i * 50, y + 50, 46, 42, (char[]){r2[i], 0}, false, 2);
    for (int i = 0; i < 8; i++) ui_button(80 + i * 50, y + 100, 46, 42, (char[]){r3[i], 0}, false, 2);
    ui_button(80, y + 150, 292, 42, "SPACE", false, 2);
    ui_button(404, y + 150, 100, 42, "Search", false, 2);
}

static int search_kbd_hit(int x, int y)
{
    static const char* r1 = "qwertyuiop";
    static const char* r2 = "asdfghjkl";
    static const char* r3 = "zxcvbnm<";
    int y0 = 94;
    for (int i = 0; i < 10; i++) if (ui_hit(20 + i * 50, y0, 46, 42, x, y)) return r1[i];
    for (int i = 0; i < 9; i++) if (ui_hit(45 + i * 50, y0 + 50, 46, 42, x, y)) return r2[i];
    for (int i = 0; i < 8; i++) if (ui_hit(80 + i * 50, y0 + 100, 46, 42, x, y)) return r3[i];
    if (ui_hit(80, y0 + 150, 292, 42, x, y)) return ' ';
    if (ui_hit(404, y0 + 150, 100, 42, x, y)) return '\n';
    return -1;
}

static void search_field_draw(void)
{
    int W = brd_width();
    brd_fill_rect(20, 45, W - 40, 40, th.bg);
    brd_draw_rect(20, 45, W - 40, 40, th.border);
    ui_text_trunc(28, 56, W - 56, g_query[0] ? g_query : "Type to search...", th.fg, 2);
}

static void search_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar("Search", NULL, false, false);
    search_field_draw();
    search_kbd_draw();
    if (g_search_none) ui_center("No results", 330, 1, th.dim);
    brd_hline(0, H - 60, W, th.border);
    ui_center("Back", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void search_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int W = brd_width(), H = brd_height();
    if (y > H - 40) { search_back_to_origin(); return; }
    int ch = search_kbd_hit(x, y);
    if (ch < 0) return;
    int len = (int)strlen(g_query);
    bool changed = false;
    if (ch == '<') { if (len) { g_query[--len] = 0; changed = true; } }
    else if (ch == ' ') { if (len < 63) { g_query[len] = ' '; g_query[len + 1] = 0; changed = true; } }
    else if (ch == '\n') { search_run(); g_force = true; return; }
    else if (len < 63) { g_query[len] = (char)ch; g_query[len + 1] = 0; changed = true; }
    if (changed) {
        g_search_none = false;
        search_field_draw();
        brd_refresh_region(20, 45, W - 40, 40, 2);
        g_force = false;
        return;
    }
    g_force = true;
}

static void results_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    char hdr[96]; snprintf(hdr, sizeof(hdr), "Search: %s", g_query);
    ui_statusbar(hdr, NULL, false, false);
    int rows = 10, y0 = 52, rh = 82;
    for (int i = 0; i < rows && g_res_scroll + i < g_nres; i++) {
        Result* r = &g_results[g_res_scroll + i];
        int y = y0 + i * rh;
        char ch[24]; snprintf(ch, sizeof(ch), "Chapter %d", r->ch + 1);
        ui_text(42, y + 5, ch, th.fg, 13);
        ui_text_trunc(42, y + 34, W - 84, r->snippet, th.fg, 2);
        brd_hline(0, y + rh - 1, W, th.border);
    }
    brd_hline(0, H - 60, W, th.border);
    char cnt[32]; snprintf(cnt, sizeof(cnt), "%d result%s", g_nres, g_nres == 1 ? "" : "s");
    brd_text(42, H - 60 + (60 - brd_font_h(2)) / 2, cnt, th.fg, 2);
    int bw = brd_text_w("Back", 2);
    brd_text(W - 42 - bw, H - 60 + (60 - brd_font_h(2)) / 2, "Back", th.fg, 2);
    brd_refresh(0);
}

static void results_input(int g, int x, int y)
{
    int H = brd_height();
    if (g == GEST_SWIPE_UP) { g_res_scroll += 10; if (g_res_scroll >= g_nres) g_res_scroll = 0; g_force = true; return; }
    if (g == GEST_SWIPE_DOWN) { g_res_scroll -= 10; if (g_res_scroll < 0) g_res_scroll = 0; g_force = true; return; }
    if (g != GEST_TAP) return;
    if (y > H - 40) { search_back_to_origin(); return; }
    int rows = 10, y0 = 52, rh = 82;
    for (int i = 0; i < rows && g_res_scroll + i < g_nres; i++) {
        if (y >= y0 + i * rh && y < y0 + (i + 1) * rh) {
            Result* r = &g_results[g_res_scroll + i];
            if (reader_load_chapter(r->ch)) {
                R.page = pg_page_of_offset(R.lay, r->off);
                if (R.page < 0) R.page = 0;
                if (R.page >= R.lay->npages) R.page = R.lay->npages - 1;
                R.bs.last_ch = R.chapter; R.bs.last_pg = R.page;
                st_book_put(R.path, &R.bs);
            }
            g_scr = SCR_READER; g_force = true; return;
        }
    }
}

// ---------------------------------------------------------------- goto
static char g_goto[8];
static int  g_goto_len;

static bool furthest_ahead(void)
{
    return R.bs.furthest_ch > R.chapter ||
           (R.bs.furthest_ch == R.chapter && R.bs.furthest_pg > R.page);
}

static void goto_field_draw(void)
{
    int W = brd_width();
    int total = 1, before = 0;
    total_pages(&total, &before);
    char range[24]; snprintf(range, sizeof(range), "(1-%d)", total);
    brd_fill_rect(40, 45, W - 80, 75, th.bg);
    brd_draw_rect(40, 45, W - 80, 75, th.fg);
    brd_draw_rect(41, 46, W - 82, 73, th.fg);
    ui_text(63, 58, g_goto_len ? g_goto : "Page number", th.fg, 2);
    brd_text(W - 60 - brd_text_w(range, 1), 62, range, th.fg, 1);
}

static void goto_draw(void)
{
    int W = brd_width(), H = brd_height();
    ui_clear();
    ui_statusbar("Go to page", NULL, false, false);
    int total = 1, before = 0;
    total_pages(&total, &before);
    goto_field_draw();
    static const char* keys[12] = { "1","2","3","4","5","6","7","8","9","<","0","Go" };
    for (int k = 0; k < 12; k++) {
        int r = k / 3, c = k % 3;
        ui_button(130 + c * 112, 148 + r * 82, 100, 70, keys[k], false, 2);
    }
    char cur[40]; snprintf(cur, sizeof(cur), "Current page: %d", before + R.page + 1);
    ui_center(cur, 470, 1, th.fg);
    if (furthest_ahead()) ui_button(120, 495, 300, 40, "Reset furthest to current", false, 1);
    brd_hline(0, H - 60, W, th.border);
    ui_center("Back", H - 60 + (60 - brd_font_h(2)) / 2, 2, th.fg);
    brd_refresh(0);
}

static void goto_input(int g, int x, int y)
{
    if (g != GEST_TAP) return;
    int W = brd_width(), H = brd_height();
    static const char* keys[12] = { "1","2","3","4","5","6","7","8","9","<","0","Go" };
    if (y > H - 40) { g_scr = SCR_READER; g_force = true; return; }
    if (furthest_ahead() && ui_hit(120, 495, 300, 40, x, y)) {
        R.bs.furthest_ch = R.chapter; R.bs.furthest_pg = R.page;
        st_book_put(R.path, &R.bs); g_force = true; return;
    }
    for (int k = 0; k < 12; k++) {
        int r = k / 3, c = k % 3;
        if (!ui_hit(130 + c * 112, 148 + r * 82, 100, 70, x, y)) continue;
        bool field_changed = false;
        if (k == 9) { if (g_goto_len) { g_goto[--g_goto_len] = 0; field_changed = true; } }
        else if (k == 11) {
            if (g_goto_len) {
                int p = atoi(g_goto);
                if (p < 1) p = 1;
                int total = 1, before = 0;
                total_pages(&total, &before);
                if (p > total) p = total;
                int ch = 0, rem = p;
                while (ch < 255 && R.bs.page_counts[ch] > 0 && rem > R.bs.page_counts[ch]) {
                    rem -= R.bs.page_counts[ch]; ch++;
                }
                if (reader_load_chapter(ch)) {
                    R.page = rem - 1;
                    if (R.page >= R.lay->npages) R.page = R.lay->npages - 1;
                }
                reader_progress();
                g_scr = SCR_READER;
            }
            g_force = true;
            return;
        } else if (g_goto_len < 7) {
            g_goto[g_goto_len++] = keys[k][0]; g_goto[g_goto_len] = 0; field_changed = true;
        }
        if (field_changed) {
            goto_field_draw();
            brd_refresh_region(40, 45, W - 80, 75, 2);
            g_force = false;
        }
        return;
    }
}

static void alarm_ring_draw(void)
{
    ui_clear();
    ui_center("ALARM", 365, 12, th.fg);
    char t[16];
    snprintf(t, sizeof(t), "%02d:%02d", g_cfg.alarm_h, g_cfg.alarm_m);
    ui_center(t, 455, 11, th.fg);
    ui_center("Tap to dismiss", 600, 2, th.dim);
    brd_refresh(0);
}

// ---------------------------------------------------------------- USB
static bool g_usb_active;
static int  g_usb_err;

static void usb_draw(void)
{
    int H = brd_height();
    ui_clear();
    ui_center("USB Disk Mode", H / 2 - 40, 2, th.fg);
    const char* sub = g_usb_active ? "Tap to exit and reboot" :
                      (g_usb_err == -1 ? "SD not ready" :
                       (g_usb_err == -2 ? "USB start failed" : "Tap to start and reboot"));
    ui_center(sub, H / 2 + 10, 3, th.dim);
    brd_refresh(0);
}

static void usb_input(int g, int x, int y)
{
    (void)x; (void)y;
    if (g != GEST_TAP) return;
    if (!g_usb_active) {
        reader_state_flush();
        batt_flush();
        st_save_cfg();
        int r = brd_usb_disk_start();
        g_usb_err = r;
        if (r == 0) { g_usb_active = true; g_usb_err = 0; }
        g_force = true;
    } else {
        brd_usb_disk_stop();
        brd_restart();
    }
}

// ---------------------------------------------------------------- dev injection
#ifdef DEV_INJECT
static struct { int kind, x0, y0, x1, y1; uint32_t t0, dur; bool active, pressed; } g_inj;
static char g_cmd[96];
static int  g_cmd_n;

static void dev_start(int kind, int x0, int y0, int x1, int y1, int dur)
{
    g_inj.kind = kind; g_inj.x0 = x0; g_inj.y0 = y0; g_inj.x1 = x1; g_inj.y1 = y1;
    g_inj.t0 = brd_millis(); g_inj.dur = (uint32_t)dur; g_inj.active = true; g_inj.pressed = false;
    char lb[64]; snprintf(lb, sizeof(lb), "DBG inj start kind=%d %d,%d", kind, x0, y0); brd_log(lb);
}

static void dev_tick(void)
{
    if (!g_inj.active) return;
    uint32_t el = brd_millis() - g_inj.t0;
    if (!g_inj.pressed) {
        brd_dbg_touch(g_inj.x0, g_inj.y0, 1);
        g_inj.pressed = true;
        return;
    }
    if (g_inj.kind == 0) {
        if (el == 0) brd_dbg_touch(g_inj.x0, g_inj.y0, 1);
        else if (el >= (uint32_t)g_inj.dur) { brd_dbg_touch(g_inj.x0, g_inj.y0, 3); g_inj.active = false; brd_log("DBG inj release"); }
        else brd_dbg_touch(g_inj.x0, g_inj.y0, 2);
    } else {
        uint32_t d = g_inj.dur ? g_inj.dur : 1;
        if (el == 0) brd_dbg_touch(g_inj.x0, g_inj.y0, 1);
        else if (el >= d) { brd_dbg_touch(g_inj.x1, g_inj.y1, 3); g_inj.active = false; }
        else {
            int x = g_inj.x0 + (int)((long)(g_inj.x1 - g_inj.x0) * (long)el / (long)d);
            int y = g_inj.y0 + (int)((long)(g_inj.y1 - g_inj.y0) * (long)el / (long)d);
            brd_dbg_touch(x, y, 2);
        }
    }
}

static void dev_send_shot(void)
{
    int w = brd_width(), h = brd_height();
    uint16_t* px = brd_canvas_ptr();
    if (!px || w <= 0 || h <= 0) return;
    uint8_t hdr[9] = { 'S', 'H', 'O', 'T', '4',
                       (uint8_t)(w & 0xFF), (uint8_t)(w >> 8),
                       (uint8_t)(h & 0xFF), (uint8_t)(h >> 8) };
    brd_serial_write(hdr, sizeof(hdr));
    uint8_t row[270];
    int rb = (w + 1) / 2;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            uint16_t v0 = px[(size_t)y * w + x];
            uint16_t v1 = (x + 1 < w) ? px[(size_t)y * w + x + 1] : v0;
            v0 = (uint16_t)((v0 >> 8) | (v0 << 8));
            v1 = (uint16_t)((v1 >> 8) | (v1 << 8));
            int r0 = ((v0 >> 11) & 31) * 255 / 31;
            int gg0 = ((v0 >> 5) & 63) * 255 / 63;
            int b0 = (v0 & 31) * 255 / 31;
            int r1 = ((v1 >> 11) & 31) * 255 / 31;
            int gg1 = ((v1 >> 5) & 63) * 255 / 63;
            int b1 = (v1 & 31) * 255 / 31;
            uint8_t g0 = (uint8_t)(((r0 * 77 + gg0 * 150 + b0 * 29) >> 8) >> 4);
            uint8_t g1 = (uint8_t)(((r1 * 77 + gg1 * 150 + b1 * 29) >> 8) >> 4);
            row[x / 2] = (uint8_t)((g0 << 4) | (g1 & 0x0F));
        }
        brd_serial_write(row, rb);
    }
}

static void dev_send_shot1(void)
{
    int w = brd_width(), h = brd_height();
    uint16_t* px = brd_canvas_ptr();
    if (!px || w <= 0 || h <= 0) return;
    uint8_t hdr[9] = { 'S', 'H', 'O', 'T', '1',
                       (uint8_t)(w & 0xFF), (uint8_t)(w >> 8),
                       (uint8_t)(h & 0xFF), (uint8_t)(h >> 8) };
    brd_serial_write(hdr, sizeof(hdr));
    uint8_t row[70];
    int rb = (w + 7) / 8;
    for (int y = 0; y < h; y++) {
        memset(row, 0, (size_t)rb);
        for (int x = 0; x < w; x++) {
            uint16_t v = px[(size_t)y * w + x];
            v = (uint16_t)((v >> 8) | (v << 8));
            int r = ((v >> 11) & 31) * 255 / 31;
            int g = ((v >> 5) & 63) * 255 / 63;
            int b = (v & 31) * 255 / 31;
            int gray = (r * 77 + g * 150 + b * 29) >> 8;
            if (gray >= 128) row[x / 8] |= (uint8_t)(0x80 >> (x & 7));
        }
        brd_serial_write(row, rb);
    }
}

static void dev_parse(char* s)
{
    int a, b, c, d;
    if (!strncasecmp(s, "@TAP ", 5) && sscanf(s + 5, "%d %d", &a, &b) == 2) dev_start(0, a, b, a, b, 90);
    else if (!strncasecmp(s, "@SWIPE ", 7) && sscanf(s + 7, "%d %d %d %d", &a, &b, &c, &d) == 4) dev_start(1, a, b, c, d, 180);
    else if (!strncasecmp(s, "@HOLD ", 6) && sscanf(s + 6, "%d %d %d", &a, &b, &c) == 3) dev_start(0, a, b, a, b, c);
    else if (!strncasecmp(s, "@SCREEN", 7)) brd_log(app_screen_name());
    else if (!strncasecmp(s, "@SEARCH ", 8)) {
        snprintf(g_query, sizeof(g_query), "%s", s + 8);
        g_scr = SCR_SEARCH;
        g_force = true;
    }
    else if (!strncasecmp(s, "@SEARCHRUN ", 11)) {
        snprintf(g_query, sizeof(g_query), "%s", s + 11);
        search_run();
        g_scr = g_nres > 0 ? SCR_RESULTS : SCR_SEARCH;
        g_force = true;
    }
    else if (!strncasecmp(s, "@WIFIPASS", 9)) {
        g_wifi_field = 1; g_wifi_page = 0; g_wifi_state = 0;
        g_scr = SCR_WIFI; g_force = true;
    }
    else if (!strncasecmp(s, "@WIFITEST ", 10)) {
        char a[64], b[64];
        if (sscanf(s + 10, "%63s %63s", a, b) == 2) wifi_test_now(a, b);
    }
    else if (!strncasecmp(s, "@SPLASH", 7)) boot_screen_draw();
    else if (!strncasecmp(s, "@BUSY ", 6)) ui_busy(s + 6);
    else if (!strncasecmp(s, "@USBSCREEN", 10)) { g_usb_active = false; g_scr = SCR_USB; g_force = true; }
    else if (!strncasecmp(s, "@LIBRARY", 8)) { reader_free(); g_scr = SCR_LIBRARY; g_force = true; }
    else if (!strncasecmp(s, "@FLIP ", 6)) {
        int n;
        if (sscanf(s + 6, "%d", &n) == 1) {
            g_cfg.flip = n ? 1 : 0;
            g_cfg.orientation = g_cfg.flip ? 2 : 0;
            brd_rotation(g_cfg.orientation);
            st_save_cfg(); g_force = true;
        }
    }
    else if (!strncasecmp(s, "@FONT ", 6)) {
        int n;
        if (sscanf(s + 6, "%d", &n) == 1 && n >= 0 && n <= 5) {
            g_cfg.rd_font = n; st_save_cfg();
            if (R.ep) { g_font_dirty = true; menu_commit_font(); }
            g_force = true;
        }
    }
    else if (!strncasecmp(s, "@SDINFO", 7)) {
        char b[160];
        snprintf(b, sizeof(b), "SD ok=%d sectors=%u secsize=%u size=%llu",
                 brd_sd_ok() ? 1 : 0, (unsigned)brd_sd_num_sectors(),
                 (unsigned)brd_sd_sector_size(), (unsigned long long)brd_sd_size());
        brd_log(b);
    }
    else if (!strncasecmp(s, "@USBSTART", 9)) {
        int r = brd_usb_disk_start();
        char b[64]; snprintf(b, sizeof(b), "USBSTART r=%d", r); brd_log(b);
    }
    else if (!strncasecmp(s, "@BATT", 5)) {
        char b[192];
        snprintf(b, sizeof(b), "BATT pct=%d raw=%d mv=%d chg=%d chgstat=%d vbus=%d hi=%d lo=%d n=%d full=%d low=%d",
                 batt_percent(), batt_percent_raw(), brd_battery_mv(), brd_charging() ? 1 : 0,
                 brd_chg_stat_high() ? 1 : 0, brd_vbus_present() ? 1 : 0,
                 batt_hi_mv(), batt_lo_mv(), batt_sample_count(),
                 batt_is_full() ? 1 : 0, batt_is_low() ? 1 : 0);
        brd_log(b);
    }
    else if (!strncasecmp(s, "@PERF RESET", 11)) { perf_reset(); brd_log("PERF reset"); }
    else if (!strncasecmp(s, "@PERF", 5)) perf_dump();
    else if (!strncasecmp(s, "@STATE", 6)) {
        BookState b; char sp[256];
        if (R.ep) {
            st_book_get(R.path, &b);
            st_book_path(R.path, sp, sizeof(sp));
            char msg[192];
            snprintf(msg, sizeof(msg), "STATE file=%s font=%d last=%d/%d furthest=%d/%d bm=%d", sp,
                     g_cfg.rd_font, b.last_ch, b.last_pg, b.furthest_ch, b.furthest_pg, b.bm_count);
            brd_log(msg);
        } else brd_log("STATE no book");
    }
    else if (!strncasecmp(s, "@SHOT1", 6)) dev_send_shot1();
    else if (!strncasecmp(s, "@ALARMTEST", 10)) alarm_test_now();
    else if (!strncasecmp(s, "@SHOT", 5)) dev_send_shot();
    else if (!strncasecmp(s, "@PING", 5)) brd_log("PONG");
}

static void dev_poll(void)
{
    if (g_inj.active) { dev_tick(); return; }   /* finish press/move/release before parsing the next command */
    while (brd_serial_avail()) {
        int ch = brd_serial_read();
        if (ch == '\r' || ch == '\n') {
            g_cmd[g_cmd_n] = 0;
            if (g_cmd_n) dev_parse(g_cmd);
            g_cmd_n = 0;
            break;   /* one injected command per loop: never overwrite g_inj */
        } else if (g_cmd_n < (int)sizeof(g_cmd) - 1) {
            g_cmd[g_cmd_n++] = (char)ch;
        }
    }
    dev_tick();
}
#endif

// ---------------------------------------------------------------- app
static uint32_t g_last_input;
static bool g_alarm_ring;
static int  g_alarm_min = -1;
static uint32_t g_alarm_beep_ms;

static void alarm_test_now(void)
{
    int y, mo, d, h, mi, s;
    if (!brd_get_localtime(&y, &mo, &d, &h, &mi, &s)) return;
    g_cfg.alarm_on = true;
    g_cfg.alarm_h = h;
    g_cfg.alarm_m = mi;
    g_alarm_min = -1;
    g_alarm_ring = false;
    st_save_cfg();
    brd_log("alarm test set to current minute");
}

void app_setup(void)
{
    brd_serial_begin();
    brd_begin(false);
    brd_sd_mount();
    st_init();
    theme_init();
    g_cfg.orientation = g_cfg.flip ? 2 : 0;
    brd_rotation(g_cfg.orientation);
    brd_imu_power(false);
    batt_init();
    {
        int mv = brd_battery_mv();
        if (mv > 0) batt_sample_ex(mv, brd_charging(), brd_chg_stat_high(), (int)(brd_temp() * 10.0f));
    }
    g_last_input = brd_millis();
    brd_cpu_freq(240);
    boot_screen_draw();
    brd_delay(1500);
    library_scan();
    brd_cpu_freq(80);
    g_scr = SCR_LIBRARY;
    g_force = true;
    brd_log("Lysis Epub reader ready");
}

int app_loop(void)
{
#ifdef DEV_INJECT
    dev_poll();
#endif
    book_count_step();
    brd_touch_t t;
    brd_touch_poll(&t);
    /* Any finger event immediately restores maximum CPU before decoding
       the gesture and rendering the response. */
    /* keep 80 MHz while opening the book/SD; render raises 240 itself */
    int gx = 0, gy = 0;
    int g = 0;
    if (t.released) {
        int x0 = t.x0, y0 = t.y0;
        if (x0 == 0 && y0 == 0) { x0 = t.x; y0 = t.y; }
        int dx = t.x - x0, dy = t.y - y0;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        brd_log("DBG touch release");
        if (adx > 70 && adx > ady) g = dx > 0 ? GEST_SWIPE_RIGHT : GEST_SWIPE_LEFT;
        else if (ady > 70 && ady > adx) g = dy > 0 ? GEST_SWIPE_DOWN : GEST_SWIPE_UP;
        else g = GEST_TAP;
        gx = x0; gy = y0;
    } else if (t.held) {
        g = GEST_HOLD; gx = t.x; gy = t.y;
    }
    if (!g_alarm_ring && g_cfg.alarm_on) {
        int y, mo, d, h, mi, s;
        if (brd_get_localtime(&y, &mo, &d, &h, &mi, &s)) {
            int minute = h * 60 + mi;
            if (h == g_cfg.alarm_h && mi == g_cfg.alarm_m && minute != g_alarm_min) {
                g_alarm_min = minute;
                g_alarm_ring = true;
                g_scr = SCR_ALARMRING;
                g_force = true;
                g_alarm_beep_ms = brd_millis();
                brd_beep_tone(880, 600);
            }
        }
    }
    if (g_alarm_ring) {
        if ((uint32_t)(brd_millis() - g_alarm_beep_ms) > 1500) {
            g_alarm_beep_ms = brd_millis();
            brd_beep_tone(880, 600);
        }
        if (g) {
            brd_beep_stop();
            g_alarm_ring = false;
            g_scr = R.ep ? SCR_READER : SCR_LIBRARY;
            g_force = true;
        }
    } else if (g) {
        {
            char dbg[96];
            snprintf(dbg, sizeof(dbg), "DBG gesture=%d %d,%d screen=%d", g, gx, gy, (int)g_scr);
            brd_log(dbg);
        }
        g_last_input = brd_millis();
        g_count_pause_until = brd_millis() + 250;
        g_evt_kind = 0;
        if (g_scr == SCR_READER) {
            if (g == GEST_TAP && gy >= 36 &&
                (gx < brd_width() / 4 || gx > brd_width() * 3 / 4))
                perf_start(PERF_PAGE);
            else if (g == GEST_SWIPE_UP)
                perf_start(PERF_MENU);
        } else if (g_scr == SCR_MENU && g == GEST_TAP) {
            perf_start(PERF_MENU_CLOSE);
        }
        switch (g_scr) {
        case SCR_LIBRARY: library_input(g, gx, gy); break;
        case SCR_LIBMENU: libmenu_input(g, gx, gy); break;
        case SCR_READER:
            if (g == GEST_SWIPE_UP) { g_font_preview = false; g_scr = SCR_MENU; g_force = true; }
            else if (g == GEST_SWIPE_DOWN) { g_scr = SCR_LIBRARY; reader_free(); g_force = true; }
            else if (g == GEST_TAP) {
                if (gy < 36) {
                    if (gx > brd_width() - 70) { bookmark_toggle(); g_force = true; }
                    else if (gx > brd_width() / 3 && gx < brd_width() * 2 / 3) {
                        g_goto_len = 0; g_scr = SCR_GOTO; g_force = true;
                    } else { g_scr = SCR_CHAPTERS; g_force = true; }
                } else if (gx < brd_width() / 4) {
                    if (R.page > 0) R.page--;
                    else if (R.chapter > 0) { if (reader_load_chapter(R.chapter - 1)) R.page = R.lay->npages - 1; }
                    reader_progress(); g_force = true;
                } else if (gx > brd_width() * 3 / 4) {
                    if (R.page + 1 < R.lay->npages) R.page++;
                    else if (R.chapter + 1 < epub_chapter_count(R.ep)) { if (reader_load_chapter(R.chapter + 1)) R.page = 0; }
                    reader_progress(); g_force = true;
                }
            }
            break;
        case SCR_MENU: menu_input(g, gx, gy); break;
        case SCR_CHAPTERS: chapters_input(g, gx, gy); break;
        case SCR_SETTINGS: settings_input(g, gx, gy); break;
        case SCR_CLOCK: clock_input(g, gx, gy); break;
        case SCR_ALARMSET: alarmset_input(g, gx, gy); break;
        case SCR_WIFI: wifi_input(g, gx, gy); break;
        case SCR_SEARCH: search_input(g, gx, gy); break;
        case SCR_RESULTS: results_input(g, gx, gy); break;
        case SCR_GOTO: goto_input(g, gx, gy); break;
        case SCR_USB: usb_input(g, gx, gy); break;
        default: break;
        }
    }
    if (brd_pwr_clicked()) {
        if (g_scr == SCR_LIBRARY) { g_scr = SCR_LIBMENU; g_force = true; }
        else if (g_scr == SCR_READER) { g_scr = SCR_MENU; g_force = true; }
    }
    if (g_force) {
        g_force = false;
        uint32_t d0 = (uint32_t)brd_us();
        switch (g_scr) {
        case SCR_LIBRARY: library_draw(); break;
        case SCR_LIBMENU: libmenu_draw(); break;
        case SCR_READER: reader_draw(); break;
        case SCR_MENU: menu_draw(); break;
        case SCR_CHAPTERS: chapters_draw(); break;
        case SCR_SETTINGS: settings_draw(); break;
        case SCR_CLOCK: clock_draw(); break;
        case SCR_ALARMSET: alarmset_draw(); break;
        case SCR_WIFI: wifi_draw(); break;
        case SCR_SEARCH: search_draw(); break;
        case SCR_RESULTS: results_draw(); break;
        case SCR_GOTO: goto_draw(); break;
        case SCR_USB: usb_draw(); break;
        case SCR_ALARMRING: alarm_ring_draw(); break;
        default: break;
        }
        if (g_evt_kind) {
            uint32_t now_us = (uint32_t)brd_us();
            perf_add(g_evt_kind, now_us - g_evt_t0, now_us - d0, 0);
            g_evt_kind = 0;
        }
    }
    static uint32_t last_bat;
    static int last_hour = -1;
    uint32_t now = brd_millis();
    if (g_cfg.hourly_refresh) {
        int yy, mo, dd, hh, mm, ss;
        if (brd_get_localtime(&yy, &mo, &dd, &hh, &mm, &ss)) {
            if (last_hour >= 0 && hh != last_hour) {
                brd_refresh(0);
                g_reader_full = true;
            }
            last_hour = hh;
        }
    }
    if ((uint32_t)(now - last_bat) > 30000) {
        int mv = brd_battery_mv();
        if (mv > 0) batt_sample_ex(mv, brd_charging(), brd_chg_stat_high(), (int)(brd_temp() * 10.0f));
        last_bat = now;
        if (batt_is_low() && !brd_charging()) poweroff_now();
    }
    if (g_cfg.auto_off_min > 0 && (uint32_t)(now - g_last_input) > (uint32_t)g_cfg.auto_off_min * 60000u) {
        poweroff_now();
    }
    if (g_state_dirty && (uint32_t)(now - g_state_dirty_at) >= 2500u)
        reader_state_flush();
    if (R.ep && g_scr == SCR_READER && g_partial_count >= 10 &&
        (uint32_t)(now - g_last_input) > 12000u &&
        (uint32_t)(now - g_last_ghost_ms) > 30000u) {
        brd_refresh(0);
        g_partial_count = 0;
        g_last_ghost_ms = now;
    }
    brd_cpu_freq(80);
    return 30;
}

bool app_can_deep_idle(void) { return false; }

const char* app_screen_name(void)
{
    switch (g_scr) {
    case SCR_LIBRARY: return "library";
    case SCR_LIBMENU: return "libmenu";
    case SCR_READER: return "reader";
    case SCR_MENU: return "menu";
    case SCR_CHAPTERS: return "chapters";
    case SCR_SETTINGS: return "settings";
    case SCR_CLOCK: return "clock";
    case SCR_ALARMSET: return "alarmset";
    case SCR_WIFI: return "wifi";
    case SCR_SEARCH: return "search";
    case SCR_RESULTS: return "results";
    case SCR_GOTO: return "goto";
    case SCR_USB: return "usb";
    case SCR_POWEROFF: return "poweroff";
    case SCR_ALARMRING: return "alarmring";
    default: return "?";
    }
}