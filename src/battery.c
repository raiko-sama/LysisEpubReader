// battery.c - LiPo capacity learner with charge-cycle endpoint tracking.
//
// How it works:
//  - The percentage is mapped between two learned voltage endpoints:
//    lo_mv (battery empty) and hi_mv (battery full), both starting from
//    the nominal LiPo curve (3300..4150 mV, same as M5Unified).
//  - On the charging->discharging transition (unplug after a charge) the
//    full voltage is learned (EWMA). On the discharging->charging
//    transition (plug-in when empty) the empty voltage is learned.
//  - As the cell degrades (full voltage drops, cutoff voltage rises) the
//    endpoints follow it, so the displayed % keeps reflecting the real
//    remaining capacity and the health estimate shrinks.
//  - An 11-sample median filter smooths the shown value; readings and
//    temperature are logged to /sd/books/.state/_bat_log.bin and the
//    model is persisted to /sd/books/.state/_bat_cycle.dat.
#include "battery.h"
#include "config.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define BAT_MAGIC       0x43415442 /* "BATC" */
#define BAT_NOMINAL_HI  4150
#define BAT_NOMINAL_LO  3300
#define BAT_A_FULL      25          /* EWMA factor x100 for full endpoint */
#define BAT_A_EMPTY     15          /* EWMA factor x100 for empty endpoint */
#define BAT_A_TRACK     5           /* continuous tracking factor x100 */
#define BAT_MIN_SPAN    150         /* mV minimum hi-lo span */
#define BAT_STABLE_N    3           /* stable samples before learning an endpoint */
#define BAT_STABLE_MV   20          /* max mV drift between stable samples */
#define BAT_NEAR_MV     400         /* endpoint candidate must be near the estimate */
#define BAT_FULL_CONFIRM_MV 4120    /* charger termination / full plateau */
#define BAT_FULL_HOLD_MV    4050    /* keep 100% while the pack is still full */
#define BAT_LOW_CUTOFF_MV   3200    /* empty endpoint / shutdown reserve */
#define BAT_LOW_CLEAR_MV    3350    /* hysteresis before leaving low state */

typedef struct {
    uint32_t magic;
    int16_t  hi_mv;
    int16_t  lo_mv;
    uint16_t cycles;
    uint8_t  state;
    uint8_t  reserved;
    uint32_t samples;
} batt_file_t;

typedef struct {
    int64_t epoch;
    int16_t mv;
    int16_t temp_dc;   /* deci-degrees C (25.0 C = 250) */
    int8_t  pct;
    int8_t  charging;
} batt_log_t;

static batt_file_t g_b;
static bool g_last_charging = false;
static bool g_first = true;
static bool g_dirty = false;
static int  g_median[11];
static int  g_med_n = 0;
static int  g_med_i = 0;
static int  g_last_mv = -1;
static int  g_stable = 0;   /* consecutive samples within BAT_STABLE_MV */
static bool g_full = false;  /* charger termination confirmed */
static bool g_low = false;   /* near empty / shutdown imminent */

#define BAT_LOG_BATCH 8
static batt_log_t g_blog[BAT_LOG_BATCH];
static int        g_blog_n = 0;

static void load(void)
{
    FILE* f = fopen(ST_BAT_CYCLE, "rb");
    bool ok = false;
    if (f) {
        if (fread(&g_b, 1, sizeof(g_b), f) == sizeof(g_b) && g_b.magic == BAT_MAGIC) ok = true;
        fclose(f);
    }
    if (!ok || g_b.hi_mv <= g_b.lo_mv || g_b.hi_mv > 5000 || g_b.lo_mv < 2000) {
        g_b.magic = BAT_MAGIC;
        g_b.hi_mv = BAT_NOMINAL_HI;
        g_b.lo_mv = BAT_NOMINAL_LO;
        g_b.cycles = 0;
        g_b.state = 0;
        g_b.reserved = 0;
        g_b.samples = 0;
    }
    g_dirty = false;
    g_last_charging = false;
    g_first = true;
    g_stable = 0;
    g_last_mv = -1;
    g_full = false;
    g_low = false;
}

void batt_init(void) { load(); }

int batt_sample_count(void) { return (int)(g_b.samples > INT32_MAX ? INT32_MAX : g_b.samples); }
int batt_hi_mv(void) { return g_b.hi_mv; }
int batt_lo_mv(void) { return g_b.lo_mv; }

void batt_reset(void)
{
    g_b.magic = BAT_MAGIC;
    g_b.hi_mv = BAT_NOMINAL_HI;
    g_b.lo_mv = BAT_NOMINAL_LO;
    g_b.cycles = 0;
    g_b.state = 0;
    g_b.reserved = 0;
    g_b.samples = 0;
    g_dirty = true;
    g_med_n = 0;
    g_med_i = 0;
    g_blog_n = 0;
    g_last_charging = false;
    g_first = true;
    g_stable = 0;
    g_last_mv = -1;
    g_full = false;
    g_low = false;
}

void batt_flush(void)
{
    if (g_blog_n > 0) {
        FILE* f = fopen(ST_BAT_LOG, "ab");
        if (f) { fwrite(g_blog, sizeof(batt_log_t), g_blog_n, f); fclose(f); }
        g_blog_n = 0;
    }
    if (!g_dirty) return;
    u_file_write_atomic(ST_BAT_CYCLE, &g_b, sizeof(g_b));
    g_dirty = false;
}

static void ewm(int16_t* dst, int target, int a_x100)
{
    int d = target - *dst;
    *dst = (int16_t)(*dst + (d * a_x100 + (d >= 0 ? 50 : -50)) / 100);
}

