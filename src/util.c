#include "config.h"
#if defined(__GNUC__) && defined(OPT_HOT_O2) && (OPT_HOT_O2)
#pragma GCC optimize("O2")   /* I5: CRC32 / u_stristr are hot */
#endif
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <esp32-hal-psram.h>
#if defined(ESP32)
#include "esp_rom_crc.h"
#endif

#if defined(ESP32) && !defined(HOST_GUI)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_sd_mtx = NULL;

void u_sd_lock_init(void)
{
    if (!s_sd_mtx) s_sd_mtx = xSemaphoreCreateRecursiveMutex();
}

void u_sd_lock(void)
{
    if (s_sd_mtx) xSemaphoreTakeRecursive(s_sd_mtx, portMAX_DELAY);
}

void u_sd_unlock(void)
{
    if (s_sd_mtx) xSemaphoreGiveRecursive(s_sd_mtx);
}
#else
void u_sd_lock_init(void) {}
void u_sd_lock(void) {}
void u_sd_unlock(void) {}
#endif

void* u_alloc(size_t n)
{
    void* p = ps_malloc(n);
    if (!p) p = malloc(n);
    return p;
}

void* u_calloc(size_t n, size_t sz)
{
    if (n && sz > SIZE_MAX / n) return NULL;
    size_t total = n * sz;
    void* p = u_alloc(total);
    if (p) memset(p, 0, total);
    return p;
}

int u_write_count = 0;

int u_file_write(const char* path, const void* buf, size_t len)
{
    u_sd_lock();
    FILE* f = fopen(path, "wb");
    if (!f) { u_sd_unlock(); return -1; }
    size_t wr = fwrite(buf, 1, len, f);
    fclose(f);
    u_write_count++;
    u_sd_unlock();
    return wr == len ? 0 : -1;
}

int u_file_write_atomic(const char* path, const void* buf, size_t len)
{
    u_sd_lock();
    char tmp[288];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) { u_sd_unlock(); return -1; }
    FILE* f = fopen(tmp, "wb");
    if (!f) { u_sd_unlock(); return -1; }
    size_t wr = fwrite(buf, 1, len, f);
    fclose(f);
    if (wr != len) { remove(tmp); u_sd_unlock(); return -1; }
    remove(path);   /* FATFS/win32 rename() cannot reliably overwrite */
    if (rename(tmp, path) != 0) { remove(tmp); u_sd_unlock(); return -1; }
    u_write_count++;
    u_sd_unlock();
    return 0;
}

int u_file_read_int(const char* path, int def)
{
    u_sd_lock();
    FILE* f = fopen(path, "rb");
    if (!f) { u_sd_unlock(); return def; }
    int v = def;
    fread(&v, sizeof(v), 1, f);
    fclose(f);
    u_sd_unlock();
    return v;
}

int u_file_write_int(const char* path, int v)
{
    return u_file_write(path, &v, sizeof(v));
}

int64_t u_file_read_i64(const char* path, int64_t def)
{
    u_sd_lock();
    FILE* f = fopen(path, "rb");
    if (!f) { u_sd_unlock(); return def; }
    int64_t v = def;
    fread(&v, sizeof(v), 1, f);
    fclose(f);
    u_sd_unlock();
    return v;
}

int u_file_read_str(const char* path, char* out, int outlen)
{
    u_sd_lock();
    out[0] = 0;
    FILE* f = fopen(path, "rb");
    if (!f) { u_sd_unlock(); return -1; }
    int n = (int)fread(out, 1, outlen - 1, f);
    out[n > 0 ? n : 0] = 0;
    fclose(f);
    u_sd_unlock();
    return 0;
}

void u_mkdirs(const char* path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0777);
#endif
            *p = '/';
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0777);
#endif
}

void u_path_join(char* out, int outlen, const char* a, const char* b)
{
    if (!a || !a[0]) { snprintf(out, outlen, "%s", b); return; }
    size_t n = strlen(a);
    if (a[n - 1] == '/') snprintf(out, outlen, "%s%s", a, b);
    else snprintf(out, outlen, "%s/%s", a, b);
}

const char* u_basename(const char* path)
{
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

void u_strip_ext(char* s)
{
    char* d = strrchr(s, '.');
    if (d && d != s) *d = 0;
}

int u_icmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        if (!ca) break;
    }
    return 0;
}

const char* u_stristr_len(const char* hay, const char* needle, size_t nl)
{
    static unsigned char fold[256];
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 256; i++) fold[i] = (i >= 'A' && i <= 'Z') ? (unsigned char)(i + 32) : (unsigned char)i;
        init = 1;
    }
    unsigned char n0 = fold[(unsigned char)needle[0]];
    if (n0 == 0) return hay;
    const unsigned char* np = (const unsigned char*)needle;
    for (const unsigned char* h = (const unsigned char*)hay; *h; h++) {
        if (fold[*h] != n0) continue;
        size_t k = 1;
        while (k < nl && fold[h[k]] == fold[np[k]]) k++;
        if (k == nl) return (const char*)h;
    }
    return NULL;
}

const char* u_stristr(const char* hay, const char* needle)
{
    return u_stristr_len(hay, needle, strlen(needle));
}

void u_trim(char* s)
{
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

void u_clip_rect(int* x, int* y, int* w, int* h, int maxw, int maxh)
{
    if (!x || !y || !w || !h || maxw <= 0 || maxh <= 0) return;
    int x0 = *x, y0 = *y;
    int x1 = *x + *w, y1 = *y + *h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > maxw) x1 = maxw;
    if (y1 > maxh) y1 = maxh;
    if (x1 < x0 || y1 < y0) { *w = 0; *h = 0; *x = 0; *y = 0; return; }
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
}

/* CRC-32 core with a running init value (standard polynomial, no final xor).
   On the target the ROM implementation is used: esp_rom_crc32_le is the same
   polynomial with init 0 and no xor, hence the final XOR here. The host
   path uses the software table; u_crc32_core(0,...) must equal the ROM
   result (documented check value 0xC704DD7B for "123456789"). */
uint32_t u_crc32_core(uint32_t crc, const uint8_t* p, size_t n)
{
    static uint32_t table[256];
    static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = 1;
    }
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t u_crc32(const void* data, size_t n)
{
#if defined(ESP32)
    /* ROM table (already in flash): standard zlib convention = ROM core
       with init 0xFFFFFFFF XORed with 0xFFFFFFFF at the end */
    return esp_rom_crc32_le(0xFFFFFFFFu, (const uint8_t*)data, (uint32_t)n) ^ 0xFFFFFFFFu;
#else
    return u_crc32_core(0xFFFFFFFFu, (const uint8_t*)data, n) ^ 0xFFFFFFFFu;
#endif
}
