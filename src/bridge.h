// bridge.h - C API over M5Unified / M5GFX / Arduino runtime
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- colors ----------
uint16_t brd_color(uint8_t r, uint8_t g, uint8_t b);
#define BRD_BLACK  0x0000
#define BRD_WHITE  0xFFFF

// ---------- lifecycle ----------
void brd_serial_begin(void);
void brd_begin(bool minimal);   /* minimal: skip IMU/mic init + panel clear (wake paths) */
void brd_log(const char* s);
void brd_wdt_feed(void);
int  brd_serial_avail(void);
int  brd_serial_read(void);
int  brd_serial_write(const void* p, int n);
int  brd_cpu_freq(int mhz);        // DVFS: returns actual MHz (80/160/240)
uint64_t brd_us(void);             // microseconds since boot (wake phase markers)
uint64_t brd_boot_us(void);        // capture time of brd_serial_begin (power->app)
int  brd_width(void);
int  brd_height(void);
void brd_rotation(int r);          // 0 or 2 (180 flip)
int  brd_get_rotation(void);
uint32_t brd_millis(void);
void brd_delay(uint32_t ms);

// ---------- drawing (off-screen canvas, pushed on refresh) ----------
void brd_clear(uint16_t c);
void brd_fill_rect(int x, int y, int w, int h, uint16_t c);
void brd_draw_rect(int x, int y, int w, int h, uint16_t c);
void brd_hline(int x, int y, int w, uint16_t c);
void brd_line(int x0, int y0, int x1, int y1, uint16_t c);
void brd_text(int x, int y, const char* s, uint16_t fg, int size);
int  brd_text_w(const char* s, int size);
int  brd_font_h(int size);
int  brd_text_fit(char* out, int outlen, const char* s, int maxw, int size); // truncate with ellipsis
void brd_draw_rgb565(int x, int y, int w, int h, const uint16_t* px);
int  brd_draw_jpeg(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh);
int  brd_draw_png(const uint8_t* p, size_t n, int x, int y, int maxw, int maxh);
/* decode jpeg/png into an RGB565 buffer (aspect preserved, downscale only,
   bg = fill for transparent pixels). Caller frees. NULL on failure. */
uint16_t* brd_img_decode(const uint8_t* p, size_t n, int maxw, int maxh, uint16_t bg, int* out_w, int* out_h);
void brd_refresh(int mode);        // 0 full/quality, 1 text, 2 fast partial
uint32_t brd_refresh_count(void);
void brd_refresh_region(int x, int y, int w, int h, int mode);
typedef struct {
    uint32_t pack_us, push_us, panel_us;
    int mode, w, h;
} brd_perf_refresh_t;
void brd_perf_last_refresh(brd_perf_refresh_t* out);
bool brd_display_busy(void);       // EPD driver still pushing (E4)
uint32_t brd_last_refresh_ms(void);// timestamp of the last push (E4)
void brd_draw_offset(int dx, int dy); // translate every following draw (page animations)
void brd_anim(bool on);               // page transition: fast refresh, no host frame dump
void brd_set_sleeping(bool s);     // panel low power while asleep
uint16_t* brd_canvas_ptr(void);    // raw RGB565 framebuffer (for cover cache)

// ---------- off-screen page canvases (R1 pre-render) ----------
void* brd_canvas_alloc(int w, int h);   // PSRAM RGB565 canvas, NULL on failure
void  brd_canvas_free(void* c);
void  brd_canvas_target(void* c);       // NULL = main canvas
void  brd_canvas_blit(void* c, int x, int y);   // copy into the main canvas

// ---------- input ----------
typedef struct {
    bool down;          // currently touching
    bool pressed;       // press event this poll
    bool released;      // release event this poll
    bool held;          // long press event
    int  x, y;          // current/last point
    int  x0, y0;        // press origin
    uint32_t held_ms;
} brd_touch_t;
void brd_touch_poll(brd_touch_t* t);
#ifdef DEV_INJECT
/* Synthetic touch for autonomous CLI tests: state 1=press, 2=down, 3=release */
void brd_dbg_touch(int x, int y, int state);
#endif
bool brd_pwr_clicked(void);
bool brd_btn_clicked(void);

