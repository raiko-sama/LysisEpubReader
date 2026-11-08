#include "bridge.h"
#include "util.h"
#include "config.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <esp32-hal-psram.h>
#if defined(ESP32) && OPT_CRASH_GUARD
#include <esp_core_dump.h>
#endif

#if DIAG_LOG
#define BRD_DIAG(...) Serial.printf("[Lysis] diag: " __VA_ARGS__)
#else
#define BRD_DIAG(...) do {} while (0)
#endif

#if __has_include(<USB.h>) && __has_include(<USBMSC.h>)
  #include <USB.h>
  #include <USBMSC.h>
  #define HAVE_USBMSC 1
#else
  #define HAVE_USBMSC 0
#endif

// ------------------------------------------------------------------
// Canvas: draw to PSRAM sprite, push on refresh (e-paper friendly)
// ------------------------------------------------------------------
static M5Canvas g_canvas(&M5.Display);
static M5Canvas* g_draw = &g_canvas;   /* current draw target (R1 slots) */
static bool     g_canvasReady = false;
static int      g_canvas_font_size = -1;
static int      g_rot = 0;
static void     wait_display_collect(void);
static int      utf8_width(lgfx::LGFX_Sprite* c, const char* s);

/* Debug/injection hooks: the autonomous CLI reads these through JTAG and
   injects synthetic touches without a human finger. */
extern "C" {
volatile uint32_t g_dbg_canvas_buf = 0;
volatile uint32_t g_dbg_canvas_w = 0;
volatile uint32_t g_dbg_canvas_h = 0;
#ifdef DEV_INJECT
volatile int g_dbg_inj_x = 0;
volatile int g_dbg_inj_y = 0;
volatile int g_dbg_inj_state = 0; /* 0 idle, 1 press, 2 down, 3 release */
void brd_dbg_touch(int x, int y, int state)
{
    g_dbg_inj_x = x;
    g_dbg_inj_y = y;
    g_dbg_inj_state = state;
}
#endif
}

static void recreate_canvas(void)
{
    if (g_canvasReady) { g_canvas.deleteSprite(); g_canvasReady = false; }
    g_canvas.setPsram(true);
    g_canvas.setColorDepth(16);
    g_canvas_font_size = -1;
    g_canvasReady = g_canvas.createSprite(M5.Display.width(), M5.Display.height());
    if (g_canvasReady) {
        g_dbg_canvas_buf = (uint32_t)g_canvas.getBuffer();
        g_dbg_canvas_w = (uint32_t)g_canvas.width();
        g_dbg_canvas_h = (uint32_t)g_canvas.height();
    }
    g_draw = &g_canvas;
    if (!g_canvasReady) {
        Serial.println("BRD: canvas alloc failed");
    }
}

extern "C" void brd_wdt_feed(void) { esp_task_wdt_reset(); }

extern "C" void brd_log(const char* s)
{
    Serial.printf("[Lysis] %s\n", s);
}

extern "C" int brd_serial_avail(void) { return Serial.available(); }
extern "C" int brd_serial_read(void) { return Serial.read(); }
extern "C" int brd_serial_write(const void* p, int n) { return (int)Serial.write((const uint8_t*)p, (size_t)n); }

static uint64_t s_boot_us = 0;

extern "C" void brd_serial_begin(void)
{
    Serial.begin(115200);   /* USB CDC console (device bring-up logs) */
    delay(30);
#ifndef APP_QEMU
    if (!s_boot_us) s_boot_us = esp_timer_get_time();   /* power -> app marker */
#endif
}

extern "C" uint64_t brd_us(void)
{
#ifdef APP_QEMU
    return (uint64_t)millis() * 1000ULL;
#else
    return esp_timer_get_time();
#endif
}

extern "C" uint64_t brd_boot_us(void)
{
#ifdef APP_QEMU
    return 0;
#else
    return s_boot_us;
#endif
}

extern "C" void brd_begin(bool minimal)
{
#ifdef APP_QEMU
    (void)minimal;
    Serial.begin(115200);
    brd_log("QEMU test mode: hardware init skipped");
    return;
#else
    auto cfg = M5.config();
    cfg.internal_mic = false;          /* the firmware never uses the mic */
    if (minimal) {
        /* wake paths (lock screen only): no IMU, no panel clear; the
           lock screen redraws everything, and the IMU starts lazily
           only if/when auto-rotation needs it again */
        cfg.internal_imu = false;
        cfg.clear_display = false;
#if OPT_WAKE_LEAN
        /* B1: the speaker starts lazily if the alarm screen needs it
           (M5Unified Speaker_Class begins on first playback call) */
        cfg.internal_spk = false;
#endif
    }
    M5.begin(cfg);
    M5.Display.setRotation(0);
    recreate_canvas();
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    Serial.println("BRD: M5 initialized");
#endif
}

extern "C" int brd_reset_reason(void)
{
#ifdef APP_QEMU
    return 0;
#else
    return (int)esp_reset_reason();
#endif
}

extern "C" uint16_t brd_color(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }
extern "C" int  brd_width(void)  { return M5.Display.width() ? M5.Display.width() : 960; }
extern "C" int  brd_height(void) { return M5.Display.height() ? M5.Display.height() : 540; }
extern "C" uint32_t brd_millis(void) { return millis(); }
extern "C" void brd_delay(uint32_t ms) { delay(ms); }

extern "C" int brd_cpu_freq(int mhz)
{
#ifdef APP_QEMU
    (void)mhz;
    return 240;
#else
    if (mhz == 40 || mhz == 80 || mhz == 160 || mhz == 240) {
        if (!setCpuFrequencyMhz(mhz)) return getCpuFrequencyMhz();
    }
    return getCpuFrequencyMhz();
#endif
}

extern "C" void brd_rotation(int r)
{
    g_rot = r;
    M5.Display.setRotation((uint8_t)r);
    recreate_canvas();
}
extern "C" int brd_get_rotation(void) { return g_rot; }

// ------------------------------------------------------------------
// drawing
// ------------------------------------------------------------------
/* UI font ids: 0 = Font0 (8 px), 1 = Font2 (16 px), 2 = Font4 (26 px).
   Reader body font ids 3/4/5 (Small/Medium/Large): the setting selects
   one of these; they only flow through the pagination and the page body,
   never through the chrome. */
static void font_setup(lgfx::LGFX_Sprite* c, int size)
{
    switch (size) {
        case 0:  c->setFont(&fonts::Font0); c->setTextSize(1.0f); break;
        case 2:  c->setFont(&fonts::Font4); c->setTextSize(1.0f); break;
        case 3:  c->setFont(&fonts::Font2); c->setTextSize(1.5f); break;   /* 24 px */
        case 4:  c->setFont(&fonts::Font4); c->setTextSize(1.5f); break;   /* 39 px */
        case 5:  c->setFont(&fonts::Font4); c->setTextSize(2.0f); break;   /* 52 px */
        /* Reader 5-step scale: 80/100/140/160/200% (Font4 ~26 px at 100%) */
        case 6:  c->setFont(&fonts::Font4); c->setTextSize(0.85f); break;
        case 7:  c->setFont(&fonts::Font4); c->setTextSize(1.0f);  break;
        case 8:  c->setFont(&fonts::Font4); c->setTextSize(1.4f);  break;
        case 9:  c->setFont(&fonts::Font4); c->setTextSize(1.6f);  break;
        case 10: c->setFont(&fonts::Font4); c->setTextSize(2.0f);  break;
        case 11: c->setFont(&fonts::Font4); c->setTextSize(3.0f);  break; /* clock ~78 px */
        case 12: c->setFont(&fonts::Font4); c->setTextSize(2.5f);  break; /* alarm ~65 px */
        case 13: c->setFont(&fonts::Font4); c->setTextSize(0.72f); break; /* subtitle ~19 px */
        case 14: c->setFont(&fonts::Font4); c->setTextSize(1.2f);  break; /* reader 120% ~31 px */
        default: c->setFont(&fonts::Font2); c->setTextSize(1.0f); break;
    }
}

