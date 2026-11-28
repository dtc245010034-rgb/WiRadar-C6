// DHT11 temperature / humidity, read in its own task so a slow or failed read (~170 ms) never stalls the main
// loop (buttons, web, TFT). Shared by both firmwares. Needs tft_ui.h first (it owns `temp` / `hum`).
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
    if (ok) Serial.printf("[dht] %.1f C  %.0f %%\n", t, h);
    else Serial.println("[dht] no answer on GPIO1");
    vTaskDelay(pdMS_TO_TICKS(ok ? 3000 : 20000));        // no sensor: try again only every 20 s
  }
}

static void climateStart() {
  dht.begin();
  xTaskCreate(climateTask, "dht", 4096, NULL, 1, NULL);
}
