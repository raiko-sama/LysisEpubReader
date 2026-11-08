// epub.h - minimal EPUB (OCF/OPF/NCX) reader
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Epub Epub;

Epub* epub_open(const char* path);
void  epub_close(Epub*);

const char* epub_title(Epub* e);
const char* epub_author(Epub* e);
int         epub_chapter_count(Epub* e);
const char* epub_chapter_title(Epub* e, int i);
const char* epub_chapter_href(Epub* e, int i);

/* decoded plain text of a chapter (psram buffer, caller frees) */
char* epub_chapter_text(Epub* e, int i, size_t* out_len);
/* raw bytes of arbitrary zip entry (psram buffer, caller frees) */
char* epub_entry_extract(Epub* e, const char* name, size_t* out_len);
/* raw image bytes for a chapter-relative or OPF-relative href */
char* epub_image(Epub* e, const char* href, size_t* out_len);
/* raw image bytes for a href relative to the given chapter's folder */
char* epub_image_from(Epub* e, int chapter, const char* href, size_t* out_len);
/* book cover image bytes (psram buffer, caller frees) */
char* epub_cover(Epub* e, size_t* out_len);
/* zip name->index cache misses (instrumentation, host tests) */
int   epub_lookup_misses(Epub* e);

#ifdef __cplusplus
}
#endif