static void font_setup_cached(lgfx::LGFX_Sprite* c, int size)
{
    if (c == &g_canvas) {
        if (g_canvas_font_size == size) return;
        font_setup(c, size);
        g_canvas_font_size = size;
        return;
    }
    font_setup(c, size);
}

#define BRD_WCACHE_N 48
static struct {
    bool used;
    int font;
    int width;
    char text[80];
} g_wcache[BRD_WCACHE_N];
static int g_wcache_next = 0;
static bool g_font_h_valid[15];
static int  g_font_h_cache[15];

static int cached_text_width(const char* s, int size)
{
    size_t n = strlen(s);
    if (n < sizeof(g_wcache[0].text)) {
        for (int i = 0; i < BRD_WCACHE_N; i++) {
            if (g_wcache[i].used && g_wcache[i].font == size &&
                !strcmp(g_wcache[i].text, s))
                return g_wcache[i].width;
        }
    }
    int w = utf8_width(&g_canvas, s);
    if (n < sizeof(g_wcache[0].text)) {
        int slot = g_wcache_next++ % BRD_WCACHE_N;
        g_wcache[slot].used = true;
        g_wcache[slot].font = size;
        g_wcache[slot].width = w;
        memcpy(g_wcache[slot].text, s, n + 1);
    }
    return w;
}

extern "C" void brd_clear(uint16_t c) { if (g_canvasReady) g_draw->fillScreen(c); }

/* UTF-8 fallback for Latin-1 letters: the built-in RLE fonts are ASCII-only.
   Draw the base glyph and a small diacritic so Italian text (ArtÃ¹, Ã¨, ...)
   remains readable without changing the font metrics. */
struct UtfAccent { uint16_t code; char base; uint8_t kind; };
static const UtfAccent kAccents[] = {
    {0x00C0,'A',0},{0x00C1,'A',1},{0x00C2,'A',2},{0x00C3,'A',4},{0x00C4,'A',3},
    {0x00C7,'C',5},{0x00C8,'E',0},{0x00C9,'E',1},{0x00CA,'E',2},{0x00CB,'E',3},
    {0x00CC,'I',0},{0x00CD,'I',1},{0x00CE,'I',2},{0x00CF,'I',3},
    {0x00D1,'N',4},{0x00D2,'O',0},{0x00D3,'O',1},{0x00D4,'O',2},{0x00D5,'O',4},{0x00D6,'O',3},
    {0x00D9,'U',0},{0x00DA,'U',1},{0x00DB,'U',2},{0x00DC,'U',3},
    {0x00E0,'a',0},{0x00E1,'a',1},{0x00E2,'a',2},{0x00E3,'a',4},{0x00E4,'a',3},
    {0x00E7,'c',5},{0x00E8,'e',0},{0x00E9,'e',1},{0x00EA,'e',2},{0x00EB,'e',3},
    {0x00EC,'i',0},{0x00ED,'i',1},{0x00EE,'i',2},{0x00EF,'i',3},
    {0x00F1,'n',4},{0x00F2,'o',0},{0x00F3,'o',1},{0x00F4,'o',2},{0x00F5,'o',4},{0x00F6,'o',3},
    {0x00F9,'u',0},{0x00FA,'u',1},{0x00FB,'u',2},{0x00FC,'u',3},
};

static uint32_t utf8_next(const unsigned char*& p)
{
    uint32_t c = *p++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && (*p & 0xC0) == 0x80) {
        uint32_t v = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        return v;
    }
    if ((c & 0xF0) == 0xE0 && p[0] && p[1] &&
        (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        uint32_t v = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
        return v;
    }
    if ((c & 0xF8) == 0xF0) {
        for (int i = 0; i < 3 && p[i]; i++) if ((p[i] & 0xC0) != 0x80) return 0xFFFD;
        uint32_t v = ((c & 7) << 18) | ((p[0] & 0x3F) << 12) |
                     ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        p += 3;
        return v;
    }
    return 0xFFFD;
}

static const UtfAccent* accent_for(uint32_t code)
{
    for (size_t i = 0; i < sizeof(kAccents)/sizeof(kAccents[0]); i++)
        if (kAccents[i].code == code) return &kAccents[i];
    return nullptr;
}

static void draw_accent(lgfx::LGFX_Sprite* c, int x, int y, int fw, int fh,
                        uint8_t kind, uint16_t fg)
{
    int ax = x + fw / 2;
    int ay = y + (fh >= 20 ? 2 : 1);
    switch (kind) {
    case 0:
        c->drawLine(ax - 2, ay, ax + 3, ay + 4, fg);
        c->drawLine(ax - 2, ay + 1, ax + 3, ay + 5, fg);
        break;                                                          /* grave */
    case 1:
        c->drawLine(ax + 2, ay, ax - 3, ay + 4, fg);
        c->drawLine(ax + 2, ay + 1, ax - 3, ay + 5, fg);
        break;                                                          /* acute */
    case 2:
        c->drawLine(ax - 3, ay + 3, ax, ay, fg);
        c->drawLine(ax, ay, ax + 3, ay + 3, fg);
        break;                                                          /* circumflex */
    case 3:
        c->fillRect(ax - 4, ay, 2, 2, fg);
        c->fillRect(ax + 2, ay, 2, 2, fg);
        break;                                                          /* diaeresis */
    case 4:
        c->drawLine(ax - 4, ay + 1, ax - 1, ay, fg);
        c->drawLine(ax - 1, ay, ax + 1, ay + 1, fg);
        c->drawLine(ax + 1, ay + 1, ax + 4, ay, fg);
        break;                                                          /* tilde */
    default:
        c->drawLine(ax, y + fh - 2, ax + 2, y + fh + 1, fg);
        break;                                                          /* cedilla */
    }
}

static int draw_utf8(lgfx::LGFX_Sprite* c, int x, int y, const char* s, uint16_t fg)
{
    int start = x;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        uint32_t code = utf8_next(p);
        char base[2] = {0, 0};
        const UtfAccent* a = accent_for(code);
        if (code < 0x80) {
            base[0] = (char)code;
        } else if (a) {
            base[0] = a->base;
        } else if (code == 0x00A0) {
            base[0] = ' ';
        } else if (code == 0x2018 || code == 0x2019) {
            base[0] = '\'';
        } else if (code == 0x201C || code == 0x201D) {
            base[0] = '"';
        } else if (code == 0x2013 || code == 0x2014) {
            base[0] = '-';
        } else if (code == 0x2026) {
            c->drawString("...", x, y);
            x += c->textWidth("...");
            continue;
        } else {
            base[0] = '?';
        }
        if (!base[0]) continue;
        c->drawString(base, x, y);
        int fw = c->textWidth(base);
        if (a) draw_accent(c, x, y, fw, c->fontHeight(), a->kind, fg);
        x += fw;
    }
    return x - start;
}

