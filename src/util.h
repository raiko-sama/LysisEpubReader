// util.h - small helpers
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#define localtime_r(a, b) (localtime_s((b), (a)) ? NULL : (b))
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* u_alloc(size_t n);
void* u_calloc(size_t n, size_t sz);
char* u_strdup(const char* s);
char* u_strndup(const char* s, size_t n);

int  u_file_read(const char* path, void** buf, size_t* len);
int  u_file_write(const char* path, const void* buf, size_t len);
/* write via tmp+rename: a power loss never leaves a half-written file */
int  u_file_write_atomic(const char* path, const void* buf, size_t len);
extern int u_write_count;              /* instrumentation: SD writes */
int  u_file_read_int(const char* path, int def);
int  u_file_write_int(const char* path, int v);
int64_t u_file_read_i64(const char* path, int64_t def);
int  u_file_write_i64(const char* path, int64_t v);
int  u_file_read_str(const char* path, char* out, int outlen);
int  u_file_write_str(const char* path, const char* s);
bool u_file_exists(const char* path);
void u_mkdirs(const char* path);

/* buffered diagnostic log mirror: lines accumulate in RAM and are written
   to `path` in one append every 10 lines, after 30 s of staleness, or on
   demand. One SD write per batch instead of one per line. */
void u_blog_append(const char* path, const char* line, uint32_t now_ms);
void u_blog_flush(const char* path);

void u_path_dir(const char* path, char* out, int outlen);
void u_path_join(char* out, int outlen, const char* a, const char* b);
const char* u_basename(const char* path);
void u_strip_ext(char* s);

int  u_icmp(const char* a, const char* b, size_t n);
const char* u_stristr(const char* hay, const char* needle);
const char* u_stristr_len(const char* hay, const char* needle, size_t needle_len);
void u_trim(char* s);
void u_ascii_fold(char* s);            // UTF-8 accents -> ASCII approximation
uint32_t u_crc32(const void* data, size_t n);
uint32_t u_crc32_core(uint32_t crc, const uint8_t* p, size_t n);

/* clamp a rectangle to [0..maxw-1]x[0..maxh-1]; sets w/h to 0 when outside */
void u_clip_rect(int* x, int* y, int* w, int* h, int maxw, int maxh);

#ifdef __cplusplus
}
#endif
