// DHT11 temperature / humidity, read in its own task so a slow or failed read (~170 ms) never stalls the main
// loop (buttons, web, TFT). Shared by both firmwares. Needs csi_core.h (serial lock) and tft_ui.h (owns `temp` / `hum`) first.
#pragma once
#include <DHT.h>
#include "config.h"

static DHT dht(PIN_DHT, DHT11);

static void climateTask(void *) {
  for (;;) {
    float t = dht.readTemperature(), h = dht.readHumidity();
    bool ok = !isnan(t) && !isnan(h);
    temp = ok ? t : NAN;                                 // both values change together
    hum  = ok ? h : NAN;
    char lg[64];                                         // whole line under the serial lock, so it never lands inside a STATE line
    int n = ok ? snprintf(lg, sizeof(lg), "[dht] %.1f C  %.0f %%\n", t, h) : snprintf(lg, sizeof(lg), "[dht] no answer on GPIO%d\n", PIN_DHT);
    if (serLock()) { Serial.write((const uint8_t *)lg, n); serUnlock(); }
    vTaskDelay(pdMS_TO_TICKS(ok ? 3000 : 20000));        // no sensor: try again only every 20 s
  }
}

static void climateStart() {
  dht.begin();
  xTaskCreate(climateTask, "dht", 4096, NULL, 1, NULL);
}