static int utf8_width(lgfx::LGFX_Sprite* c, const char* s)
{
    int w = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        uint32_t code = utf8_next(p);
        const UtfAccent* a = accent_for(code);
        char base[2] = {0, 0};
        if (code < 0x80) base[0] = (char)code;
        else if (a) base[0] = a->base;
        else if (code == 0x00A0) base[0] = ' ';
        else if (code == 0x2018 || code == 0x2019) base[0] = '\'';
        else if (code == 0x201C || code == 0x201D) base[0] = '"';
        else if (code == 0x2013 || code == 0x2014) base[0] = '-';
        else if (code == 0x2026) { w += c->textWidth("..."); continue; }
        else base[0] = '?';
        if (base[0]) w += c->textWidth(base);
    }
    return w;
}

static int g_odx = 0, g_ody = 0;
static bool g_anim = false;
extern "C" void brd_draw_offset(int dx, int dy) { g_odx = dx; g_ody = dy; }
extern "C" void brd_anim(bool on) { g_anim = on; }

extern "C" void brd_fill_rect(int x, int y, int w, int h, uint16_t c) { if (g_canvasReady) g_draw->fillRect(x + g_odx, y + g_ody, w, h, c); }
extern "C" void brd_draw_rect(int x, int y, int w, int h, uint16_t c) { if (g_canvasReady) g_draw->drawRect(x + g_odx, y + g_ody, w, h, c); }
extern "C" void brd_hline(int x, int y, int w, uint16_t c) { if (g_canvasReady) g_draw->drawFastHLine(x + g_odx, y + g_ody, w, c); }
extern "C" void brd_line(int x0, int y0, int x1, int y1, uint16_t c) { if (g_canvasReady) g_draw->drawLine(x0 + g_odx, y0 + g_ody, x1 + g_odx, y1 + g_ody, c); }

extern "C" void brd_text(int x, int y, const char* s, uint16_t fg, int size)
{
    if (!g_canvasReady) return;
    font_setup_cached(g_draw, size);
    g_draw->setTextColor(fg);
    draw_utf8(g_draw, x + g_odx, y + g_ody, s, fg);
}

extern "C" int brd_text_w(const char* s, int size)
{
#ifdef APP_QEMU
    (void)size;
    return (int)strlen(s) * 8 + 2;
#else
    if (!g_canvasReady) return 0;
    font_setup_cached(&g_canvas, size);
    return cached_text_width(s, size);
#endif
}

extern "C" int brd_font_h(int size)
{
#ifdef APP_QEMU
    switch (size) {
        case 3: return 24;
        case 4: return 39;
        case 5: return 52;
        case 6: return 22;
        case 7: return 26;
        case 8: return 36;
        case 9: return 42;
        case 10: return 52;
        case 11: return 78;
        case 12: return 65;
        case 13: return 19;
        default: return size == 2 ? 28 : (size == 1 ? 18 : 10);
    }
#else
    if (!g_canvasReady) return 0;
    if (size >= 0 && size < 15 && g_font_h_valid[size]) return g_font_h_cache[size];
    font_setup_cached(&g_canvas, size);
    int h = g_canvas.fontHeight();
    if (size >= 0 && size < 15) {
        g_font_h_cache[size] = h;
        g_font_h_valid[size] = true;
    }
    return h;
#endif
}

extern "C" int brd_text_fit(char* out, int outlen, const char* s, int maxw, int size)
{
    int n = (int)strlen(s);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, s, n);
    out[n] = 0;
#ifdef APP_QEMU
    (void)maxw; (void)size;
    return n;
#else
    if (!g_canvasReady) return n;
    font_setup_cached(&g_canvas, size);
    if (utf8_width(&g_canvas, out) <= maxw) return n;
    /* estimate a cut point by width ratio, then trim precisely */
    int wout = utf8_width(&g_canvas, out);
    int est = wout > 0 ? (int)((int64_t)n * maxw / wout) : n;
    if (est < 1) est = 1;
    if (est > n - 1) est = n - 1;
    if (est > outlen - 4) est = outlen - 4;   /* leave room for "..." */
    int lo = 0, hi = est, best = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        char cand[512];
        int k = mid > (int)sizeof(cand) - 4 ? (int)sizeof(cand) - 4 : mid;
        memcpy(cand, s, k); cand[k] = 0;
        strcat(cand, "...");
        if (utf8_width(&g_canvas, cand) <= maxw) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    est = best;
    memcpy(out, s, est);
    out[est] = 0;
    strcat(out, "...");
    return (int)strlen(out);
#endif
}

extern "C" void brd_draw_rgb565(int x, int y, int w, int h, const uint16_t* px)
{
    if (g_canvasReady) g_draw->pushImage(x + g_odx, y + g_ody, w, h, px);
}

extern "C" int brd_draw_jpeg(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh)
{
    if (!g_canvasReady) return 0;
    return g_draw->drawJpg(p, (uint32_t)n, x + g_odx, y + g_ody, maxw, maxh,
                           0, 0, 1.0f, 0.0f, datum_t::top_left);
}

extern "C" int brd_draw_png(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh)
{
    if (!g_canvasReady) return 0;
    return g_draw->drawPng(p, (uint32_t)n, x + g_odx, y + g_ody, maxw, maxh,
                           0, 0, 1.0f, 0.0f, datum_t::top_left);
}

/* minimal header parsers for the target size of a decoded image */
static bool jpg_dims(const uint8_t* p, size_t n, int* w, int* h)
{
    if (n < 9 || p[0] != 0xFF || p[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 8 < n) {
        if (p[i] != 0xFF) { i++; continue; }
        uint8_t m = p[i + 1];
        if (m == 0xFF) { i++; continue; }                        /* fill byte */
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD9) || m == 0x01) { i += 2; continue; }
        uint16_t len = (uint16_t)((p[i + 2] << 8) | p[i + 3]);
        if (len < 2) return false;
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            *h = (p[i + 5] << 8) | p[i + 6];
            *w = (p[i + 7] << 8) | p[i + 8];
            return *w > 0 && *h > 0;
        }
        i += 2 + len;
    }
    return false;
}
static bool png_dims(const uint8_t* p, size_t n, int* w, int* h)
{
    if (n < 24) return false;
    if (p[0] != 0x89 || p[1] != 'P' || p[2] != 'N' || p[3] != 'G') return false;
    *w = (p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19];
    *h = (p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23];
    return *w > 0 && *h > 0;
}

