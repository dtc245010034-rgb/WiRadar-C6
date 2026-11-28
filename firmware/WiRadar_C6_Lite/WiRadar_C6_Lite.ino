// WiRadar-C6 Lite  -  Wi-Fi CSI motion detection on ESP32-C6, hardware only (no web, no setup AP)
// ---------------------------------------------------------------------------
//  Same radar as WiRadar_C6, but nothing is served over Wi-Fi: it joins your router and everything
//  is shown on the TFT ST7735 1.44" + DHT11 + one button.
//  Shared code (config.h, csi_core.h, wifi_mode.h, tft_ui.h) is copied from ../WiRadar_C6/ by
//  scripts/sync_shared.py: edit the files there, not the copies here.
//
//  Wi-Fi: uses the network saved on the board (by the full WiRadar_C6 firmware / its phone setup page),
//         otherwise copy secrets.h.example to secrets.h and fill in your Wi-Fi.
//  Button (GPIO2, or the BOOT button GPIO9 for testing), actions happen on release:
//        short press   next screen   (TEMP/HUM -> RADAR -> GRAPH -> NET -> TEMP/HUM)
//        hold 1..10 s  recalibrate the radar
//        hold 10 s+    switch Wi-Fi 4 (11n) <-> Wi-Fi 6 (11ax) mode, reconnects
//  USB serial (921600): STATE lines for scripts/radar.sh; 'C' keep-alives stream raw CSI for scripts/raw.sh;
//        '4' / '6' select the Wi-Fi 4 / Wi-Fi 6 mode from the laptop.
//
// Board: ESP32C6 Dev Module | USB CDC On Boot: Disabled | Flash 16MB
// Wiring: see ../../docs/WIRING.md
// ---------------------------------------------------------------------------

#include <WiFi.h>
#include <Preferences.h>
#include <SPI.h>
#include <DHT.h>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "config.h"
#include "csi_core.h"
#include "wifi_mode.h"
#include "tft_ui.h"
#if __has_include("secrets.h")
#include "secrets.h"                       // optional: #define WIFI_SSID / WIFI_PASS
#endif

#define PROJECT_NAME  "WiRadar-C6 Lite"
#define HOSTNAME      "wiradar-lite"
#define STA_TIMEOUT   15000                // ms to wait for the router

DHT dht(PIN_DHT, DHT11);

// =====================================================================================
//  Wi-Fi (station only)
// =====================================================================================
static String netSsid, netIp;
static bool wifiOk = false;

static bool loadCreds(String &ssid, String &pass) {
  Preferences p;
  p.begin("wiradar", true);
  ssid = p.getString("ssid", "");
  pass = p.getString("pass", "");
  p.end();
#ifdef WIFI_SSID
  if (ssid.length() == 0) { ssid = WIFI_SSID; pass = WIFI_PASS; }
#endif
  return ssid.length() > 0;
}

static bool startSTA(const String &ssid, const String &pass) {
  pingStop();
  s_csiMacSet = false;
  wifiOk = false;
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_protocol(WIFI_IF_STA, wifiProto());      // Wi-Fi 4 or Wi-Fi 6 mode
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_TIMEOUT) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;

  wifiOk = true;
  netSsid = ssid;
  netIp = WiFi.localIP().toString();
  csiEnable();
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) setCsiSource(ap.bssid);
  pingStart((uint32_t)WiFi.gatewayIP());
  Serial.printf("[net] connected to %s, IP %s\n", ssid.c_str(), netIp.c_str());
  return true;
}

