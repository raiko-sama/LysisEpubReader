// radio.h - WiFi/Bluetooth traffic monitor: turns off the antenna that has
// been idle for too long to save power. Pure logic, unit-testable.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADIO_NONE = 0,
    RADIO_WIFI_OFF,
    RADIO_BT_OFF,
} radio_event_t;

void radio_init(void);
void radio_set_idle_ms(uint32_t ms);            /* default 5 minutes */
void radio_mark_traffic(uint32_t now_ms);       /* called on app network activity */

/* Returns the event when an antenna has been idle for the configured time.
   wifi_active / bt_active = antenna currently enabled; traffic = activity in
   this tick. */
radio_event_t radio_tick(uint32_t now_ms, bool wifi_active, bool bt_active, bool traffic);

#ifdef __cplusplus
}
#endif
