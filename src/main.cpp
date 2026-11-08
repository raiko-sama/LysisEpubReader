#include <Arduino.h>
#include "bridge.h"

extern "C" {
void app_setup(void);
int  app_loop(void);            /* returns ms the loop may sleep */
bool app_can_deep_idle(void);   /* light sleep allowed (E1 guard) */
}

/* The Arduino loopTask is created with an 8 KB stack (framework default)
   and M5GFX 0.2.x text rendering overflows it while drawing the reader.
   The whole application runs on a dedicated 32 KB task instead; the
   Arduino loop stays as a shallow idle holder. */
static void app_task(void* arg)
{
    (void)arg;
    app_setup();
    for (;;) {
        int ms = app_loop();
        /* light sleep in idle when nothing needs the CPU soon (E1);
           falls back to a plain delay when the guards say so */
        if (ms > 0) {
            if (app_can_deep_idle()) brd_idle_sleep((uint32_t)ms);
            else delay((uint32_t)ms);
        }
    }
}

void setup()
{
    xTaskCreatePinnedToCore(app_task, "lysis", 32768, NULL, 1, NULL, 1);
}

void loop()
{
    delay(1000);   /* idle: the real loop runs on the app task */
}