static void drawNet() {
  header("WIFI / NETWORK");
  canvas.setTextSize(2);
  if (wifiOk && WiFi.status() == WL_CONNECTED) {
    canvas.setTextColor(COL_GREEN); canvas.setCursor(4, 18); canvas.print("ONLINE");
    canvas.setTextSize(1);
    fitPrint(4, 38, COL_GRAY, "Wi-Fi network:", 21);
    fitPrint(4, 48, COL_WHITE, netSsid, 21);
    fitPrint(4, 62, COL_GRAY, "IP address:", 21);
    fitPrint(4, 72, COL_YEL, netIp, 21);
    fitPrint(4, 86, COL_GRAY, String(WiFi.RSSI()) + " dBm  offer W" + (wifi6 ? "6" : "4"), 21);
    int ls = linkStandard();
    fitPrint(4, 97, ls == 4 ? COL_GREEN : COL_YEL, String("Link: ") + stdTft(ls), 21);
    fitPrint(4, 108, COL_GRAY, String("CSI ") + pps + " pkt/s", 21);
  } else {
    canvas.setTextColor(COL_RED); canvas.setCursor(4, 18); canvas.print("NO WI-FI");
    canvas.setTextSize(1);
    fitPrint(4, 40, COL_GRAY, "Not connected.", 21);
    fitPrint(4, 52, COL_GRAY, "Set Wi-Fi with the full", 21);
    fitPrint(4, 62, COL_GRAY, "WiRadar_C6 firmware or", 21);
    fitPrint(4, 72, COL_GRAY, "secrets.h, then reflash.", 21);
  }
  fitPrint(4, 119, COL_GRAY, "10s hold: Wi-Fi 4/6", 21);
  push();
}

// =====================================================================================
//  Button
// =====================================================================================
static uint32_t btnEdge = 0;
static bool readBtn() { return digitalRead(PIN_BTN) == LOW || digitalRead(PIN_BOOT) == LOW; }

static void nextMode() {
  switch (mode) {
    case MODE_DHT:   mode = MODE_RADAR; break;
    case MODE_RADAR: mode = MODE_GRAPH; break;
    case MODE_GRAPH: mode = MODE_NET;   break;
    default:         mode = MODE_DHT;   break;
  }
}

static void handleButton() {                             // actions happen on release (overlay shows which)
  bool d = readBtn();
  uint32_t now = millis();
  if (d && !btnDown && now - btnEdge > 40) { btnDown = true; btnT = now; btnEdge = now; }
  else if (!d && btnDown && now - btnEdge > 40) {
    btnDown = false; btnEdge = now;
    uint32_t held = now - btnT;
    if (held >= 10000)     pendingWifi = wifi6 ? 4 : 6;  // switch Wi-Fi 4 <-> Wi-Fi 6
    else if (held >= 1000) startCalibration();           // recalibrate radar
    else                   nextMode();                   // next screen
  }
}

// =====================================================================================
//  setup / loop
// =====================================================================================
static void applyWifiMode(int wm) {                      // 4 or 6; no-op if already in that mode
  bool six = (wm == 6);
  if (six == wifi6) return;
  wifi6 = six;
  saveWifiMode();
  Serial.printf("[wifi] mode -> %s\n", wifi6 ? "Wi-Fi 6 (11b/g/n/ax)" : "Wi-Fi 4 (11b/g/n)");
  splash("Switching to", wifi6 ? "Wi-Fi 6 (11ax)..." : "Wi-Fi 4 (11n)...");
  String ssid, pass;
  if (loadCreds(ssid, pass)) startSTA(ssid, pass);
  mode = MODE_NET;
}

void setup() {
  s_serMux = xSemaphoreCreateMutex();
  Serial.setTxBufferSize(8192);
  Serial.begin(921600);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  dht.begin();

  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  tft.initR(TFT_TAB);
  tft.setSPISpeed(27000000);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);
  splash("Starting...");
  Serial.println("\n=== " PROJECT_NAME " ===");

  loadWifiMode();
  String ssid, pass;
  if (loadCreds(ssid, pass)) {
    splash("Connecting to", ssid.substring(0, 20).c_str());
    if (!startSTA(ssid, pass)) Serial.println("[net] could not join the Wi-Fi, will keep retrying");
  } else {
    Serial.println("[net] no Wi-Fi saved and no secrets.h");
    mode = MODE_NET;
  }
  randomSeed(micros());
}

