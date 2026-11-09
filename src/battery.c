// battery.c - LiPo capacity estimation with monotonic discharge tracking.
//
// The Paper S3 has a 3.7 V / 1800 mAh single-cell LiPo and a LGS4056H charger.
// There is no current-sense ADC, so the remaining percentage is estimated from
// a filtered open-circuit-voltage curve and learned charge/discharge endpoints.
//
// Important behaviour: while the device is discharging the displayed percentage
// is monotonic. A voltage recovery after load removal can never make the shown
// percentage go up. Increases are accepted only while charging or after a full
// charge. The model, the last accepted percentage and the history are persisted
// on the SD card.
#include "battery.h"
#include "config.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
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
    int16_t  last_pct;   /* last accepted monotonic percentage, -1 unknown */
    int16_t  last_mv;    /* voltage paired with last_pct, -1 unknown */
} batt_file_t;

#define BAT_FILE_V1_SIZE ((size_t)offsetof(batt_file_t, last_pct))

typedef struct {
    int64_t epoch;
    int16_t mv;
    int16_t temp_dc;   /* deci-degrees C (25.0 C = 250) */
    int8_t  pct;
    int8_t  charging;
} batt_log_t;

typedef struct { int16_t mv; int8_t pct; } curve_point_t;

/* Typical single-cell LiPo OCV curve. The learner adapts the endpoints, while
   the curve gives a realistic slope between them instead of a straight line. */
static const curve_point_t kLipoCurve[] = {
    {4200,100},{4150, 97},{4100, 93},{4050, 89},{4000, 84},
    {3950, 79},{3900, 73},{3850, 67},{3800, 60},{3750, 53},
    {3700, 45},{3650, 37},{3600, 29},{3550, 22},{3500, 16},
    {3450, 11},{3400,  7},{3350,  3},{3300,  0}
};

static batt_file_t g_b;
static bool g_last_charging = false;
static bool g_first = true;
static bool g_dirty = false;
static int  g_median[11];
static int  g_med_n = 0;
static int  g_med_i = 0;
static int  g_last_mv = -1;
static int  g_soc = -1;         /* monotonic displayed percentage */
static int  g_stable = 0;
static bool g_full = false;
static bool g_low = false;

#define BAT_LOG_BATCH 8
static batt_log_t g_blog[BAT_LOG_BATCH];
static int        g_blog_n = 0;

static int clamp_pct(int p)
{
    if (p < 0) return 0;
    if (p > 100) return 100;
    return p;
}

static int pct_from_mv(int mv)
{
    if (mv >= g_b.hi_mv) return 100;
    if (mv <= g_b.lo_mv) return 0;

    for (size_t i = 1; i < sizeof(kLipoCurve) / sizeof(kLipoCurve[0]); i++) {
        if (mv >= kLipoCurve[i].mv) {
            int hi_mv = kLipoCurve[i - 1].mv;
            int lo_mv = kLipoCurve[i].mv;
            int hi_pct = kLipoCurve[i - 1].pct;
            int lo_pct = kLipoCurve[i].pct;
            int span = hi_mv - lo_mv;
            if (span <= 0) return lo_pct;
            return clamp_pct(lo_pct + (mv - lo_mv) * (hi_pct - lo_pct) / span);
        }
    }
    return 0;
}

static void write_log_batch(void)
{
    if (g_blog_n <= 0) return;

    FILE* f = fopen(ST_BAT_LOG, "ab");
    if (f) {
        fwrite(g_blog, sizeof(batt_log_t), (size_t)g_blog_n, f);
        fclose(f);
    }

    f = fopen(ST_BAT_HISTORY, "a");
    if (f) {
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0) {
            fputs("epoch,mv,pct,charging,temp_dc\n", f);
        }
        for (int i = 0; i < g_blog_n; i++) {
            fprintf(f, "%lld,%d,%d,%d,%d\n",
                    (long long)g_blog[i].epoch,
                    (int)g_blog[i].mv,
                    (int)g_blog[i].pct,
                    (int)g_blog[i].charging,
                    (int)g_blog[i].temp_dc);
        }
        fclose(f);
    }
    g_blog_n = 0;
}

static void load(void)
{
    memset(&g_b, 0, sizeof(g_b));
    FILE* f = fopen(ST_BAT_CYCLE, "rb");
    size_t got = 0;
    if (f) {
        got = fread(&g_b, 1, sizeof(g_b), f);
        fclose(f);
    }
    bool valid = got >= BAT_FILE_V1_SIZE && g_b.magic == BAT_MAGIC &&
                 g_b.hi_mv > g_b.lo_mv && g_b.hi_mv <= 5000 && g_b.lo_mv >= 2000;
    if (!valid) {
        memset(&g_b, 0, sizeof(g_b));
        g_b.magic = BAT_MAGIC;
        g_b.hi_mv = BAT_NOMINAL_HI;
        g_b.lo_mv = BAT_NOMINAL_LO;
        g_b.cycles = 0;
        g_b.state = 0;
        g_b.reserved = 0;
        g_b.samples = 0;
        g_b.last_pct = -1;
        g_b.last_mv = -1;
    } else if (got < sizeof(g_b)) {
        /* Migrate the previous shorter state record. */
        g_b.last_pct = -1;
        g_b.last_mv = -1;
    }

    g_dirty = false;
    g_last_charging = false;
    g_first = true;
    g_stable = 0;
    g_last_mv = -1;
    g_soc = (g_b.last_pct >= 0 && g_b.last_pct <= 100) ? g_b.last_pct : -1;
    g_full = false;
    g_low = false;
    g_med_n = 0;
    g_med_i = 0;
    g_blog_n = 0;
}