// ---------- power / sensors ----------
int   brd_battery(void);           // -1 unknown else 0..100 (M5 internal curve)
int   brd_battery_mv(void);        // raw battery voltage in mV, -1 unknown
bool  brd_charging(void);
bool  brd_vbus_present(void);
bool  brd_chg_stat_high(void);
float brd_temp(void);              // deg C, NAN if unavailable
void  brd_accel(float* x, float* y, float* z);
void  brd_imu_power(bool on);      /* suspend/start the IMU when not needed */
bool  brd_imu_enabled(void);       /* is the IMU sensor initialized? */
void  brd_beep_tone(int freq, int ms);
void  brd_beep_stop(void);
uint32_t brd_sleep_cause(void);    // 0 none, 1 timer, 2 button
int   brd_reset_reason(void);      // esp_reset_reason() passthrough (device bring-up)
void  brd_poweroff(void);          // cut main power (latch): wake only via side button
#define BRD_SLEEP_NO_TIMER UINT64_MAX   /* deep sleep with no timer wakeup */
void  brd_deep_sleep(uint64_t us, bool touch_wakeup);
void  brd_wait_button_release(void);
void  brd_restart(void);

// ---------- crash guard (B2) ----------
bool  brd_coredump_save(const char* path);   // save the flash coredump to SD (if any)

// ---------- light sleep in idle (E1) ----------
typedef enum {
    BRD_LS_SKIPPED = 0,   /* not slept: guard hit or too short */
    BRD_LS_TIMER,         /* woke on the deadline timer */
    BRD_LS_TOUCH,         /* woke on the touch INT (GPIO48) */
    BRD_LS_BUTTON,        /* woke on a button (if available) */
    BRD_LS_OTHER
} brd_ls_t;
brd_ls_t brd_idle_sleep(uint32_t max_ms);   /* light sleep up to max_ms or event */

// ---------- memory telemetry (Q2) ----------
uint32_t brd_heap_free(void);      /* internal RAM free bytes */
uint32_t brd_psram_free(void);     /* PSRAM free bytes */
uint32_t brd_psram_largest(void);  /* PSRAM largest free block */

// ---------- wifi / time ----------
// returns 0 ok, 1 wifi fail, 2 ntp fail.
// bssid/channel != NULL/0 enable the scan-less fast connect (W1).
int   brd_wifi_sync(const char* ssid, const char* pass, int utc_hours, int timeout_s,
                    const uint8_t* bssid, int channel);
int   brd_wifi_scan(char (*ssids)[33], int max);   // scan nearby networks, returns count
typedef void (*brd_wifi_progress_fn)(int pct01);
void  brd_wifi_progress_cb(brd_wifi_progress_fn cb); // progress while connecting/syncing
void  brd_wifi_off(void);
bool  brd_wifi_active(void);       // WiFi currently connected
void  brd_wifi_bssid(uint8_t out[6]);   // BSSID of the current connection
int   brd_wifi_channel(void);           // channel of the current connection
bool  brd_bt_active(void);         // Bluetooth controller enabled
void  brd_bt_off(void);            // disable Bluetooth radio
bool  brd_time_valid(void);
bool  brd_get_localtime(int* y, int* mo, int* d, int* h, int* mi, int* s);
void  brd_apply_tz(int utc_hours);
int64_t brd_epoch(void);
void  brd_rtc_sync(void);          // write system time (UTC) to the BM8563 RTC

// ---------- storage ----------
bool brd_sd_mount(void);
bool brd_sd_remount(void);
bool brd_sd_ok(void);
uint64_t brd_sd_size(void);
uint32_t brd_sd_num_sectors(void);
uint32_t brd_sd_sector_size(void);
uint64_t brd_sd_free(void);

// ---------- usb mass storage ----------
int  brd_usb_disk_start(void);     // 0 ok
void brd_usb_disk_stop(void);

#ifdef __cplusplus
}
#endif
