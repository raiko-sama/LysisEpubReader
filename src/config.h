// config.h - project constants and paths
#pragma once

#define APP_NAME     "Lysis Epub reader"
#define APP_VERSION  "V1.0 Beta"
#define APP_AUTHOR   "Lysis"

#ifndef BOOKS_DIR
#define BOOKS_DIR    "/sd/books"
#endif
#ifndef ST_DIR
#define ST_DIR       "/sd/books/.state"
#endif

#define ST_BAT_CYCLE        ST_DIR "/_bat_cycle.dat"
#define ST_BAT_LOG          ST_DIR "/_bat_log.bin"
#define ST_BOOKMARKS        ST_DIR "/_bookmarks.dat"
#define ST_LAST             ST_DIR "/_last.txt"
#define ST_DISPLAY          ST_DIR "/_display.dat"
#define ST_IMAGES           ST_DIR "/_images.dat"
#define ST_COVER_POWEROFF   ST_DIR "/_cover_poweroff.dat"
#define ST_HOURLY_REFRESH   ST_DIR "/_hourly_refresh.dat"
#define ST_WAKE_TYPE        ST_DIR "/_wake_type.dat"
#define ST_POWERED_OFF_AT   ST_DIR "/_powered_off_at.dat"
#define ST_LAST_ROTATION    ST_DIR "/_last_rotation.dat"
#define ST_LAST_COVER       ST_DIR "/_last_cover.txt"
#define ST_AUTO_OFF         ST_DIR "/_auto_off.dat"
#define ST_DISPLAY_FLIP     ST_DIR "/_display_flip.dat"
#define ST_AUTO_ROTATION    ST_DIR "/_auto_rotation.dat"
#define ST_ROTATION_LOCK    ST_DIR "/_rotation_lock.dat"
#define ST_ORIENTATION      ST_DIR "/_orientation.dat"
#define ST_DARK_MODE        ST_DIR "/_dark_mode.dat"
#define ST_UTC_OFFSET       ST_DIR "/_utc_offset.dat"
#define ST_ALARM            ST_DIR "/_alarm.dat"
#define ST_NTP_SYNC         ST_DIR "/_ntp_sync.dat"
#define ST_COVER_RGB        ST_DIR "/_cover.rgb"
#define ST_COVER_RGBZ       ST_DIR "/_cover.rgbz"
#define ST_WIFI             ST_DIR "/_wifi.dat"
#define ST_LOWBATT          ST_DIR "/_lowbatt.dat"
#define ST_DIAG             ST_DIR "/_diag.log"
#define ST_WAKE_LOG         ST_DIR "/_wake_log.bin"
#define ST_WIFI_RETRY       ST_DIR "/_wifi_retry.dat"
#define ST_WIFI_FAST        ST_DIR "/_wifi_fast.dat"
#define ST_READING          ST_DIR "/_reading.dat"

/* diagnostic logging for device bring-up (serial + SD mirror);
   0 = release build, 1 = bring-up (build with -DDIAG_LOG=1) */
#ifndef DIAG_LOG
#define DIAG_LOG 0
#endif

/* ---- rev. 5 optimization switches (each compiles at 0 and 1) ----
   Defaults per nuova_implementazione.md; e-ink/device-gated ones stay 0
   until validated on the real panel (V6) or on the device. */
#ifndef OPT_LIGHT_SLEEP
#define OPT_LIGHT_SLEEP 0          /* E1: light sleep in idle (device validation required) */
#endif
#ifndef OPT_TOUCH_INT_GATE
#define OPT_TOUCH_INT_GATE 1       /* E2: I2C touch reads gated by the INT line */
#endif
#ifndef OPT_DVFS_HYST
#define OPT_DVFS_HYST 0            /* E3: DVFS hysteresis (decided by T2 + measurements) */
#endif
#ifndef OPT_BAT_QUIET
#define OPT_BAT_QUIET 1            /* E4: battery sampling only at rest */
#endif
#ifndef OPT_PRERENDER
#define OPT_PRERENDER 1            /* R1: next/prev page pre-rendered */
#endif
#ifndef OPT_FB_DIFF
#define OPT_FB_DIFF 0              /* R2: framebuffer diff (device photos required) */
#endif
#ifndef OPT_PROGRESSIVE_LAYOUT
#define OPT_PROGRESSIVE_LAYOUT 0   /* R3: progressive pagination */
#endif
#ifndef OPT_CANVAS_GRAY8
#define OPT_CANVAS_GRAY8 0         /* R4: gray8 canvas (P3, gated on photos) */
#endif
#ifndef OPT_LIB_INDEX
#define OPT_LIB_INDEX 1            /* I1: persistent library index */
#endif
#ifndef OPT_COVER_Z
#define OPT_COVER_Z 1              /* I2: lossless-compressed lock cover */
#endif
#ifndef OPT_STREAM_EXTRACT
#define OPT_STREAM_EXTRACT 0       /* I3: streaming extraction pipeline */
#endif
#ifndef OPT_SEARCH_TEXTCACHE
#define OPT_SEARCH_TEXTCACHE 1     /* I4: LRU text cache for search */
#endif
#ifndef OPT_HOT_O2
#define OPT_HOT_O2 1               /* I5: -O2 pragmas on hot paths */
#endif
#ifndef OPT_WIFI_FAST
#define OPT_WIFI_FAST 1            /* W1: fast connect + single-shot SNTP */
#endif
#ifndef OPT_WAKE_LEAN
#define OPT_WAKE_LEAN 1            /* B1: lean wake (speaker lazy) + phase markers */
#endif
#ifndef OPT_CRASH_GUARD
#define OPT_CRASH_GUARD 1          /* B2: coredump to SD + safe mode */
#endif
#ifndef OPT_ZIP_GUARD
#define OPT_ZIP_GUARD 1            /* Q1: zip size/ratio guards before alloc */
#endif
#ifndef OPT_MEM_TELEMETRY
#define OPT_MEM_TELEMETRY 1        /* Q2: heap/PSRAM telemetry + eviction */
#endif

#define WAKE_NONE    0
#define WAKE_HOURLY  1
#define WAKE_ALARM   2

#define UI_BOOT_DELAY_MS   800
#define UI_AUTOOFF_DEFAULT 10      /* minutes, 0 = off */
#define UI_KEY_H           40
