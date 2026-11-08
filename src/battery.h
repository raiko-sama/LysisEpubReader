// battery.h - LiPo capacity learner: adapts the percentage mapping to the
// observed charge/discharge extremes so the shown capacity stays truthful
// even as the cell degrades. Matches the "per-cell LiPo learner" behavior
// of the original firmware (SD log + temperature).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void batt_init(void);                              /* load model from _bat_cycle.dat */
void batt_sample(int mv, bool charging, int temp_dc); /* feed one reading (mV, deci-C) */
void batt_sample_ex(int mv, bool charging, bool chg_stat_high, int temp_dc);
int  batt_percent(void);                           /* median-filtered learned % */
int  batt_percent_raw(void);                       /* unfiltered learned % */
int  batt_health(void);                            /* capacity health 0..100 */
bool batt_is_full(void);                           /* charger termination confirmed */
bool batt_is_low(void);                            /* near empty / shutdown imminent */
void batt_flush(void);                             /* persist model */
void batt_reset(void);                             /* back to nominal defaults */
int  batt_sample_count(void);                      /* samples fed since reset (tests/instrumentation) */
int  batt_hi_mv(void);
int  batt_lo_mv(void);

#ifdef __cplusplus
}
#endif
