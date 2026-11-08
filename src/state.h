// state.h - settings + per-book state persisted on SD
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

typedef struct {
    bool dark_mode;
    bool images;
    bool flip;
    bool autorot;
    bool rot_lock;
    int  orientation;              /* 0/1/2/3 = 0/90/180/270 degrees */
    bool hourly_refresh;
    bool cover_poweroff;
    int  auto_off_min;
    int  utc_offset;
    bool alarm_on;
    int  alarm_h, alarm_m;
    bool ntp_synced;
    int64_t ntp_epoch;
    int  wake_type;
    int  last_rotation;
    char last_book[256];
    int  last_chapter;
    int  last_page;
    int  rd_font;                  /* 0..5 = 80/100/140/160/200/120% */
    int  rd_spacing;               /* 0/1/2 = extra line gap 0/4/8 px */
    int  rd_margins;               /* 0/1/2 = side margins 28/44/60 px */
} Config;

extern Config g_cfg;

#define BM_MAX 64
#define BOOKSTATE_MAGIC 0x4C595349 /* "LYSI" */

typedef struct {
    int32_t magic;
    int32_t furthest_ch, furthest_pg;
    int32_t last_ch, last_pg;
    int32_t bm_count;
    struct { int32_t ch, pg; } bm[BM_MAX];
    int32_t page_counts[256];      /* pages per chapter, -1 unknown */
    int32_t last_off;              /* text offset of the last page, -1 unknown */
    int32_t lkey;                  /* layout key (w/h/font); mismatch invalidates */
} BookState;

/* payload size of the pre-offset (v1) state files, for migration */
#define BS_LEGACY_V1 ((int)sizeof(BookState) - 2 * (int)sizeof(int32_t))

int  st_init(void);
void st_save_cfg(void);
void st_load_cfg(void);

/* known WiFi networks (persisted credentials, most recent first) */
#define WNET_MAX 4
void st_wifi_save(const char* ssid, const char* pass);
const char* st_wifi_find(const char* ssid);     /* returns password or NULL */
int  st_wifi_count(void);
int  st_wifi_get(int i, char* ssid, int sslen, char* pass, int palen);

/* W1: cached BSSID/channel for fast (scan-less) reconnects */
bool st_wifi_fast_get(const char* ssid, uint8_t bssid_out[6], int* channel_out);
void st_wifi_fast_save(const char* ssid, const uint8_t bssid[6], int channel);

void st_book_path(const char* bookpath, char* out, int outlen);
void st_book_path_legacy(const char* bookpath, char* out, int outlen);
int  st_book_get(const char* bookpath, BookState* st);
void st_book_put(const char* bookpath, const BookState* st);
int  st_book_get_count(void);   /* instrumentation (I1 host test) */

int  st_total_books_progress(void);   /* rough library progress, -1 unknown */