extern "C" uint16_t* brd_img_decode(const uint8_t* p, size_t n, int maxw, int maxh, uint16_t bg, int* out_w, int* out_h)
{
#ifdef APP_QEMU
    (void)p; (void)n; (void)maxw; (void)maxh; (void)bg; (void)out_w; (void)out_h;
    return nullptr;
#else
    if (!g_canvasReady || !p || !n || !out_w || !out_h) return nullptr;
    int iw = 0, ih = 0;
    bool is_jpg = jpg_dims(p, n, &iw, &ih);
    bool is_png = !is_jpg && png_dims(p, n, &iw, &ih);
    if (!is_jpg && !is_png) return nullptr;

    /* downscale only, keep the aspect ratio (same rule as M5GFX) */
    float r = 1.0f;
    if (iw > maxw && (float)maxw / iw < r) r = (float)maxw / iw;
    if (ih > maxh && (float)maxh / ih < r) r = (float)maxh / ih;
    int tw = (int)(iw * r + 0.5f), th = (int)(ih * r + 0.5f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;

    static M5Canvas g_dec(&M5.Display);
    g_dec.setPsram(true);
    g_dec.setColorDepth(16);
    if (!g_dec.createSprite(tw, th)) return nullptr;
    g_dec.fillSprite(bg);
    float zx = (float)tw / (float)iw;
    float zy = (float)th / (float)ih;
    bool ok = is_jpg
                ? g_dec.drawJpg(p, (uint32_t)n, 0, 0, tw, th, 0, 0, zx, zy, datum_t::top_left)
                : g_dec.drawPng(p, (uint32_t)n, 0, 0, tw, th, 0, 0, zx, zy, datum_t::top_left);
    if (!ok) { g_dec.deleteSprite(); return nullptr; }
    size_t sz = (size_t)tw * (size_t)th * sizeof(uint16_t);
    uint16_t* out = (uint16_t*)ps_malloc(sz);
    if (!out) out = (uint16_t*)malloc(sz);
    if (out) {
        memcpy(out, g_dec.getBuffer(), sz);
        *out_w = tw;
        *out_h = th;
    }
    g_dec.deleteSprite();
    return out;
#endif
}

static uint32_t g_refresh_count = 0;
static uint32_t g_last_refresh = 0;
static brd_perf_refresh_t g_pf_refresh;

extern "C" void brd_perf_last_refresh(brd_perf_refresh_t* out)
{
    if (out) *out = g_pf_refresh;
}

extern "C" bool brd_display_busy(void)
{
#ifdef APP_QEMU
    return false;
#else
    return M5.Display.displayBusy();
#endif
}

extern "C" uint32_t brd_last_refresh_ms(void)
{
#ifdef APP_QEMU
    return millis();
#else
    return g_last_refresh;
#endif
}

extern "C" void brd_refresh(int mode)
{
#ifdef APP_QEMU
    g_refresh_count++;
    g_last_refresh = millis();
    return;
#else
    if (!g_canvasReady) return;
    switch (mode) {
        case 1:  M5.Display.setEpdMode(g_anim ? epd_mode_t::epd_fastest : epd_mode_t::epd_text);   break;
        case 2:  M5.Display.setEpdMode(epd_mode_t::epd_fast);   break;
        default: M5.Display.setEpdMode(g_anim ? epd_mode_t::epd_fastest : epd_mode_t::epd_quality); break;
    }
    uint32_t t0 = millis();
    uint32_t p0 = (uint32_t)esp_timer_get_time();
    g_canvas.pushSprite(0, 0);
    uint32_t p1 = (uint32_t)esp_timer_get_time();
    g_last_refresh = millis();
    wait_display_collect();   /* keep collecting touch while the panel works */
    uint32_t p2 = (uint32_t)esp_timer_get_time();
    g_pf_refresh.pack_us = 0;
    g_pf_refresh.push_us = p1 - p0;
    g_pf_refresh.panel_us = p2 - p1;
    g_pf_refresh.mode = mode;
    g_pf_refresh.w = g_canvas.width();
    g_pf_refresh.h = g_canvas.height();
    BRD_DIAG("refresh full mode=%d ms=%u\n", mode, (unsigned)(millis() - t0));
    g_refresh_count++;
#endif
}

extern "C" uint32_t brd_refresh_count(void) { return g_refresh_count; }

extern "C" void brd_refresh_region(int x, int y, int w, int h, int mode)
{
#ifdef APP_QEMU
    (void)x; (void)y; (void)w; (void)h;
    g_refresh_count++;
    g_last_refresh = millis();
    return;
#else
    if (!g_canvasReady) { brd_refresh(mode); return; }
    int cw = g_canvas.width(), chh = g_canvas.height();
    u_clip_rect(&x, &y, &w, &h, cw, chh);
    if (w <= 0 || h <= 0) return;
    switch (mode) {
        case 1:  M5.Display.setEpdMode(g_anim ? epd_mode_t::epd_fastest : epd_mode_t::epd_text);   break;
        case 2:  M5.Display.setEpdMode(epd_mode_t::epd_fast);   break;
        default: M5.Display.setEpdMode(g_anim ? epd_mode_t::epd_fastest : epd_mode_t::epd_quality); break;
    }
    uint16_t* px = (uint16_t*)g_canvas.getBuffer();
    if (!px) { brd_refresh(mode); return; }
    /* the canvas rows are separated by `cw` pixels: pack the region into a
       contiguous buffer so a single pushImage transfers correct pixels.
       The buffer is persistent (grow-only): no alloc/free per refresh. */
    size_t sz = (size_t)w * h * sizeof(uint16_t);
    static uint16_t* g_pack = nullptr;
    static size_t    g_pack_sz = 0;
    uint16_t* tmp = g_pack;
    if (!tmp || sz > g_pack_sz) {
        free(g_pack);
        g_pack = nullptr;
        g_pack_sz = 0;
        tmp = (uint16_t*)ps_malloc(sz);
        if (!tmp) tmp = (uint16_t*)malloc(sz);
        if (!tmp) { brd_refresh(mode); return; }
        g_pack = tmp;
        g_pack_sz = sz;
    }
    uint32_t pk0 = (uint32_t)esp_timer_get_time();
    for (int j = 0; j < h; j++) {
        memcpy(tmp + (size_t)j * w, px + (size_t)(y + j) * cw + x, (size_t)w * sizeof(uint16_t));
    }
    uint32_t pk1 = (uint32_t)esp_timer_get_time();
    uint32_t t0 = millis();
    M5.Display.pushImage(x, y, w, h, tmp);
    uint32_t pu1 = (uint32_t)esp_timer_get_time();
    g_last_refresh = millis();
    wait_display_collect();   /* keep collecting touch while the panel works */
    uint32_t pu2 = (uint32_t)esp_timer_get_time();
    g_pf_refresh.pack_us = pk1 - pk0;
    g_pf_refresh.push_us = pu1 - pk1;
    g_pf_refresh.panel_us = pu2 - pu1;
    g_pf_refresh.mode = mode;
    g_pf_refresh.w = w;
    g_pf_refresh.h = h;
    BRD_DIAG("refresh region mode=%d x=%d y=%d w=%d h=%d ms=%u\n",
             mode, x, y, w, h, (unsigned)(millis() - t0));
    g_refresh_count++;
#endif
}

extern "C" void brd_set_sleeping(bool s)
{
#ifdef APP_QEMU
    (void)s;
#else
    if (s) M5.Display.sleep();
    else   M5.Display.wakeup();
#endif
}

extern "C" uint16_t* brd_canvas_ptr(void)
{
#ifdef APP_QEMU
    return nullptr;
#else
    return (uint16_t*)g_canvas.getBuffer();
#endif
}

/* ---- off-screen page canvases (R1 pre-render) ---- */
extern "C" void* brd_canvas_alloc(int w, int h)
{
#ifdef APP_QEMU
    (void)w; (void)h;
    return nullptr;
#else
    M5Canvas* c = new M5Canvas(&M5.Display);
    c->setPsram(true);
    c->setColorDepth(16);
    if (!c->createSprite(w, h)) { delete c; return nullptr; }
    return c;
#endif
}

extern "C" void brd_canvas_free(void* cv)
{
#ifdef APP_QEMU
    (void)cv;
#else
    if (!cv) return;
    M5Canvas* c = (M5Canvas*)cv;
    if (g_draw == c) g_draw = &g_canvas;
    delete c;
#endif
}

extern "C" void brd_canvas_target(void* cv)
{
#ifdef APP_QEMU
    (void)cv;
#else
    g_draw = cv ? (M5Canvas*)cv : &g_canvas;
#endif
}

extern "C" void brd_canvas_blit(void* cv, int x, int y)
{
#ifdef APP_QEMU
    (void)cv; (void)x; (void)y;
#else
    if (!cv || !g_canvasReady) return;
    M5Canvas* c = (M5Canvas*)cv;
    g_canvas.pushImage(x, y, c->width(), c->height(), (const uint16_t*)c->getBuffer());
#endif
}

// ------------------------------------------------------------------
// input
// ------------------------------------------------------------------
static uint32_t g_press_ms = 0;
static bool     g_held = false;
static int      g_tx0 = -1, g_ty0 = -1;   /* press origin kept across polls */
#if OPT_TOUCH_INT_GATE
static bool     g_touch_active = false;   /* last poll had a touch in progress */
static uint32_t g_last_int_ms = 0;        /* last INT activity (any event) */
#endif

/* touch events collected while the panel is busy: taps are never lost
   during an e-ink refresh */
#define TOUCH_Q 8
static brd_touch_t g_tq[TOUCH_Q];
static int         g_tq_n = 0;

static void touch_read_raw(brd_touch_t* t)
{
    memset(t, 0, sizeof(*t));
#ifdef APP_QEMU
    return;
#else
#ifdef DEV_INJECT
    if (g_dbg_inj_state) {
        int st = g_dbg_inj_state;
        t->x = g_dbg_inj_x; t->y = g_dbg_inj_y;
        if (st == 1) {
            t->down = true; t->pressed = true;
            t->x0 = t->x; t->y0 = t->y;
            g_tx0 = t->x; g_ty0 = t->y;
            g_press_ms = millis();
            g_held = false;
            g_dbg_inj_state = 2;
        } else if (st == 2) {
            t->down = true;
            t->x0 = g_tx0; t->y0 = g_ty0;
            t->held_ms = millis() - g_press_ms;
            if (!g_held && t->held_ms > 700) { g_held = true; t->held = true; }
        } else {
            t->down = false; t->released = true;
            t->x0 = g_tx0; t->y0 = g_ty0;
            g_tx0 = g_ty0 = -1;
            g_held = false;
            g_dbg_inj_state = 0;
        }
        return;
    }
#endif
    M5.update();               /* refresh touch + button states (M5Unified) */
    auto d = M5.Touch.getDetail();
    t->x = d.x; t->y = d.y;
    t->down = d.isPressed();
    if (d.wasPressed()) {
        t->pressed = true;
        t->x0 = d.x; t->y0 = d.y;
        g_tx0 = d.x; g_ty0 = d.y;
        g_press_ms = millis();
        g_held = false;
    }
    if (d.isPressed()) {
        t->held_ms = millis() - g_press_ms;
        if (!g_held && t->held_ms > 700) { g_held = true; t->held = true; }
    }
    if (d.wasReleased()) {
        t->released = true;
        /* keep the true press origin (a press can be missed during idle) */
        if (g_tx0 >= 0 && g_ty0 >= 0) { t->x0 = g_tx0; t->y0 = g_ty0; }
        else if (t->x0 == 0 && t->y0 == 0) { t->x0 = d.x; t->y0 = d.y; }
        g_tx0 = g_ty0 = -1;
        g_held = false;
    }
#endif
}

static void touch_collect(void)
{
    if (g_tq_n >= TOUCH_Q) return;
    brd_touch_t t;
    touch_read_raw(&t);
    if (t.pressed || t.released || t.held || t.down) g_tq[g_tq_n++] = t;
}

/* wait for the panel while collecting touch events */
static void wait_display_collect(void)
{
    while (M5.Display.displayBusy()) {
        touch_collect();
        delay(5);
    }
}

extern "C" void brd_touch_poll(brd_touch_t* t)
{
    if (g_tq_n > 0) {
        *t = g_tq[0];
        memmove(&g_tq[0], &g_tq[1], sizeof(brd_touch_t) * (size_t)(g_tq_n - 1));
        g_tq_n--;
        return;
    }
#ifdef DEV_INJECT
    if (g_dbg_inj_state) {
        touch_read_raw(t);
        if (t->down || t->pressed || t->released || t->held) g_last_int_ms = millis();
        g_touch_active = t->down;
        return;
    }
#endif
#if OPT_TOUCH_INT_GATE
    /* E2: skip the I2C transaction when the GT911 INT line is inactive.
       Official pinmap: INT = GPIO48, asserted LOW while a touch is in
       progress. Keep polling fully for 150 ms after the last activity so
       drags and edge humps never split. */
#ifndef APP_QEMU
    if (gpio_get_level(GPIO_NUM_48) != 0 && !g_touch_active &&
        (uint32_t)(millis() - g_last_int_ms) > 150) {
        memset(t, 0, sizeof(*t));
        return;
    }
#endif
#endif
    touch_read_raw(t);
#if OPT_TOUCH_INT_GATE
    if (t->down || t->pressed || t->released || t->held) g_last_int_ms = millis();
    g_touch_active = t->down;
#endif
}

extern "C" bool brd_pwr_clicked(void) {
#ifdef APP_QEMU
    return false;
#else
    M5.update();
    return M5.BtnPWR.wasClicked();
#endif
}
extern "C" bool brd_btn_clicked(void) {
#ifdef APP_QEMU
    return false;
#else
    return M5.BtnA.wasClicked();
#endif
}

// ------------------------------------------------------------------
// power & sensors
// ------------------------------------------------------------------
extern "C" int brd_battery(void)
{
#ifdef APP_QEMU
    return 87;
#else
    int lvl = (int)M5.Power.getBatteryLevel();
    if (lvl < 0 || lvl > 100) return -1;
    return lvl;
#endif
}

extern "C" int brd_battery_mv(void)
{
#ifdef APP_QEMU
    return 3900;
#else
    int v = (int)M5.Power.getBatteryVoltage();
    return v > 0 ? v : -1;
#endif
}
extern "C" bool brd_charging(void) {
#ifdef APP_QEMU
    return false;
#else
    return M5.Power.isCharging();
#endif
}

extern "C" bool brd_vbus_present(void)
{
#ifdef APP_QEMU
    return false;
#else
    static bool ready = false;
    if (!ready) { pinMode(5, INPUT); ready = true; }
    return digitalRead(5) != 0;
#endif
}

extern "C" bool brd_chg_stat_high(void)
{
#ifdef APP_QEMU
    return true;
#else
    static bool ready = false;
    if (!ready) { pinMode(4, INPUT); ready = true; }
    return digitalRead(4) != 0;
#endif
}

extern "C" float brd_temp(void)
{
#ifdef APP_QEMU
    return 25.0f;
#else
    float t = NAN;
    if (M5.Imu.isEnabled()) {
        if (!M5.Imu.getTemp(&t)) t = NAN;
    }
    return t;
#endif
}

extern "C" void brd_accel(float* x, float* y, float* z)
{
    *x = *y = *z = 0;
#ifndef APP_QEMU
    if (M5.Imu.isEnabled()) M5.Imu.getAccel(x, y, z);
#endif
}

extern "C" void brd_imu_power(bool on)
{
#ifndef APP_QEMU
    if (on) {
        /* minimal wake boot skipped the IMU: initialize it on demand */
        if (!M5.Imu.isEnabled()) M5.Imu.begin(&M5.In_I2C, M5.getBoard());
    } else {
        if (M5.Imu.isEnabled()) M5.Imu.sleep();   /* saves current */
    }
#endif
}

extern "C" bool brd_imu_enabled(void)
{
#ifdef APP_QEMU
    return true;
#else
    return M5.Imu.isEnabled();
#endif
}

extern "C" void brd_beep_tone(int freq, int ms)
{
#ifndef APP_QEMU
    static bool ready = false;
    if (!ready) {
        ledcSetup(7, (double)freq, 10);
        ledcAttachPin(21, 7);
        ready = true;
    }
    ledcWriteTone(7, (double)freq);
    (void)ms;
#else
    (void)freq; (void)ms;
#endif
}
extern "C" void brd_beep_stop(void) {
#ifndef APP_QEMU
    ledcWriteTone(7, 0);
#endif
}

extern "C" uint32_t brd_sleep_cause(void)
{
    esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
    switch (c) {
        case ESP_SLEEP_WAKEUP_TIMER:  return 1;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
        case ESP_SLEEP_WAKEUP_GPIO:   return 2;
        default: return 0;
    }
}

extern "C" void brd_wait_button_release(void)
{
    while (M5.BtnPWR.isPressed()) delay(10);
}

extern "C" void brd_restart(void)
{
    delay(100);
    esp_restart();
}

/* B2: copy a flash coredump (if any) to the SD and erase it from flash */
extern "C" bool brd_coredump_save(const char* path)
{
#ifdef APP_QEMU
    (void)path;
    return false;
#else
#if OPT_CRASH_GUARD
    size_t addr = 0, size = 0;
    esp_core_dump_image_get(&addr, &size);
    if (!addr || !size || size > (1u << 20)) return false;   /* sanity */
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    /* the dump lives in FLASH: esp_core_dump_image_get returns a flash
       address, not RAM - read it back in chunks and stream it to the SD */
    uint8_t* chunk = (uint8_t*)malloc(4096);
    bool ok = (chunk != NULL);
    if (ok) {
        for (size_t off = 0; off < size; off += 4096) {
            size_t n = (size - off < 4096) ? (size - off) : 4096;
            if (esp_flash_read(NULL, chunk, (uint32_t)(addr + off), (uint32_t)n) != ESP_OK ||
                fwrite(chunk, 1, n, f) != n) {
                ok = false;
                break;
            }
        }
        free(chunk);
    }
    fclose(f);
    if (!ok) { remove(path); return false; }
    esp_core_dump_image_erase();
    return true;
#else
    (void)path;
    return false;
#endif
#endif
}

extern "C" void brd_poweroff(void)
{
#ifdef APP_QEMU
    brd_log("poweroff requested (QEMU: deep sleep forever)");
    esp_deep_sleep_start();
#else
    M5.Display.sleep();
    M5.Speaker.stop();
    M5.Power.powerOff();            // cuts the power latch (Paper S3: pulse on GPIO44)
    esp_deep_sleep_start();         // fallback
#endif
}

extern "C" void brd_deep_sleep(uint64_t us, bool touch_wakeup)
{
#ifdef APP_QEMU
    char buf[96];
    snprintf(buf, sizeof(buf), "deep sleep: timer=%llu us touch_wakeup=%d",
             (unsigned long long)(us == BRD_SLEEP_NO_TIMER ? 0 : us), touch_wakeup ? 1 : 0);
    brd_log(buf);
    if (us != BRD_SLEEP_NO_TIMER && us) esp_sleep_enable_timer_wakeup(us);
    esp_deep_sleep_start();
#else
    M5.Display.sleep();
    M5.Speaker.stop();
    // Paper S3: timer wake works; touch panel INT (GPIO48) is not RTC-capable.
    // BRD_SLEEP_NO_TIMER = sleep until a power cycle (side button).
    if (us == BRD_SLEEP_NO_TIMER) M5.Power.deepSleep(~0ull, touch_wakeup);
    else                          M5.Power.deepSleep(us, touch_wakeup);
#endif
}

/* ---- light sleep in idle (E1, OPT_LIGHT_SLEEP) ----
   Verified on the vendored code and the official docs
   (docs.m5stack.com/en/core/PaperS3):
   N1: GT911 INT is wired to GPIO48 (official pinmap). M5GFX Touch_GT911
       leaves the INT trigger register at its default; the GT911 asserts
       INT LOW while touched -> GPIO_INTR_LOW_LEVEL.
   N2: the single physical button is wired to the PMS150G power latch
       (click = power on, double-click = power off, long press = download
       mode) - it is NOT readable from a GPIO, so it cannot wake light
       sleep; the button latency stays <= max_ms (<= 500 ms).
   N3: IDF 4.4 compensates esp_timer across light sleep (millis stays
       consistent); device validation still required.
   N4: we never sleep while the EPD is busy (displayBusy check below);
       LCD_CAM/DMA survival across light sleep to be validated on device.
   N5: the precompiled Arduino sdkconfig (dio_opi) has no CONFIG_PM_ENABLE
       and no FREERTOS tickless idle -> manual light sleep only. */
#define LS_MIN_MS 40

static bool usb_msc_active(void);

extern "C" brd_ls_t brd_idle_sleep(uint32_t max_ms)
{
#ifdef APP_QEMU
    delay(max_ms);
    return BRD_LS_SKIPPED;
#else
#if !OPT_LIGHT_SLEEP
    delay(max_ms);
    return BRD_LS_SKIPPED;
#else
    if (max_ms < LS_MIN_MS) { delay(max_ms); return BRD_LS_SKIPPED; }
    if (brd_wifi_active() || brd_bt_active()) { delay(max_ms); return BRD_LS_SKIPPED; }
    if (usb_msc_active()) { delay(max_ms); return BRD_LS_SKIPPED; }   /* USB host owns the bus */
    if (M5.Display.displayBusy()) M5.Display.waitDisplay();    /* never during an EPD refresh */
    if (gpio_get_level(GPIO_NUM_48) == 0) return BRD_LS_TOUCH; /* INT already active */
    gpio_wakeup_enable(GPIO_NUM_48, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup((uint64_t)max_ms * 1000ULL);
    Serial.flush();
    esp_light_sleep_start();
    gpio_wakeup_disable(GPIO_NUM_48);
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return BRD_LS_TIMER;
        case ESP_SLEEP_WAKEUP_GPIO:  return BRD_LS_TOUCH;
        default:                     return BRD_LS_OTHER;
    }
#endif
#endif
}

/* ---- memory telemetry (Q2) ---- */
extern "C" uint32_t brd_heap_free(void)
{
#ifdef APP_QEMU
    return 200000;
#else
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
}

extern "C" uint32_t brd_psram_free(void)
{
#ifdef APP_QEMU
    return 8u * 1024u * 1024u;
#else
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
}

extern "C" uint32_t brd_psram_largest(void)
{
#ifdef APP_QEMU
    return 8u * 1024u * 1024u;
#else
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#endif
}

// ------------------------------------------------------------------
// wifi / ntp
// ------------------------------------------------------------------
extern "C" void brd_apply_tz(int utc_hours)
{
    setenv("TZ", "", 1);
    char tz[32];
    if (utc_hours < 0) snprintf(tz, sizeof(tz), "UTC%d", utc_hours);
    else               snprintf(tz, sizeof(tz), "UTC-%d", utc_hours);
    setenv("TZ", tz, 1);
    tzset();
}

static brd_wifi_progress_fn g_wifi_cb = nullptr;
extern "C" void brd_wifi_progress_cb(brd_wifi_progress_fn cb) { g_wifi_cb = cb; }

extern "C" int brd_wifi_sync(const char* ssid, const char* pass, int utc_hours, int timeout_s,
                             const uint8_t* bssid, int channel)
{
#ifdef APP_QEMU
    (void)ssid; (void)pass; (void)utc_hours; (void)timeout_s; (void)bssid; (void)channel;
    brd_log("wifi_sync: not available in QEMU");
    return 1;
#else
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   /* short session: faster is cheaper than modem sleep */
    if (ssid && ssid[0]) {
#if OPT_WIFI_FAST
        if (bssid && channel >= 1 && channel <= 14) {
            WiFi.begin(ssid, pass && pass[0] ? pass : nullptr, channel, bssid, true);
        } else
#endif
        {
            WiFi.begin(ssid, pass && pass[0] ? pass : nullptr);
        }
    } else {
        if (WiFi.status() != WL_CONNECTED) return 1;
    }
    uint32_t t0 = millis();
    uint32_t win = (uint32_t)timeout_s * 1000UL;
    while (WiFi.status() != WL_CONNECTED) {
        if (win && g_wifi_cb) {
            int pct = (int)((millis() - t0) * 60UL / win);
            if (pct > 59) pct = 59;
            g_wifi_cb(pct);
        }
        if (millis() - t0 > win) {
            WiFi.disconnect(true);
            if (g_wifi_cb) g_wifi_cb(0);
            BRD_DIAG("wifi fail (connect timeout)\n");
            return 1;
        }
        delay(100);
    }
    BRD_DIAG("wifi connected ssid='%s' rssi=%d\n", ssid ? ssid : "", (int)WiFi.RSSI());
    if (g_wifi_cb) g_wifi_cb(60);
    configTime(utc_hours * 3600, 0, "pool.ntp.org", "time.nist.gov");
    t0 = millis();
    struct tm ti;
    while (!getLocalTime(&ti, 0)) {
        if (g_wifi_cb) {
            int pct = 60 + (int)((millis() - t0) * 39UL / 20000UL);
            if (pct > 99) pct = 99;
            g_wifi_cb(pct);
        }
        if (millis() - t0 > 20000UL) { if (g_wifi_cb) g_wifi_cb(0); BRD_DIAG("wifi fail (ntp timeout)\n"); return 2; }
        delay(100);
    }
    BRD_DIAG("wifi ntp ok\n");
    if (g_wifi_cb) g_wifi_cb(100);
    return 0;
#endif
}

extern "C" int brd_wifi_scan(char (*ssids)[33], int max)
{
#ifdef APP_QEMU
    (void)ssids; (void)max;
    return 0;
#else
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (n < 0) n = 0;
    BRD_DIAG("wifi scan n=%d\n", n);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        snprintf(ssids[i], 33, "%s", WiFi.SSID(i).c_str());
    }
    WiFi.scanDelete();
    return n;
#endif
}