void batt_sample(int mv, bool charging, int temp_dc)
{
    /* Host/tests: without the CHG_STAT pin, low means charging. */
    batt_sample_ex(mv, charging, !charging, temp_dc);
}

void batt_sample_ex(int mv, bool charging, bool chg_stat_high, int temp_dc)
{
    if (mv < 1500 || mv > 6000) return;
    g_b.samples++;

    /* stability: an endpoint is only learned from a settled voltage */
    if (g_last_mv >= 0 && abs(mv - g_last_mv) <= BAT_STABLE_MV) g_stable++;
    else g_stable = 1;
    g_last_mv = mv;

    /* Charger termination (LGS4056H CHG_STAT high) plus a full plateau is
       the only reliable full-charge signal on PaperS3: there is no current
       sense and the ADC sees the charger node, not a rest OCV. */
    int full_mv = g_b.hi_mv - 30;
    if (full_mv < 4050) full_mv = 4050;
    if (full_mv > BAT_FULL_CONFIRM_MV) full_mv = BAT_FULL_CONFIRM_MV;
    int hold_mv = full_mv - 70;
    if (chg_stat_high && mv >= full_mv) {
        g_full = true;
    } else if (mv < hold_mv || (charging && mv < full_mv)) {
        g_full = false;
    }

    if (g_first) {
        /* first reading after boot: establish state, never learn an endpoint
           (device may have booted mid-charge) */
        g_first = false;
        g_last_charging = charging;
        g_b.state = charging ? 1 : 2;
    } else if (charging && !g_last_charging) {
        /* plugged in: learn the empty point only when the battery really was
           near empty (voltage close to the current low endpoint) and stable */
        if (g_stable >= BAT_STABLE_N && mv <= g_b.lo_mv + BAT_NEAR_MV) {
            ewm(&g_b.lo_mv, mv, BAT_A_EMPTY);
            if (g_b.lo_mv > g_b.hi_mv - BAT_MIN_SPAN) g_b.lo_mv = g_b.hi_mv - BAT_MIN_SPAN;
            g_dirty = true;
        }
        g_b.state = 1;
    } else if (!charging && g_last_charging) {
        /* unplugged: learn the full point only when the charge was complete
           (voltage close to the current high endpoint) and stable */
        if (g_stable >= BAT_STABLE_N && mv >= g_b.hi_mv - BAT_NEAR_MV) {
            ewm(&g_b.hi_mv, mv, BAT_A_FULL);
            if (g_b.hi_mv < g_b.lo_mv + BAT_MIN_SPAN) g_b.hi_mv = g_b.lo_mv + BAT_MIN_SPAN;
            g_b.cycles++;
            g_dirty = true;
        }
        g_b.state = 2;
    } else {
        g_b.state = charging ? 1 : 2;
        /* gentle continuous tracking between transitions */
        if (charging && mv > g_b.hi_mv) ewm(&g_b.hi_mv, mv, BAT_A_TRACK);
        if (!charging && mv < g_b.lo_mv) ewm(&g_b.lo_mv, mv, BAT_A_TRACK);
    }
    g_last_charging = charging;

    if (!charging && mv <= BAT_LOW_CUTOFF_MV) g_low = true;
    else if (!charging && mv >= BAT_LOW_CLEAR_MV) g_low = false;

    int pct = batt_percent_raw();
    if (pct >= 0) {
        g_median[g_med_i] = pct;
        g_med_i = (g_med_i + 1) % 11;
        if (g_med_n < 11) g_med_n++;
    }

    batt_log_t lg;
    lg.epoch = (int64_t)time(NULL);
    lg.mv = (int16_t)mv;
    lg.temp_dc = (int16_t)temp_dc;
    lg.pct = (int8_t)(pct < 0 ? -1 : pct);
    lg.charging = charging ? 1 : 0;
    g_blog[g_blog_n++] = lg;
    if (g_blog_n >= BAT_LOG_BATCH) {
        FILE* f = fopen(ST_BAT_LOG, "ab");
        if (f) { fwrite(g_blog, sizeof(batt_log_t), g_blog_n, f); fclose(f); }
        g_blog_n = 0;
    }

    if (g_dirty && (g_b.samples % 5) == 0) batt_flush();
}

int batt_percent_raw(void)
{
    if (g_last_mv < 0) return -1;
    if (g_full) { int h = g_b.hi_mv - 100; if (g_last_mv >= h) return 100; }
    int span = (int)g_b.hi_mv - (int)g_b.lo_mv;
    if (span <= 0) return -1;
    int pct = (g_last_mv - (int)g_b.lo_mv) * 100 / span;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

int batt_percent(void)
{
    if (g_full) { int h = g_b.hi_mv - 100; if (g_last_mv >= h) return 100; }
    if (g_med_n == 0) return batt_percent_raw();
    int tmp[11];
    memcpy(tmp, g_median, g_med_n * sizeof(int));
    for (int i = 1; i < g_med_n; i++) {
        int v = tmp[i], j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = v;
    }
    return tmp[g_med_n / 2];
}

bool batt_is_full(void) { return g_full; }
bool batt_is_low(void) { return g_low; }

int batt_health(void)
{
    int span = (int)g_b.hi_mv - (int)g_b.lo_mv;
    int nominal = BAT_NOMINAL_HI - BAT_NOMINAL_LO;
    int h = span * 100 / nominal;
    if (h < 0) h = 0;
    if (h > 100) h = 100;
    return h;
}
