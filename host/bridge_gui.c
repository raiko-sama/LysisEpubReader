// host/bridge_gui.c - console UI emulator: implements the same C API as
// src/bridge.h on top of a 960x540 grayscale framebuffer that is rendered
// to the terminal as ASCII art. Touch input is injected by a script.
#include "../src/bridge.h"
#include "font5x7.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <windows.h>
#include <time.h>

#define FB_W 540
#define FB_H 960
static uint8_t fb[FB_H][FB_W];
static int g_rot = 0;

static uint32_t g_refresh = 0;
static int g_cpu = 240;
static int g_odx = 0, g_ody = 0;
static bool g_anim = false;

void brd_draw_offset(int dx, int dy) { g_odx = dx; g_ody = dy; }
void brd_anim(bool on) { g_anim = on; }

static void (*g_frame_cb)(void) = NULL;
void host_set_frame_cb(void (*cb)(void)) { g_frame_cb = cb; }

uint16_t brd_color(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

static inline uint8_t gray_of(uint16_t c)
{
    uint8_t r = (c >> 8) & 0xF8;
    uint8_t g = (c >> 3) & 0xFC;
    uint8_t b = (c << 3) & 0xF8;
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

void brd_serial_begin(void) {}
void brd_begin(bool minimal) { (void)minimal; memset(fb, 255, sizeof(fb)); }
void brd_log(const char* s) { printf("[Lysis] %s\n", s); }
void brd_log_sync(const char* s) { printf("[Lysis] %s\n", s); fflush(stdout); }

uint64_t brd_us(void) { return (uint64_t)GetTickCount64() * 1000ULL; }
uint64_t brd_boot_us(void) { return 0; }
uint32_t brd_heap_free(void) { return 200000; }
uint32_t brd_psram_free(void) { return 8u * 1024u * 1024u; }
uint32_t brd_psram_largest(void) { return 8u * 1024u * 1024u; }
static uint32_t g_last_refresh = 0;
bool brd_display_busy(void) { return false; }
uint32_t brd_last_refresh_ms(void) { return g_last_refresh; }

int  brd_width(void)  { return (g_rot & 1) ? FB_H : FB_W; }
int  brd_height(void) { return (g_rot & 1) ? FB_W : FB_H; }
void brd_rotation(int r) { g_rot = r & 3; }
int  brd_get_rotation(void) { return g_rot; }
uint32_t brd_millis(void) { return (uint32_t)GetTickCount64(); }
void brd_delay(uint32_t ms) { if (ms > 2000) ms = 10; Sleep(ms); }
int  brd_cpu_freq(int mhz) { if (mhz == 80 || mhz == 160 || mhz == 240) g_cpu = mhz; return g_cpu; }
int  host_cpu(void) { return g_cpu; }

/* logical -> physical pixel with the current rotation.
   Physical frame buffer is always FB_W x FB_H (540x960); logical size
   depends on rotation: 0/2 = 540x960, 1/3 = 960x540. */
static void px(int x, int y, uint8_t v)
{
    x += g_odx; y += g_ody;
    int X = 0, Y = 0;
    switch (g_rot) {
    case 0: X = x;                Y = y;               break;
    case 1: X = y;                Y = FB_H - 1 - x;    break;
    case 2: X = FB_W - 1 - x;     Y = FB_H - 1 - y;    break;
    case 3: X = FB_W - 1 - y;     Y = x;               break;
    }
    if (X >= 0 && X < FB_W && Y >= 0 && Y < FB_H) fb[Y][X] = v;
}

static uint8_t fb_get(int x, int y)
{
    int X = 0, Y = 0;
    switch (g_rot) {
    case 0: X = x;                Y = y;               break;
    case 1: X = y;                Y = FB_H - 1 - x;    break;
    case 2: X = FB_W - 1 - x;     Y = FB_H - 1 - y;    break;
    case 3: X = FB_W - 1 - y;     Y = x;               break;
    }
    if (X >= 0 && X < FB_W && Y >= 0 && Y < FB_H) return fb[Y][X];
    return 255;
}

void brd_clear(uint16_t c)
{
    uint8_t g = gray_of(c);
    int sx = g_odx, sy = g_ody;
    g_odx = g_ody = 0;
    if (g_rot == 0) { memset(fb, g, sizeof(fb)); }
    else {
        int W = brd_width(), H = brd_height();
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) px(x, y, g);
    }
    g_odx = sx; g_ody = sy;
}
void brd_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    uint8_t g = gray_of(c);
    if (g_rot == 0) {
        for (int j = 0; j < h; j++) {
            int yy = y + j;
            if (yy < 0 || yy >= FB_H) continue;
            int x0 = x < 0 ? 0 : x;
            int x1 = x + w > FB_W ? FB_W : x + w;
            if (x0 < x1) memset(&fb[yy][x0], g, (size_t)(x1 - x0));
        }
        return;
    }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) px(x + i, y + j, g);
}
void brd_draw_rect(int x, int y, int w, int h, uint16_t c)
{
    uint8_t g = gray_of(c);
    for (int i = 0; i < w; i++) { px(x + i, y, g); px(x + i, y + h - 1, g); }
    for (int i = 0; i < h; i++) { px(x, y + i, g); px(x + w - 1, y + i, g); }
}
void brd_hline(int x, int y, int w, uint16_t c)
{
    uint8_t g = gray_of(c);
    for (int i = 0; i < w; i++) px(x + i, y, g);
}
void brd_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        px(x0, y0, gray_of(c));
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static int scale_for(int size)
{
    switch (size) {
    case 0: return 1;
    case 2: case 3: return 3;
    case 4: return 5;
    case 5: return 6;
    case 6: return 2;
    case 7: return 2;
    case 8: return 3;
    case 9: return 4;
    case 10: return 5;
    case 11: return 9;
    case 12: return 7;
    default: return 2;
    }
}