extern "C" void brd_wifi_off(void)
{
#ifndef APP_QEMU
    if (WiFi.getMode() != WIFI_OFF) BRD_DIAG("wifi off\n");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
}

extern "C" void brd_wifi_bssid(uint8_t out[6])
{
    memset(out, 0, 6);
#ifndef APP_QEMU
    if (WiFi.status() == WL_CONNECTED) {
        uint8_t* b = WiFi.BSSID();
        if (b) memcpy(out, b, 6);
    }
#endif
}

extern "C" int brd_wifi_channel(void)
{
#ifdef APP_QEMU
    return 0;
#else
    return (WiFi.status() == WL_CONNECTED) ? (int)WiFi.channel() : 0;
#endif
}

extern "C" bool brd_wifi_active(void)
{
#ifdef APP_QEMU
    return false;
#else
    return WiFi.status() == WL_CONNECTED;
#endif
}

#if __has_include(<esp_bt.h>)
#include <esp_bt.h>
#define HAVE_ESP_BT 1
#else
#define HAVE_ESP_BT 0
#endif

extern "C" bool brd_bt_active(void)
{
#if HAVE_ESP_BT && !defined(APP_QEMU)
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
#else
    return false;
#endif
}

extern "C" void brd_bt_off(void)
{
#if HAVE_ESP_BT && !defined(APP_QEMU)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
    }
