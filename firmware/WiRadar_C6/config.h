// Pins and radar tuning shared by both firmwares (WiRadar_C6 and WiRadar_C6_Lite).
// Source of truth: firmware/WiRadar_C6/ ; the Lite folder gets a copy via scripts/sync_shared.py.
#pragma once

// ---------------- Pins (change if your board differs) ----------------
#define PIN_SCK   6
#define PIN_MOSI  7
#define PIN_CS    18
#define PIN_DC    19
#define PIN_RST   20
#define PIN_DHT   1
#define PIN_BTN   2      // external button to GND
#define PIN_BOOT  9      // on-board BOOT button (handy for testing)
#define TFT_TAB   INITR_144GREENTAB        // try INITR_BLACKTAB if the picture is shifted
#define TFT_ROTATION 3                     // module lies on its side in the landscape case (header on the right): 3. Upright: 0. If the picture is turned the wrong way try 1 (or 2)

// ---------------- Radar tuning ----------------
#define SEND_FREQUENCY   100     // pings per second toward the CSI source
#define CAL_SECONDS      5
#define DETECT_RATIO     1.8f    // detected when motion > floor * this
#define RELEASE_RATIO    1.45f   // released below this (hysteresis)
#define FULL_RATIO       4.0f    // level bar is full at floor * this
#define HOLD_MS          800     // keep "detected" this long after the last peak
#define RAW_EVERY        16      // raw CSI stream (scripts/raw.sh): 1 packet out of N (~6/s at 100 pkt/s)

#define W 128
#define H 128
