// radio.c - WiFi/Bluetooth idle monitoring.
#include "radio.h"

#define RADIO_DEFAULT_IDLE_MS (5u * 60u * 1000u)

static uint32_t g_idle_ms = RADIO_DEFAULT_IDLE_MS;
static uint32_t g_last_activity = 0;
static bool     g_inited = false;
static uint8_t  g_wifi_sent = 0;
static uint8_t  g_bt_sent = 0;

void radio_init(void)
{
    g_last_activity = 0;
    g_inited = false;
    g_wifi_sent = 0;
    g_bt_sent = 0;
}

void radio_set_idle_ms(uint32_t ms) { g_idle_ms = ms; }

void radio_mark_traffic(uint32_t now_ms)
{
    g_last_activity = now_ms;
    g_wifi_sent = 0;
    g_bt_sent = 0;
}

radio_event_t radio_tick(uint32_t now_ms, bool wifi_active, bool bt_active, bool traffic)
{
    if (!g_inited) {
        /* first reading establishes the baseline: no event can fire yet */
        g_inited = true;
        g_last_activity = now_ms;
        g_wifi_sent = 0;
        g_bt_sent = 0;
        return RADIO_NONE;
    }
    if (traffic) radio_mark_traffic(now_ms);

    radio_event_t ev = RADIO_NONE;

    if (wifi_active) {
        if (now_ms - g_last_activity >= g_idle_ms && !g_wifi_sent) {
            ev = RADIO_WIFI_OFF;
            g_wifi_sent = 1;
        }
    } else {
        g_wifi_sent = 0;
    }

    if (bt_active) {
        if (now_ms - g_last_activity >= g_idle_ms && !g_bt_sent) {
            if (ev == RADIO_NONE) ev = RADIO_BT_OFF;
            g_bt_sent = 1;
        }
    } else {
        g_bt_sent = 0;
    }

    return ev;
}