#endif
}

extern "C" bool brd_time_valid(void) { return time(nullptr) > 1700000000; }

extern "C" bool brd_get_localtime(int* y, int* mo, int* d, int* h, int* mi, int* s)
{
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    struct tm ti;
    localtime_r(&now, &ti);
    if (y) *y = ti.tm_year + 1900;
    if (mo) *mo = ti.tm_mon + 1;
    if (d) *d = ti.tm_mday;
    if (h) *h = ti.tm_hour;
    if (mi) *mi = ti.tm_min;
    if (s) *s = ti.tm_sec;
    return true;
}

extern "C" int64_t brd_epoch(void) { return (int64_t)time(nullptr); }

extern "C" void brd_rtc_sync(void)
{
#ifndef APP_QEMU
    if (M5.Rtc.isEnabled()) {
        time_t now = time(nullptr);
        struct tm ti;
        gmtime_r(&now, &ti);
        M5.Rtc.setDateTime(&ti);   /* BM8563 keeps time while the SoC is off */
    }
#endif
}

// ------------------------------------------------------------------
// SD card (SPI). Pins for M5Stack Paper S3.
// ------------------------------------------------------------------
#ifndef BRD_SD_SCK
#define BRD_SD_SCK  39
#endif
#ifndef BRD_SD_MISO
#define BRD_SD_MISO 40
#endif
#ifndef BRD_SD_MOSI
#define BRD_SD_MOSI 38
#endif
#ifndef BRD_SD_CS
#define BRD_SD_CS   47
#endif