void batt_init(void) { load(); }

int batt_sample_count(void) { return (int)(g_b.samples > INT32_MAX ? INT32_MAX : g_b.samples); }
int batt_hi_mv(void) { return g_b.hi_mv; }
int batt_lo_mv(void) { return g_b.lo_mv; }

void batt_reset(void)
{
    memset(&g_b, 0, sizeof(g_b));
    g_b.magic = BAT_MAGIC;
    g_b.hi_mv = BAT_NOMINAL_HI;
    g_b.lo_mv = BAT_NOMINAL_LO;
    g_b.last_pct = -1;
    g_b.last_mv = -1;
    g_dirty = true;
    g_med_n = 0;
    g_med_i = 0;
    g_blog_n = 0;
    g_last_charging = false;
    g_first = true;
    g_stable = 0;
    g_last_mv = -1;
    g_soc = -1;
    g_full = false;
    g_low = false;
}

void batt_flush(void)
{
    write_log_batch();
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

    /* Stability: an endpoint is learned only from a settled voltage. */
    if (g_last_mv >= 0 && abs(mv - g_last_mv) <= BAT_STABLE_MV) g_stable++;
    else g_stable = 1;
    g_last_mv = mv;

    int full_mv = g_b.hi_mv - 30;
    if (full_mv < BAT_FULL_HOLD_MV) full_mv = BAT_FULL_HOLD_MV;
    if (full_mv > BAT_FULL_CONFIRM_MV) full_mv = BAT_FULL_CONFIRM_MV;
    int hold_mv = full_mv - 70;
    if (chg_stat_high && mv >= full_mv) {
        g_full = true;
    } else if (mv < hold_mv || (charging && mv < full_mv)) {
        g_full = false;
    }

    if (g_first) {
        /* First reading after boot: never learn an endpoint from a loaded or
           charger-node voltage. */
        g_first = false;
        g_last_charging = charging;
        g_b.state = charging ? 1 : 2;
    } else if (charging && !g_last_charging) {
        if (g_stable >= BAT_STABLE_N && mv <= g_b.lo_mv + BAT_NEAR_MV) {
            ewm(&g_b.lo_mv, mv, BAT_A_EMPTY);
            if (g_b.lo_mv > g_b.hi_mv - BAT_MIN_SPAN) g_b.lo_mv = g_b.hi_mv - BAT_MIN_SPAN;
            g_dirty = true;
        }
        g_b.state = 1;
    } else if (!charging && g_last_charging) {
        if (g_stable >= BAT_STABLE_N && mv >= g_b.hi_mv - BAT_NEAR_MV) {
            ewm(&g_b.hi_mv, mv, BAT_A_FULL);
            if (g_b.hi_mv < g_b.lo_mv + BAT_MIN_SPAN) g_b.hi_mv = g_b.lo_mv + BAT_MIN_SPAN;
            g_b.cycles++;
            g_dirty = true;
        }
        g_b.state = 2;
    } else {
        g_b.state = charging ? 1 : 2;
        if (charging && mv > g_b.hi_mv) ewm(&g_b.hi_mv, mv, BAT_A_TRACK);
        if (!charging && mv < g_b.lo_mv) ewm(&g_b.lo_mv, mv, BAT_A_TRACK);
    }
    g_last_charging = charging;

    if (!charging && mv <= BAT_LOW_CUTOFF_MV) g_low = true;
    else if (!charging && mv >= BAT_LOW_CLEAR_MV) g_low = false;

    int raw = pct_from_mv(mv);
    if (g_full) raw = 100;

    if (g_soc < 0) {
        g_soc = raw;
    } else if (charging || g_full) {
        /* While charging the percentage may rise, but never fall because of
           a momentary load sag. */
        if (raw > g_soc) g_soc = raw;
        if (g_full) g_soc = 100;
    } else {
        /* Discharging: the displayed percentage can only go down. This is the
           key fix for a standby voltage recovery making 93% become 96%. */
        if (raw < g_soc) g_soc = raw;
    }
    g_soc = clamp_pct(g_soc);
    g_b.last_pct = (int16_t)g_soc;
    g_b.last_mv = (int16_t)mv;
    g_dirty = true;

    if (g_soc >= 0) {
        g_median[g_med_i] = g_soc;
        g_med_i = (g_med_i + 1) % 11;
        if (g_med_n < 11) g_med_n++;
    }

    batt_log_t lg;
    lg.epoch = (int64_t)time(NULL);
    lg.mv = (int16_t)mv;
    lg.temp_dc = (int16_t)temp_dc;
    lg.pct = (int8_t)g_soc;
    lg.charging = charging ? 1 : 0;
    g_blog[g_blog_n++] = lg;
    if (g_blog_n >= BAT_LOG_BATCH) write_log_batch();

    if ((g_b.samples % 5) == 0) batt_flush();
}

int batt_percent_raw(void)
{
    if (g_full) return 100;
    if (g_soc >= 0) return g_soc;
    if (g_last_mv < 0) return -1;
    return pct_from_mv(g_last_mv);
}

int batt_percent(void)
{
    if (g_full) return 100;
    if (g_med_n == 0) return batt_percent_raw();
    int tmp[11];
    memcpy(tmp, g_median, (size_t)g_med_n * sizeof(int));
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
