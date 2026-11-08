// paginate.h - word-wrap layout with pages for the reader
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PGL_IMAGE 1
#define PGL_HEAD  2

typedef struct {
    uint32_t start;   /* offset in text */
    uint32_t end;     /* exclusive */
    uint16_t flags;
} PgLine;

typedef struct {
    PgLine* lines;
    int     nlines;
    int*    page_first;  /* npages+1 entries: first line index of page */
    int     npages;
    int     line_h;
} Layout;

Layout* pg_layout(const char* text, size_t len, int width, int height, int font, int gap);
void    pg_free(Layout* l);
int     pg_page_of_offset(const Layout* l, size_t off);
void    pg_width_cache_reset(void);
