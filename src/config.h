// config.h - project constants and paths
#pragma once

#define APP_NAME     "Lysis Epub reader"
#define APP_VERSION  "V1.0.2"
#define APP_AUTHOR   "Lysis"

#ifndef BOOKS_DIR
#define BOOKS_DIR    "/sd/books"
#endif
#ifndef ST_DIR
#define ST_DIR       "/sd/books/.state"
#endif

#define ST_BAT_CYCLE        ST_DIR "/_bat_cycle.dat"
#define ST_BAT_LOG          ST_DIR "/_bat_log.bin"
#define ST_BAT_HISTORY      ST_DIR "/_bat_history.csv"
#define ST_LAST             ST_DIR "/_last.txt"
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
#define OPT_LIGHT_SLEEP 0          /* E1: rolled back — touch/refresh degraded on battery */
#endif
#ifndef OPT_TOUCH_INT_GATE
#define OPT_TOUCH_INT_GATE 1       /* E2: I2C touch reads gated by the INT line */
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
#ifndef OPT_ZIP_GUARD
#define OPT_ZIP_GUARD 1            /* Q1: zip size/ratio guards before alloc */
#endif

#define WAKE_NONE    0
#define WAKE_HOURLY  1
#define WAKE_ALARM   2

#define UI_AUTOOFF_DEFAULT 10      /* minutes, 0 = off */