static bool g_sd_ok = false;

extern "C" bool brd_sd_mount(void)
{
#ifdef APP_QEMU
    brd_log("SD mount: simulated OK (QEMU)");
    g_sd_ok = true;
    return true;
#else
    if (g_sd_ok) return true;
    SPI.begin(BRD_SD_SCK, BRD_SD_MISO, BRD_SD_MOSI, BRD_SD_CS);
    g_sd_ok = SD.begin(BRD_SD_CS, SPI, 20000000);
    return g_sd_ok;
#endif
}

extern "C" bool brd_sd_remount(void)
{
#ifdef APP_QEMU
    g_sd_ok = true;
    return true;
#else
    /* M5.begin re-initializes the shared SPI bus (display/touch bring-up):
       the SD handle goes stale and every access fails with sdSelectCard().
       Re-select the card after the display is up; verify it actually
       answers before declaring success. */
    SD.end();
    SPI.end();
    delay(20);
    for (int i = 0; i < 2; i++) {
        SPI.begin(BRD_SD_SCK, BRD_SD_MISO, BRD_SD_MOSI, BRD_SD_CS);
        if (SD.begin(BRD_SD_CS, SPI, 20000000) && SD.cardSize() > 0) {
            g_sd_ok = true;
            BRD_DIAG("SD remount: OK (attempt %d)\n", i + 1);
            return true;
        }
        SD.end();
        SPI.end();
        delay(50);
    }
    g_sd_ok = false;
    BRD_DIAG("SD remount: FAILED\n");
    return false;
#endif
}
extern "C" bool brd_sd_ok(void) { return g_sd_ok; }
extern "C" uint32_t brd_sd_num_sectors(void) { return g_sd_ok ? (uint32_t)SD.numSectors() : 0; }
extern "C" uint32_t brd_sd_sector_size(void) { return g_sd_ok ? (uint32_t)SD.sectorSize() : 0; }
extern "C" uint64_t brd_sd_size(void) {
#ifdef APP_QEMU
    return 8ULL * 1024 * 1024 * 1024;
#else
    return g_sd_ok ? (uint64_t)SD.cardSize() : 0;
#endif
}
extern "C" uint64_t brd_sd_free(void)
{
#ifdef APP_QEMU
    return 4ULL * 1024 * 1024 * 1024;
#else
    if (!g_sd_ok) return 0;
    return (uint64_t)SD.totalBytes() - (uint64_t)SD.usedBytes();
#endif
}

