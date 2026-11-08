// htmltext.h - HTML/XHTML to plain text conversion for EPUB chapters
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Converts html to text. Image tags become "\x01<src>\x02" markers on their
// own line. Returns psram buffer (free with free()). out_len may be NULL.
char* ht_to_text(const char* html, size_t len, size_t* out_len);

#ifdef __cplusplus
}
#endif