void brd_text(int x, int y, const char* s, uint16_t fg, int size)
{
    int sc = scale_for(size);
    uint8_t g = gray_of(fg);
    int cx = x;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (*p < 32 || *p > 126) { cx += 6 * sc; continue; }
        const uint8_t* glyph = FONT5X7[*p - 32];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (1 << col)) {
                    for (int sy = 0; sy < sc; sy++)
                        for (int sx = 0; sx < sc; sx++)
                            px(cx + col * sc + sx, y + row * sc + sy, g);
                }
            }
        }
        cx += 6 * sc;
    }
}
int  brd_text_w(const char* s, int size) { return (int)strlen(s) * 6 * scale_for(size) + 2; }
int  brd_font_h(int size) { return 8 * scale_for(size); }
int  brd_text_fit(char* out, int outlen, const char* s, int maxw, int size)
{
    int maxc = maxw / (6 * scale_for(size)) - 3;
    if (maxc < 1) maxc = 1;
    int n = (int)strlen(s);
    if (n > maxc) {
        if (maxc > outlen - 4) maxc = outlen - 4;
        if (maxc < 1) maxc = 1;
        memcpy(out, s, maxc);
        strcpy(out + maxc, "...");
    } else {
        snprintf(out, outlen, "%s", s);
    }
    return (int)strlen(out);
}
void brd_draw_rgb565(int x, int y, int w, int h, const uint16_t* px16)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px(x + i, y + j, gray_of(px16[j * w + i]));
}
static int draw_image(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc* img = stbi_load_from_memory((const stbi_uc*)p, (int)n, &w, &h, &comp, 4);
    if (!img) return 0;
    int dw = w, dh = h;
    if (dw > maxw) { dh = dh * maxw / dw; dw = maxw; }
    if (dh > maxh) { dw = dw * maxh / dh; dh = maxh; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    for (int j = 0; j < dh; j++) {
        int sy = j * h / dh;
        for (int i = 0; i < dw; i++) {
            int sx = i * w / dw;
            const stbi_uc* sp = &img[((size_t)sy * w + sx) * 4];
            unsigned gray = (sp[0] * 77 + sp[1] * 150 + sp[2] * 29) >> 8;
            unsigned a = sp[3];
            /* composite over the white page background */
            unsigned v = (gray * a + 255 * (255 - a)) / 255;
            px(x + i, y + j, (uint8_t)v);
        }
    }
    stbi_image_free(img);
    return dw;
}

int  brd_draw_jpeg(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh)
{ return draw_image(p, n, x, y, maxw, maxh); }
int  brd_draw_png(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh)
{ return draw_image(p, n, x, y, maxw, maxh); }

/* decode into an RGB565 buffer (same scaling rule as draw_image) */
uint16_t* brd_img_decode(const uint8_t* p, size_t n, int maxw, int maxh, uint16_t bg, int* out_w, int* out_h)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc* img = stbi_load_from_memory((const stbi_uc*)p, (int)n, &w, &h, &comp, 4);
    if (!img) return NULL;
    int dw = w, dh = h;
    if (dw > maxw) { dh = dh * maxw / dw; dw = maxw; }
    if (dh > maxh) { dw = dw * maxh / dh; dh = maxh; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    uint16_t* px = (uint16_t*)malloc((size_t)dw * dh * 2);
    if (!px) { stbi_image_free(img); return NULL; }
    uint8_t bg_g = gray_of(bg);
    for (int j = 0; j < dh; j++) {
        int sy = j * h / dh;
        for (int i = 0; i < dw; i++) {
            int sx = i * w / dw;
            const stbi_uc* sp = &img[((size_t)sy * w + sx) * 4];
            unsigned gray = (sp[0] * 77 + sp[1] * 150 + sp[2] * 29) >> 8;
            unsigned a = sp[3];
            uint8_t v = (uint8_t)((gray * a + bg_g * (255 - a)) / 255);
            px[(size_t)j * dw + i] = (uint16_t)(((v & 0xF8) << 8) | ((v & 0xFC) << 3) | (v >> 3));
        }
    }
    stbi_image_free(img);
    *out_w = dw;
    *out_h = dh;
    return px;
}

/* 24-bit BMP screenshot dump (viewable/convetible to PNG) */
static void dump_bmp_named(const char* path)
{
    int LW = brd_width(), LH = brd_height();
    FILE* f = fopen(path, "wb");
    if (!f) return;
    int rowsz = (LW * 3 + 3) & ~3;
    unsigned datasize = (unsigned)rowsz * LH;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    *(unsigned*)&hdr[2] = 54 + datasize;
    *(unsigned*)&hdr[10] = 54;
    *(unsigned*)&hdr[14] = 40;
    *(int*)&hdr[18] = LW;
    *(int*)&hdr[22] = LH;
    *(unsigned short*)&hdr[26] = 1;
    *(unsigned short*)&hdr[28] = 24;
    *(unsigned*)&hdr[34] = datasize;
    fwrite(hdr, 1, 54, f);
    unsigned char* row = (unsigned char*)malloc(rowsz);
    for (int y = LH - 1; y >= 0; y--) {
        memset(row, 0, rowsz);
        for (int x = 0; x < LW; x++) {
            unsigned char v = fb_get(x, y);
            row[x * 3] = v; row[x * 3 + 1] = v; row[x * 3 + 2] = v;
        }
        fwrite(row, 1, rowsz, f);
    }
    free(row);
    fclose(f);
}

static void dump_bmp(void)
{
    if (getenv("HOST_GUI_NOBMP")) return;
    static int dir_ok = 0;
    if (!dir_ok) { _mkdir("gui_frames"); dir_ok = 1; }
    char path[64];
    snprintf(path, sizeof(path), "gui_frames/frame_%04u.bmp", (unsigned)g_refresh);
    dump_bmp_named(path);
}

/* named screenshot of the current canvas (used by the `shot` script command) */
void host_shot(const char* name)
{
    static int dir_ok = 0;
    if (!dir_ok) { _mkdir("gui_frames"); dir_ok = 1; }
    char path[96];
    snprintf(path, sizeof(path), "gui_frames/shot_%s.bmp", name);
    dump_bmp_named(path);
    printf("[SHOT] %s\n", path);
}

/* scheduled shots: fired from brd_touch_poll so they work even while the
   app is blocked inside a modal loop (on-screen keyboard, alarm screen) */
typedef struct { char name[32]; uint32_t at; } shot_sched_t;
#define SHOT_SCHED_MAX 64
static shot_sched_t g_shots[SHOT_SCHED_MAX];
static int g_shots_n = 0;

void host_schedule_shot(const char* name, uint32_t at_ms)
{
    if (g_shots_n >= SHOT_SCHED_MAX) return;
    snprintf(g_shots[g_shots_n].name, sizeof(g_shots[g_shots_n].name), "%s", name);
    g_shots[g_shots_n].at = at_ms;
    g_shots_n++;
}

static void shots_due(uint32_t now)
{
    for (int i = 0; i < g_shots_n; ) {
        if (now >= g_shots[i].at) {
            host_shot(g_shots[i].name);
            memmove(&g_shots[i], &g_shots[i + 1], sizeof(shot_sched_t) * (size_t)(g_shots_n - i - 1));
            g_shots_n--;
        } else {
            i++;
        }
    }
}

void host_shots_pending(void) { shots_due(brd_millis()); }

/* console render: 8x downscale, tuned gray ramp for thin text */
static void render_frame(void)
{
    g_refresh++;
    printf("\n===== FRAME %u =====\n", (unsigned)g_refresh);
    if (g_frame_cb) g_frame_cb();
    dump_bmp();
    int LW = brd_width(), LH = brd_height();
    int cols = LW / 8;
    if (cols > 120) cols = 120;
    char line[121];
    for (int y = 0; y < LH; y += 8) {
        for (int x = 0; x < LW; x += 8) {
            unsigned sum = 0;
            int n = 0;
            for (int j = 0; j < 8 && y + j < LH; j++)
                for (int i = 0; i < 8 && x + i < LW; i++) { sum += fb_get(x + i, y + j); n++; }
            unsigned avg = n ? sum / n : 255;
            char ch = avg >= 248 ? ' ' : avg >= 232 ? '.' : avg >= 190 ? ':' : avg >= 120 ? '*' : avg >= 60 ? '%' : '#';
            line[(x / 8)] = ch;
        }
        line[cols] = 0;
        printf("%s\n", line);
    }
}

void brd_refresh(int mode)
{
    (void)mode;
    g_last_refresh = (uint32_t)GetTickCount64();
    if (g_anim && !getenv("HOST_GUI_ANIM_SHOTS")) return; /* page transition: skip dump */
    render_frame();
}
uint32_t brd_refresh_count(void) { return g_refresh; }

void brd_perf_last_refresh(brd_perf_refresh_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
}

/* host-only stubs for the remaining bridge API */
bool brd_chg_stat_high(void) { return true; }
bool brd_vbus_present(void) { return false; }
uint32_t brd_sd_num_sectors(void) { return 0; }
uint32_t brd_sd_sector_size(void) { return 512; }
int brd_serial_avail(void) { return 0; }
int brd_serial_read(void) { return -1; }
int brd_serial_write(const void* p, int n) { (void)p; return n; }
void brd_wdt_feed(void) {}
brd_ls_t brd_idle_sleep(uint32_t max_ms) { (void)max_ms; return BRD_LS_SKIPPED; }
void brd_refresh_region(int x, int y, int w, int h, int mode)
{
    (void)x; (void)y; (void)w; (void)h; (void)mode;
    g_last_refresh = (uint32_t)GetTickCount64();
    if (g_anim && !getenv("HOST_GUI_ANIM_SHOTS")) return;
    render_frame();
}
void brd_set_sleeping(bool s) { (void)s; }
uint16_t* brd_canvas_ptr(void) { return NULL; }

/* R1 slots: not emulated (the GUI scripts exercise the fallback path) */
void* brd_canvas_alloc(int w, int h) { (void)w; (void)h; return NULL; }
void  brd_canvas_free(void* c) { (void)c; }
void  brd_canvas_target(void* c) { (void)c; }
void  brd_canvas_blit(void* c, int x, int y) { (void)c; (void)x; (void)y; }

/* ---------------- scheduled touch injection ----------------
   Events are queued with absolute timestamps so they are delivered even
   while the app blocks inside modal loops (on-screen keyboard). */
typedef struct {
    int type;               /* 0 = touch, 1 = pwr */
    int x0, y0, x1, y1;
    uint32_t t_press, t_release;
    bool emitted_press;
    bool emitted_release;
} sched_ev_t;

#define SCHED_MAX 512
static sched_ev_t sched[SCHED_MAX];
static int sched_n = 0;
static int sched_i = 0; /* next event index (sorted by t_press) */

static bool pwr_pending = false;

static void sched_insert(int idx)
{
    /* keep the pending queue sorted by t_press (stable) */
    for (int i = idx; i > sched_i && sched[i].t_press < sched[i - 1].t_press; i--) {
        sched_ev_t tmp = sched[i];
        sched[i] = sched[i - 1];
        sched[i - 1] = tmp;
    }
}

void host_schedule_touch_ms(int x0, int y0, int x1, int y1, uint32_t at_ms, uint32_t hold_ms)
{
    if (sched_n >= SCHED_MAX) return;
    sched_ev_t* e = &sched[sched_n];
    e->type = 0;
    e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1;
    e->t_press = at_ms;
    e->t_release = at_ms + (hold_ms ? hold_ms : 90);
    e->emitted_press = e->emitted_release = false;
    sched_n++;
    sched_insert(sched_n - 1);
}

void host_schedule_touch(int x0, int y0, int x1, int y1, uint32_t at_ms)
{
    host_schedule_touch_ms(x0, y0, x1, y1, at_ms, 90);
}

void host_schedule_pwr(uint32_t at_ms)
{
    if (sched_n >= SCHED_MAX) return;
    sched_ev_t* e = &sched[sched_n];
    e->type = 1;
    e->t_press = at_ms;
    e->t_release = at_ms;
    e->emitted_press = e->emitted_release = false;
    sched_n++;
    sched_insert(sched_n - 1);
}

void host_inject_tap(int x, int y) { host_schedule_touch(x, y, x, y, brd_millis() + 10); }
void host_inject_swipe(int x0, int y0, int x1, int y1) { host_schedule_touch(x0, y0, x1, y1, brd_millis() + 10); }
void host_inject_pwr(void) { host_schedule_pwr(brd_millis() + 10); }

/* delay all events that have not started yet (e.g. when a screen took
   longer than expected to appear); pending shots shift with them */
void host_shift_pending_events(uint32_t delta)
{
    if (delta == 0) return;
    for (int i = sched_i; i < sched_n; i++) {
        if (sched[i].emitted_press) continue;
        sched[i].t_press += delta;
        sched[i].t_release += delta;
    }
    for (int i = 0; i < g_shots_n; i++) g_shots[i].at += delta;
}

void brd_touch_poll(brd_touch_t* t)
{
    memset(t, 0, sizeof(*t));
    uint32_t now = brd_millis();
    shots_due(now);   /* shots scheduled for this moment (works in modals) */
    if (sched_i >= sched_n) return;
    sched_ev_t* e = &sched[sched_i];
    if (e->type != 0) { /* pwr events handled in brd_pwr_clicked */
        if (now >= e->t_press) sched_i++;
        return;
    }
    if (now < e->t_press) return;
    if (now < e->t_release) {
        if (!e->emitted_press) {
            e->emitted_press = true;
            t->pressed = true;
            t->x0 = e->x0; t->y0 = e->y0;
            printf("[TOUCH] press %d,%d at %u\n", e->x0, e->y0, (unsigned)now);
        }
        t->down = true;
        float k = (float)(now - e->t_press) / (float)(e->t_release - e->t_press);
        t->x = e->x0 + (int)((e->x1 - e->x0) * k);
        t->y = e->y0 + (int)((e->y1 - e->y0) * k);
        return;
    }
    if (!e->emitted_release) {
        e->emitted_release = true;
        t->released = true;
        t->x = e->x1; t->y = e->y1;
        t->x0 = e->x0; t->y0 = e->y0;
        printf("[TOUCH] release %d,%d at %u\n", e->x1, e->y1, (unsigned)now);
    }
    sched_i++;
}

bool brd_coredump_save(const char* path) { (void)path; return false; }

bool brd_pwr_clicked(void)
{
    uint32_t now = brd_millis();
    if (sched_i < sched_n && sched[sched_i].type == 1 && now >= sched[sched_i].t_press) {
        sched_i++;
        return true;
    }
    return false;
}
bool brd_btn_clicked(void) { return false; }

int  brd_battery(void) { return 87; }
int  brd_reset_reason(void) { return 0; }
bool brd_charging(void) { return false; }
int  brd_battery_mv(void) { return 4000; }
static float g_acc[3] = { 0, 0, 1.0f };
void host_set_accel(float x, float y, float z) { g_acc[0] = x; g_acc[1] = y; g_acc[2] = z; }
void brd_accel(float* x, float* y, float* z) { *x = g_acc[0]; *y = g_acc[1]; *z = g_acc[2]; }
void brd_imu_power(bool on) { (void)on; }
bool brd_imu_enabled(void) { return true; }
float brd_temp(void) { return 25.0f; }
void brd_beep_tone(int freq, int ms) { (void)freq; (void)ms; }
void brd_beep_stop(void) {}

uint32_t brd_sleep_cause(void) { return 0; }
void brd_wait_button_release(void) {}
void brd_restart(void) { exit(0); }

static uint64_t g_sleep_us = 0;
static bool     g_sleep_touch = false;
static int      g_sleep_count = 0;
static int      g_poweroff_requests = 0;
uint64_t host_last_sleep_us(void) { return g_sleep_us; }
bool     host_last_sleep_touch(void) { return g_sleep_touch; }
int      host_sleep_count(void) { return g_sleep_count; }

void brd_poweroff(void)
{
    g_poweroff_requests++;
    printf("[BRD] POWEROFF REQUESTED (real device: power latch cut)\n");
}
int host_poweroff_requests(void) { return g_poweroff_requests; }
void brd_deep_sleep(uint64_t us, bool touch_wakeup)
{
    g_sleep_us = us;
    g_sleep_touch = touch_wakeup;
    g_sleep_count++;
    printf("[BRD] deep sleep: us=%llu touch_wakeup=%d\n",
           (unsigned long long)us, touch_wakeup ? 1 : 0);
}

/* WiFi mock: a list of simulated nearby networks with passwords.
   `netadd SSID PASS` feeds it, `brd_wifi_sync` only succeeds when the
   credentials match a known network. */
typedef struct { char ssid[33]; char pass[65]; } hnet_t;
#define HNET_MAX 8
static hnet_t g_nets[HNET_MAX];
static int g_nnets = 0;

void host_net_add(const char* ssid, const char* pass)
{
    if (!ssid || !ssid[0] || g_nnets >= HNET_MAX) return;
    for (int i = 0; i < g_nnets; i++) {
        if (strcmp(g_nets[i].ssid, ssid) == 0) {
            snprintf(g_nets[i].pass, sizeof(g_nets[i].pass), "%s", pass ? pass : "");
            return;
        }
    }
    snprintf(g_nets[g_nnets].ssid, sizeof(g_nets[g_nnets].ssid), "%s", ssid);
    snprintf(g_nets[g_nnets].pass, sizeof(g_nets[g_nnets].pass), "%s", pass ? pass : "");
    g_nnets++;
}
void host_net_clear(void) { g_nnets = 0; }

int brd_wifi_scan(char (*ssids)[33], int max)
{
    int n = g_nnets < max ? g_nnets : max;
    for (int i = 0; i < n; i++) snprintf(ssids[i], 33, "%s", g_nets[i].ssid);
    if (n > 0) printf("[BRD] wifi scan: %d network(s)\n", n);
    return n;
}

static bool g_wifi_on = false;
void host_set_wifi_active(bool on) { g_wifi_on = on; }
bool brd_wifi_active(void) { return g_wifi_on; }

int brd_wifi_sync(const char* ssid, const char* pass, int utc_hours, int timeout_s,
                  const uint8_t* bssid, int channel)
{
    (void)utc_hours; (void)timeout_s; (void)bssid; (void)channel;
    g_wifi_on = false;
    bool ok = false;
    for (int i = 0; i < g_nnets; i++) {
        if (strcmp(g_nets[i].ssid, ssid) == 0) {
            if (strcmp(g_nets[i].pass, pass ? pass : "") == 0) ok = true;
            else printf("[BRD] wifi_sync: wrong password for '%s'\n", ssid);
            break;
        }
    }
    /* simulate a connection that takes ~900 ms */
    Sleep(900);
    if (!ok) return 1;
    g_wifi_on = true;
    printf("[BRD] wifi_sync: connected to '%s'\n", ssid);
    return 0;
}

void brd_wifi_off(void) { if (g_wifi_on) { g_wifi_on = false; printf("[BRD] WiFi off\n"); } }
void brd_wifi_bssid(uint8_t out[6]) { memset(out, 0, 6); }
int  brd_wifi_channel(void) { return 0; }
bool brd_bt_active(void) { return false; }
void brd_bt_off(void) {}
bool brd_time_valid(void) { return true; }
bool brd_get_localtime(int* y, int* mo, int* d, int* h, int* mi, int* s)
{
    time_t now = time(NULL);
    struct tm ti;
    if (localtime_s(&ti, &now) != 0) return false;
    if (y) *y = ti.tm_year + 1900;
    if (mo) *mo = ti.tm_mon + 1;
    if (d) *d = ti.tm_mday;
    if (h) *h = ti.tm_hour;
    if (mi) *mi = ti.tm_min;
    if (s) *s = ti.tm_sec;
    return true;
}
void brd_apply_tz(int utc_hours) { (void)utc_hours; }
int64_t brd_epoch(void) { return (int64_t)time(NULL); }
void brd_rtc_sync(void) {}

bool brd_sd_mount(void) { return true; }
bool brd_sd_remount(void) { return true; }
bool brd_sd_ok(void) { return true; }
uint64_t brd_sd_size(void) { return 8ULL * 1024 * 1024 * 1024; }
uint64_t brd_sd_free(void) { return 4ULL * 1024 * 1024 * 1024; }
int  brd_usb_disk_start(void) { printf("[BRD] usb disk start (simulated)\n"); return 0; }
void brd_usb_disk_stop(void) { printf("[BRD] usb disk stop\n"); }