// ------------------------------------------------------------------
// USB mass storage
// ------------------------------------------------------------------
#if HAVE_USBMSC
static USBMSC g_msc;
static bool   g_msc_on = false;
static bool usb_msc_active(void) { return g_msc_on; }
#else
static bool usb_msc_active(void) { return false; }
#endif

static uint8_t g_msc_sec[512] __attribute__((aligned(4)));
static uint32_t g_msc_lba0 = 0;      /* exposed partition start */
static uint32_t g_msc_blocks = 0;    /* exposed partition length */
static uint32_t g_msc_ss = 512;      /* sector size at MSC start */
static uint32_t g_msc_nsec = 0;      /* physical sector count at MSC start */

static int32_t msc_read(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    if (!g_sd_ok || !buffer) return -1;
    uint32_t ss = g_msc_ss;
    uint32_t nsec = g_msc_nsec;
    if (lba + g_msc_lba0 >= nsec) return -1;
    lba += g_msc_lba0;
    uint8_t* dst = (uint8_t*)buffer;
    uint32_t done = 0;
    while (done < bufsize) {
        if (lba >= nsec) return -1;
        if (!SD.readRAW(g_msc_sec, lba)) return -1;
        uint32_t off = (done == 0) ? offset : 0;
        if (off >= ss) return -1;
        uint32_t take = ss - off;
        if (take > bufsize - done) take = bufsize - done;
        memcpy(dst + done, g_msc_sec + off, take);
        done += take;
        lba++;
    }
    return (int32_t)done;
}
static int32_t msc_write(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    if (!g_sd_ok || !buffer) return -1;
    uint32_t ss = g_msc_ss;
    uint32_t nsec = g_msc_nsec;
    if (lba + g_msc_lba0 >= nsec) return -1;
    lba += g_msc_lba0;
    uint8_t* src = (uint8_t*)buffer;
    uint32_t done = 0;
    while (done < bufsize) {
        if (lba >= nsec) return -1;
        uint32_t off = (done == 0) ? offset : 0;
        if (off > ss) off = ss;
        uint32_t take = ss - off;
        if (take > bufsize - done) take = bufsize - done;
        if (take < ss) {
            if (!SD.readRAW(g_msc_sec, lba)) return -1;   /* read-modify-write */
        }
        memcpy(g_msc_sec + off, src + done, take);
        if (!SD.writeRAW(g_msc_sec, lba)) return -1;
        done += take;
        lba++;
    }
    return (int32_t)done;
}
static bool msc_startstop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition; (void)start; (void)load_eject;
    return true;
}

extern "C" int brd_usb_disk_start(void)
{
#ifdef APP_QEMU
    return -3;
#else
#if HAVE_USBMSC
    if (g_msc_on) return 0;
    if (!g_sd_ok || SD.numSectors() == 0) {
        if (!brd_sd_remount() || SD.numSectors() == 0) return -1;
    }
    uint32_t msc_sectors = (uint32_t)SD.numSectors();
    uint32_t msc_ss = (uint32_t)SD.sectorSize();
    if (!msc_sectors || msc_ss != 512) return -2;
    g_msc_ss = msc_ss;
    g_msc_nsec = (uint32_t)SD.numSectors();
    g_msc_lba0 = 0;
    g_msc_blocks = msc_sectors;
    /* Superfloppy: expose the FAT partition starting at LBA 0. */
    if (SD.readRAW(g_msc_sec, 0) && g_msc_sec[510] == 0x55 && g_msc_sec[511] == 0xAA) {
        for (int i = 0; i < 4; i++) {
            const uint8_t* e = g_msc_sec + 446 + i * 16;
            uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
            uint32_t cnt = (uint32_t)e[12] | ((uint32_t)e[13] << 8) | ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
            if (e[4] != 0 && lba && cnt && lba + cnt <= msc_sectors) {
                g_msc_lba0 = lba;
                g_msc_blocks = cnt;
                break;
            }
        }
    }
    msc_sectors = g_msc_blocks;
    g_msc.vendorID("Lysis");
    g_msc.productID("Lysis Reader");
    g_msc.productRevision("1.0");
    g_msc.onRead(msc_read);
    g_msc.onWrite(msc_write);
    g_msc.onStartStop(msc_startstop);
    g_msc.mediaPresent(true);
    if (!g_msc.begin(msc_sectors, 512)) return -2;
    USB.begin();
    g_msc_on = true;
    BRD_DIAG("usb start ok (sectors=%u ss=%u)\n", (unsigned)msc_sectors, (unsigned)msc_ss);
    return 0;
#else
    return -3;
#endif
#endif
}

extern "C" void brd_usb_disk_stop(void)
{
#if HAVE_USBMSC
    if (!g_msc_on) return;
    g_msc.end();
    g_msc_on = false;
#endif
}
