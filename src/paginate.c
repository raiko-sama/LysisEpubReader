#include "config.h"
#if defined(__GNUC__) && defined(OPT_HOT_O2) && (OPT_HOT_O2)
#pragma GCC optimize("O2")   /* I5: pagination is the hottest layout path */
#endif
#include "paginate.h"
#include "bridge.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>

#define PGL_MAX_LINES 1000000      /* hard cap: explicit failure, never truncate */
#define PGL_INIT_LINES 256

#ifndef PG_NO_WIDTH_CACHE
/* width cache: measuring a word through the font engine is expensive on
   the target; chapters repeat the same words a lot. */
#define WIDTH_CACHE 1024
typedef struct { uint32_t key; int16_t width; } wc_entry_t;
static wc_entry_t g_wc[WIDTH_CACHE];

static int cached_word_width(const char* word, int len, int font)
{
    if (len < 2 || len > 96) return -1;
    uint32_t key = u_crc32(word, (size_t)len) ^ ((uint32_t)font << 24) ^ (uint32_t)len;
    wc_entry_t* e = &g_wc[key % WIDTH_CACHE];
    if (e->key == key && e->width > 0) return e->width;
    char tmp[128];
    memcpy(tmp, word, len);
    tmp[len] = 0;
    int w = brd_text_w(tmp, font);
    if (w > 32767) w = 32767;
    e->key = key;
    e->width = (int16_t)w;
    return w;
}
#else
#define cached_word_width(word, len, font) (-1)
#endif

typedef struct {
    PgLine* lines;
    int     n;
    int     cap;
} linelist_t;

static int push_line(linelist_t* ll, uint32_t s, uint32_t e, uint16_t flags)
{
    if (ll->n >= ll->cap) {
        int ncap = ll->cap ? ll->cap * 2 : PGL_INIT_LINES;
        if (ncap > PGL_MAX_LINES) return 0;
        PgLine* np = (PgLine*)realloc(ll->lines, (size_t)ncap * sizeof(PgLine));
        if (!np) return 0;
        ll->lines = np;
        ll->cap = ncap;
    }
    ll->lines[ll->n].start = s;
    ll->lines[ll->n].end = e;
    ll->lines[ll->n].flags = flags;
    ll->n++;
    return 1;
}

Layout* pg_layout(const char* text, size_t len, int width, int height, int font, int gap)
{
    if (!text || !len) len = text ? strlen(text) : 0;
    if (width <= 8 || height <= 8) return NULL;

    linelist_t ll;
    ll.lines = NULL;
    ll.n = 0;
    ll.cap = 0;

    int line_h = brd_font_h(font) + 2 + gap;
    if (line_h < 8) line_h = 10;
    int img_h = height / 3;
    if (img_h < 60) img_h = 60;
    if (img_h > height - line_h) img_h = height - line_h;
    int img_block = img_h + 8;

    size_t i = 0;
    uint32_t line_start = 0;
    int line_w = 0;
    int first_word = 1;
    int space_w = brd_text_w(" ", font);

    while (i <= len) {
        char c = (i < len) ? text[i] : '\n';
        if (c == '\n' || c == 0 || i == len) {
            if (!push_line(&ll, line_start, (uint32_t)i, 0)) goto fail;
            i++;
            line_start = (uint32_t)i;
            line_w = 0;
            first_word = 1;
            continue;
        }
        if (c == '\x01') {
            /* image marker: flush line, emit image line */
            if (i > line_start && !push_line(&ll, line_start, (uint32_t)i, 0)) goto fail;
            const char* close = memchr(text + i + 1, '\x02', len - i - 1);
            uint32_t e = close ? (uint32_t)(close - text + 1) : (uint32_t)len;
            if (!push_line(&ll, (uint32_t)i, e, PGL_IMAGE)) goto fail;
            i = e;
            line_start = (uint32_t)i;
            line_w = 0;
            first_word = 1;
            continue;
        }
        if (c == ' ') {
            i++;
            continue;
        }

        /* find word end */
        size_t we = i;
        while (we < len && text[we] != ' ' && text[we] != '\n' && text[we] != '\x01') we++;

        int wl = (int)(we - i);
        if (wl > 96) wl = 96;
        int ww = cached_word_width(text + i, wl, font);
        if (ww < 0) {
            char word[128];
            memcpy(word, text + i, wl);
            word[wl] = 0;
            ww = brd_text_w(word, font);
        }

        if (!first_word && line_w + space_w + ww > width) {
            if (!push_line(&ll, line_start, (uint32_t)i, 0)) goto fail;
            line_start = (uint32_t)i;
            line_w = 0;
            first_word = 1;
        }

        if (first_word) {
            line_w = ww;
            first_word = 0;
        } else {
            line_w += space_w + ww;
        }

        if (ww > width) {
            /* word too long: keep it on its own (overlong) line */
            if (!push_line(&ll, line_start, (uint32_t)we, 0)) goto fail;
            line_start = (uint32_t)we;
            line_w = 0;
            first_word = 1;
        }
        i = we;
    }

    /* assign pages by cumulative height: image lines consume an image
       block, text lines consume line_h. */
    Layout* l = (Layout*)u_calloc(1, sizeof(Layout));
    if (!l) { free(ll.lines); return NULL; }
    l->lines = ll.lines;
    l->nlines = ll.n;
    l->line_h = line_h;
    l->page_first = (int*)u_alloc(sizeof(int) * (ll.n + 2));
    if (!l->page_first) { free(l->lines); free(l); return NULL; }

    int p = 0;
    int y = 0;
    uint32_t first_line = 0;
    for (int li = 0; li < ll.n; li++) {
        int lh = (ll.lines[li].flags & PGL_IMAGE) ? img_block : line_h;
        if (y + lh > height && li > (int)first_line) {
            l->page_first[p++] = (int)first_line;
            first_line = (uint32_t)li;
            y = 0;
        }
        y += lh;
    }
    l->page_first[p++] = (int)first_line;
    l->page_first[p] = ll.n;
    l->npages = p;
    if (l->npages < 1) { l->npages = 1; l->page_first[0] = 0; l->page_first[1] = ll.n; }
    return l;

fail:
    free(ll.lines);
    return NULL;
}

void pg_free(Layout* l)
{
    if (!l) return;
    free(l->lines);
    free(l->page_first);
    free(l);
}

int pg_page_of_offset(const Layout* l, size_t off)
{
    if (!l || l->npages < 1 || l->nlines <= 0) return 0;
    int lo = 0, hi = l->nlines;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if ((size_t)l->lines[mid].start <= off) lo = mid + 1;
        else hi = mid;
    }
    int line_idx = lo > 0 ? lo - 1 : 0;
    lo = 0;
    hi = l->npages;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (l->page_first[mid] <= line_idx) lo = mid + 1;
        else hi = mid;
    }
    return lo > 0 ? lo - 1 : 0;
}

void pg_width_cache_reset(void)
{
#ifndef PG_NO_WIDTH_CACHE
    memset(g_wc, 0, sizeof(g_wc));
#endif
}