void loop() {
  static uint32_t loopStart = 0, loopMax = 0, loopCnt = 0;
  { uint32_t t = millis(); if (loopStart) { uint32_t dt = t - loopStart; if (dt > loopMax) loopMax = dt; } loopStart = t; loopCnt++; }
  while (Serial.available()) {                          // 'C' keep-alive = raw CSI on, 'c' = off, '4'/'6' = Wi-Fi mode
    int ch = Serial.read();
    if (ch == 'C') { if ((int32_t)(s_rawUntil - millis()) <= 0) s_rawSeq = 0; s_rawUntil = millis() + 3000; }
    else if (ch == 'c') s_rawUntil = 0;
    else if (ch == 'S') s_mirrorUntil = millis() + 3000;
    else if (ch == 'N') nextMode();
    else if (ch == 'R') startCalibration();
    else if (ch == '4') pendingWifi = 4;
    else if (ch == '6') pendingWifi = 6;
  }
  handleButton();
  uint32_t now = millis();
  if (pendingWifi) { int w = pendingWifi; pendingWifi = 0; applyWifiMode(w); }

  static uint32_t lastTry = 0;                           // router gone / never reached: retry every 20 s
  if (WiFi.status() != WL_CONNECTED && now - lastTry > 20000) {
    lastTry = now;
    String ssid, pass;
    if (loadCreds(ssid, pass)) startSTA(ssid, pass);
  }

  updateMotionState(now);

  static uint32_t lastDhtRead = 0;
  // DHT11: every 3 s; when no sensor answers, only every 20 s (a failed read blocks ~170 ms)
  if (now - lastDhtRead > ((isnan(temp) && isnan(hum)) ? 20000u : 3000u) || lastDhtRead == 0) {
    lastDhtRead = now ? now : 1;
    float t = dht.readTemperature(), h = dht.readHumidity();
    temp = isnan(t) ? NAN : t;
    hum = isnan(h) ? NAN : h;
    if (isnan(temp) || isnan(hum)) Serial.println("[dht] no answer on GPIO1");
    else Serial.printf("[dht] %.1f C  %.0f %%\n", temp, hum);
  }

  static uint32_t lastDraw = 0;
  if (now - lastDraw >= (mode == MODE_RADAR ? 40u : 100u)) {
    lastDraw = now;
    switch (mode) {
      case MODE_NET:   drawNet(); break;
      case MODE_DHT:   drawDht(); break;
      case MODE_RADAR: sweep += 7.0f; if (sweep >= 360) sweep -= 360; drawRadar(); break;
      case MODE_GRAPH: drawGraph(); break;
    }
  }

  // Machine-readable state for the PC viewer (run2_viewer.sh), 10 lines per second
  static uint32_t lastState = 0;
  if (now - lastState >= 100) {
    lastState = now;
    char st[160];
    int sl = snprintf(st, sizeof(st), "STATE,%.3f,%.3f,%d,%d,%d,%d,%d,%lu,%.4f,%d,%d,%lu,%lu,%lu,%d\n", motionRatio(), motionLevel(),
                      detected ? 1 : 0, calibrating ? 1 : 0, calLeft(), haveData() ? 1 : 0, (int)s_rssi,
                      (unsigned long)pps, floorMotion, linkStandard(), wifi6 ? 1 : 0,
                      (unsigned long)fmtHEs, (unsigned long)fmtHTs, (unsigned long)fmtLEGs, (int)s_csiLen);
    if (serLock()) { Serial.write((const uint8_t *)st, sl); serUnlock(); }
  }

  static uint32_t lastLog = 0;
  if (now - lastLog > 2000) {
    lastLog = now;
    char lg[160];
    int ll = snprintf(lg, sizeof(lg), "[%s] %s  ratio=%.2f pps=%lu data=%d%s  loops/2s=%lu maxloop=%lums\n", "STA", netIp.c_str(),
                      motionRatio(), (unsigned long)pps, haveData(), detected ? "  MOTION" : "", (unsigned long)loopCnt, (unsigned long)loopMax);
    if (serLock()) { Serial.write((const uint8_t *)lg, ll); serUnlock(); }
    loopCnt = 0; loopMax = 0;
  }
  delay(2);
}
