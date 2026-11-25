#!/usr/bin/env python3
"""Generates firmware/WiRadar_C6_Lite/WiRadar_C6_Lite.ino from the full firmware.

The CSI / radar / Wi-Fi 4-6 mode / TFT code is shared; the Lite version drops the setup Access Point,
DNS, mDNS and web server. Edit firmware/WiRadar_C6/WiRadar_C6.ino, then run:

    python3 scripts/make_lite.py
"""
import os, re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = open(os.path.join(ROOT, 'firmware/WiRadar_C6/WiRadar_C6.ino')).read()
BAR = "// =====================================================================================\n"


def mark(name):
    return BAR + "//  " + name


def sub(old, new, s, count=1):
    assert old in s, "pattern not found: " + old[:70]
    return s.replace(old, new, count)


i_csi, i_net, i_tft, i_btn, i_set = (src.index(mark(n)) for n in
    ("CSI -> motion level", "Network: saved credentials", "TFT screens", "Button", "setup / loop"))
tuning = src[src.index("// ---------------- Pins"):src.index("Adafruit_ST7735 tft(")]

head = '''// WiRadar-C6 Lite  -  Wi-Fi CSI motion detection on ESP32-C6, hardware only (no web, no setup AP)
// ---------------------------------------------------------------------------
//  Same radar as WiRadar_C6, but nothing is served over Wi-Fi: it joins your router and everything
//  is shown on the TFT ST7735 1.44" + DHT11 + one button.
//  (Generated from ../WiRadar_C6/WiRadar_C6.ino by scripts/make_lite.py: edit the full firmware, not this file.)
//
//  Wi-Fi: uses the network saved on the board (by the full WiRadar_C6 firmware / its phone setup page),
//         otherwise copy secrets.h.example to secrets.h and fill in your Wi-Fi.
//  Button (GPIO2, or the BOOT button GPIO9 for testing), actions happen on release:
//        short press   next screen   (TEMP/HUM -> RADAR -> GRAPH -> NET -> TEMP/HUM)
//        hold 1..10 s  recalibrate the radar
//        hold 10 s+    switch Wi-Fi 4 (11n) <-> Wi-Fi 6 (11ax) mode, reconnects
//  USB serial (921600): STATE lines for run2_viewer.sh; 'C' keep-alives stream raw CSI for run_viewer.sh;
//        '4' / '6' select the Wi-Fi 4 / Wi-Fi 6 mode from the laptop.
//
// Board: ESP32C6 Dev Module | USB CDC On Boot: Disabled | Flash 16MB
// Wiring: see ../../docs/WIRING.md
// ---------------------------------------------------------------------------

#include <WiFi.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <DHT.h>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#if __has_include("secrets.h")
#include "secrets.h"                       // optional: #define WIFI_SSID / WIFI_PASS
#endif

#define PROJECT_NAME  "WiRadar-C6 Lite"
#define HOSTNAME      "wiradar-lite"
#define STA_TIMEOUT   15000                // ms to wait for the router

''' + tuning + '''Adafruit_ST7735 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 canvas(W, H);
DHT dht(PIN_DHT, DHT11);

'''
csi = src[i_csi:i_net]            # CSI core + shared Wi-Fi 4/6 block

net = BAR + '''//  Wi-Fi (station only)
''' + BAR + '''static float temp = NAN, hum = NAN;
static String netSsid, netIp;
static bool wifiOk = false;

static void setCsiSource(const uint8_t *mac) {
  memcpy(s_csiMac, mac, 6);
  s_csiMacSet = true;
  startCalibration();
}

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
  Serial.printf("[net] connected to %s, IP %s\\n", ssid.c_str(), netIp.c_str());
  return true;
}

'''

tft = src[i_tft:i_btn]
tft = sub("static Mode mode = MODE_NET;", "static Mode mode = MODE_DHT;", tft)
a = tft.index("static void push() {"); b = tft.index("static void header(")
tft = tft[:a] + '''static void push() {
  if (btnDown && millis() - btnT > 1000) {               // held: show what releasing now will do
    uint32_t held = millis() - btnT;
    const char *act = held >= 10000 ? (wifi6 ? "Release: Wi-Fi 4" : "Release: Wi-Fi 6") : "Release: recalibrate";
    canvas.fillRect(4, 90, 120, 36, COL_BG);
    canvas.drawRect(4, 90, 120, 36, COL_YEL);
    canvas.setTextSize(1);
    canvas.setTextColor(COL_YEL);  canvas.setCursor(9, 95);  canvas.print(act);
    canvas.setTextColor(COL_GRAY); canvas.setCursor(9, 105); canvas.print("10s: Wi-Fi 4/6");
    canvas.drawRect(9, 116, 110, 5, COL_GRAY);
    canvas.fillRect(10, 117, (int)(108UL * min<uint32_t>(held, 10000) / 10000), 3, held >= 10000 ? COL_GREEN : COL_YEL);
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
}

''' + tft[b:]
a = tft.index("static void drawNet() {"); b = tft.index("static void drawDht() {")
tft = tft[:a] + '''static void drawNet() {
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

''' + tft[b:]
tft = re.sub(r'netMode == NET_AP \? "[^"]*" : ("[^"]*")', r'\1', tft)
assert "netMode" not in tft and "AP_PASS" not in tft

btn = BAR + '''//  Button
''' + BAR + '''static uint32_t btnEdge = 0;
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

'''

setloop_src = src[i_set:]
splash = setloop_src[setloop_src.index("static void splash("):setloop_src.index("static void applyWifiMode(")]
tail = setloop_src[setloop_src.index("  updateMotionState(now);"):]
tail = sub('netMode == NET_AP ? "AP" : "STA", netIp.c_str(),', '"STA", netIp.c_str(),', tail)
assert "netMode" not in tail
setloop = BAR + '''//  setup / loop
''' + BAR + splash + '''static void applyWifiMode(int wm) {                      // 4 or 6; no-op if already in that mode
  bool six = (wm == 6);
  if (six == wifi6) return;
  wifi6 = six;
  saveWifiMode();
  Serial.printf("[wifi] mode -> %s\\n", wifi6 ? "Wi-Fi 6 (11b/g/n/ax)" : "Wi-Fi 4 (11b/g/n)");
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
  Serial.println("\\n=== " PROJECT_NAME " ===");

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

''' + tail

out = head + csi + net + tft + btn + setloop
os.makedirs(os.path.join(ROOT, 'firmware/WiRadar_C6_Lite'), exist_ok=True)
open(os.path.join(ROOT, 'firmware/WiRadar_C6_Lite/WiRadar_C6_Lite.ino'), 'w').write(out)
print("WiRadar_C6_Lite.ino written,", out.count("\n"), "lines")
