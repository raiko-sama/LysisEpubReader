// bridge.h - C API over M5Unified / M5GFX / Arduino runtime
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRD_BLACK  0x0000
#define BRD_WHITE  0xFFFF

// ---------- lifecycle ----------
void brd_serial_begin(void);
void brd_begin(bool minimal);   /* minimal: skip IMU/mic init + panel clear (wake paths) */
void brd_log(const char* s);        /* non-blocking: drops when the TX ring is full */
void brd_log_sync(const char* s);   /* protocol responses: wait for the host */
int  brd_serial_avail(void);
int  brd_serial_read(void);
int  brd_serial_write(const void* p, int n);
int  brd_cpu_freq(int mhz);        // DVFS: returns actual MHz (80/160/240)
uint64_t brd_us(void);             // microseconds since boot (wake phase markers)
int  brd_width(void);
int  brd_height(void);
void brd_rotation(int r);          // 0 or 2 (180 flip)
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
uint16_t* brd_canvas_ptr(void);    // raw RGB565 framebuffer (for cover cache)

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

// ---------- power / sensors ----------
int   brd_battery(void);           // -1 unknown else 0..100 (M5 internal curve)
int   brd_battery_mv(void);        // raw battery voltage in mV, -1 unknown
bool  brd_charging(void);
bool  brd_vbus_present(void);
bool  brd_chg_stat_high(void);
float brd_temp(void);              // deg C, NAN if unavailable
void  brd_imu_power(bool on);      /* suspend/start the IMU when not needed */
void  brd_beep_tone(int freq, int ms);
void  brd_beep_stop(void);
void  brd_poweroff(void);          // cut main power (latch): wake only via side button
#define BRD_SLEEP_NO_TIMER UINT64_MAX   /* deep sleep with no timer wakeup */
void  brd_deep_sleep(uint64_t us, bool touch_wakeup);
void  brd_restart(void);

// ---------- light sleep in idle (E1) ----------
typedef enum {
    BRD_LS_SKIPPED = 0,   /* not slept: guard hit or too short */
    BRD_LS_TIMER,         /* woke on the deadline timer */
    BRD_LS_TOUCH,         /* woke on the touch INT (GPIO48) */
    BRD_LS_BUTTON,        /* woke on a button (if available) */
    BRD_LS_OTHER
} brd_ls_t;
brd_ls_t brd_idle_sleep(uint32_t max_ms);   /* light sleep up to max_ms or event */

typedef struct {
    uint32_t entries;   /* completed light-sleep cycles */
    uint32_t timer;     /* woke on the deadline timer */
    uint32_t touch;     /* woke on the GT911 INT line */
    uint32_t other;     /* other wake sources */
    uint32_t skipped;   /* USB/VBUS present: sleep deliberately skipped */
} brd_ls_stats_t;
void brd_idle_stats(brd_ls_stats_t* out);   /* E1 telemetry */

// ---------- wifi / time ----------
// returns 0 ok, 1 wifi fail, 2 ntp fail.
// bssid/channel != NULL/0 enable the scan-less fast connect (W1).
int   brd_wifi_sync(const char* ssid, const char* pass, int utc_hours, int timeout_s,
                    const uint8_t* bssid, int channel);
void  brd_wifi_off(void);
bool  brd_wifi_active(void);       // WiFi currently connected
void  brd_wifi_bssid(uint8_t out[6]);   // BSSID of the current connection
int   brd_wifi_channel(void);           // channel of the current connection
bool  brd_bt_active(void);         // Bluetooth controller enabled
void  brd_bt_off(void);            // disable Bluetooth radio
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

// ---------- usb mass storage ----------
int  brd_usb_disk_start(void);     // 0 ok
void brd_usb_disk_stop(void);

#ifdef __cplusplus
}
#endif
