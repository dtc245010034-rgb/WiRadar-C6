// Wi-Fi 4 / Wi-Fi 6 link mode. Shared by both firmwares. Needs csi_core.h first.
#pragma once
#include <Preferences.h>
#include "esp_wifi.h"

// ==== WIFI-MODE-BEGIN ===============================================================
//  Wi-Fi 4 / Wi-Fi 6 link mode: which 802.11 standards THIS board offers when it connects.
//  The router/AP is never changed: the link only reaches 802.11ax if both sides support it.
static bool wifi6 = true;                              // true: 11b/g/n/ax   false: 11b/g/n only
static uint8_t wifiProto() { return WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | (wifi6 ? WIFI_PROTOCOL_11AX : 0); }
static void loadWifiMode() { Preferences pf; pf.begin("wiradar", true);  wifi6 = pf.getBool("w6", true); pf.end(); }
static void saveWifiMode() { Preferences pf; pf.begin("wiradar", false); pf.putBool("w6", wifi6);        pf.end(); }
static volatile int pendingWifi = 0;                   // 4 or 6: switch requested (button, web or serial), done in loop()

// standard actually in use: 0 none, 1 = 11b, 2 = 11g, 3 = 11n (Wi-Fi 4), 4 = 11ax (Wi-Fi 6)
static int linkStandard() {
  wifi_phy_mode_t m;
  if (esp_wifi_sta_get_negotiated_phymode(&m) == ESP_OK) {          // station side: what was negotiated
    switch (m) {
      case WIFI_PHY_MODE_HE20: return 4;
      case WIFI_PHY_MODE_HT20: case WIFI_PHY_MODE_HT40: case WIFI_PHY_MODE_VHT20: return 3;
      case WIFI_PHY_MODE_11G: case WIFI_PHY_MODE_11A: return 2;
      case WIFI_PHY_MODE_11B: return 1;
      default: break;
    }
  }
  if (fmtHEs) return 4;                                              // AP side: judge by received frames
  if (fmtHTs) return 3;
  if (fmtLEGs) return 2;
  return 0;
}
static const char *stdName(int s) {
  switch (s) { case 4: return "Wi-Fi 6 (802.11ax)"; case 3: return "Wi-Fi 4 (802.11n)"; case 2: return "802.11g (legacy)"; case 1: return "802.11b (legacy)"; default: return "no link"; }
}
static const char *stdTft(int s) {
  switch (s) { case 4: return "Wi-Fi 6 (11ax)"; case 3: return "Wi-Fi 4 (11n)"; case 2: return "11g (legacy)"; case 1: return "11b (legacy)"; default: return "no link"; }
}
static const char *stdTag(int s) { return s == 4 ? "W6" : (s == 3 ? "W4" : "--"); }
// ==== WIFI-MODE-END =================================================================
