  // WiRadar-C6  -  Wi-Fi CSI motion detection on ESP32-C6
// ---------------------------------------------------------------------------
//  * Boot: join the saved Wi-Fi; if none (or it fails) open the setup Access Point
//          "WiRadar-C6-Setup" (random password, shown on the TFT and Serial). A phone joins it and picks the home Wi-Fi.
//  * Dashboard for phones: http://<ip>  or  http://wiradar.local   (Radar / Graph / Climate / Wi-Fi)
//  * TFT ST7735 1.44" (optional): screens NET, TEMP/HUMIDITY, RADAR, GRAPH
//  * Button (GPIO2, or the BOOT button GPIO9 for testing):
//        short press      next screen            (NET -> TEMP/HUM -> RADAR -> GRAPH -> NET)
//        hold 1..5 s      recalibrate the radar  (on release)
//        hold 5..10 s     start the setup Access Point again (on release)
//        hold 10 s+       switch Wi-Fi 4 (11n) <-> Wi-Fi 6 (11ax) mode, reconnects (on release)
//  * USB serial (921600): prints STATE lines that scripts/radar.sh draws as a radar on the PC.
//    Send 'C' (repeat every second) to also stream raw CSI_DATA lines (scripts/raw.sh does this), 'c' to stop.
//    Send '4' or '6' to select the Wi-Fi 4 / Wi-Fi 6 mode from the laptop.
//    'S' (every second) mirrors the screen to scripts/mirror.sh; 'N' = next screen, 'R' = recalibrate.
//  * Test without any parts: the board alone works (the phone's link is used as the CSI source).
//
// Board: ESP32C6 Dev Module | USB CDC On Boot: Disabled | Flash 16MB
// Wiring: see ../../docs/WIRING.md
// ---------------------------------------------------------------------------

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#define WIRADAR_FULL
#include "config.h"
#include "csi_core.h"
#include "wifi_mode.h"
#include "tft_ui.h"
#include "climate.h"
#include "web_ui.h"

// ---------------- Project ----------------
#define PROJECT_NAME  "WiRadar-C6"
#define AP_SSID       "WiRadar-C6-Setup"
#define HOSTNAME      "wiradar"            // http://wiradar.local
#define STA_TIMEOUT   15000                // ms to wait for the home Wi-Fi at boot


WebServer server(80);
DNSServer dns;
Preferences prefs;



// =====================================================================================
//  Network: saved credentials, STA, setup AP, web server
// =====================================================================================
enum NetMode { NET_NONE, NET_STA, NET_AP };
static NetMode netMode = NET_NONE;
static bool radarApMode() { return netMode == NET_AP; }
static String savedSsid, savedPass, netSsid, netIp;
static volatile bool pendingStation = false;           // set by Wi-Fi event, handled in loop
static uint8_t pendingMac[6]; static uint32_t pendingIp = 0;
static bool requestAP = false;

static void loadCreds() {
  prefs.begin("wiradar", true);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();
}
static void saveCreds(const String &s, const String &p) {
  prefs.begin("wiradar", false);
  prefs.putString("ssid", s); prefs.putString("pass", p);
  prefs.end();
}
static void clearCreds() {                             // forget the home Wi-Fi only (keeps the AP password and the Wi-Fi 4/6 mode)
  prefs.begin("wiradar", false); prefs.remove("ssid"); prefs.remove("pass"); prefs.end();
  savedSsid = ""; savedPass = "";
}

// Setup-AP password: random per device, created on the first boot and shown on the TFT / Serial (never a published default).
static String apPass;
static void loadApPass() {
  prefs.begin("wiradar", false);
  apPass = prefs.getString("appass", "");
  if (apPass.length() < 8) {
    static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";       // no look-alike characters (i l o 0 1)
    apPass = "";
    for (int i = 0; i < 10; i++) apPass += A[esp_random() % (sizeof(A) - 1)];
    prefs.putString("appass", apPass);
  }
  prefs.end();
}

static String jsonEsc(const String &s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {     // a phone got an IP from our DHCP
    memcpy(pendingMac, info.wifi_ap_staipassigned.mac, 6);
    pendingIp = info.wifi_ap_staipassigned.ip.addr;
    pendingStation = true;
  }
}


static void stopServices() {
  pingStop();
  s_csiMacSet = false;
  dns.stop();
  MDNS.end();
}

static bool startSTA(const String &ssid, const String &pass) {
  stopServices();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_protocol(WIFI_IF_STA, wifiProto());      // Wi-Fi 4 or Wi-Fi 6 mode
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_TIMEOUT) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;

  netMode = NET_STA;
  netSsid = ssid;
  netIp = WiFi.localIP().toString();
  csiEnable();
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) setCsiSource(ap.bssid);
  pingStart((uint32_t)WiFi.gatewayIP());
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  Serial.printf("[net] connected to %s, IP %s\n", ssid.c_str(), netIp.c_str());
  return true;
}

static void startAP() {
  stopServices();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_protocol(WIFI_IF_AP, wifiProto());       // the setup AP offers the same standards
  esp_wifi_set_protocol(WIFI_IF_STA, wifiProto());
  WiFi.softAP(AP_SSID, apPass.c_str(), 1, 0, 4);
  delay(300);
  netMode = NET_AP;
  netSsid = AP_SSID;
  netIp = WiFi.softAPIP().toString();
  dns.start(53, "*", WiFi.softAPIP());                   // captive portal: every name -> us
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  csiEnable();
  startCalibration();                                    // CSI starts when a phone joins
  Serial.printf("[net] setup AP \"%s\" (pass %s), IP %s\n", AP_SSID, apPass.c_str(), netIp.c_str());
}

// ---- web handlers ----

static void sendJsonNum(String &o, const char *key, float v, int dec) {
  o += '"'; o += key; o += "\":";
  if (isnan(v)) o += "null"; else o += String(v, dec);
  o += ',';
}

static void handleState() {
  String o; o.reserve(420);
  o += "{\"net\":\""; o += (netMode == NET_AP ? "AP" : "STA"); o += "\",";
  o += "\"ssid\":\""; o += jsonEsc(netSsid); o += "\",";
  o += "\"ip\":\""; o += netIp; o += "\",";
  o += "\"sta\":"; o += (netMode == NET_AP ? WiFi.softAPgetStationNum() : 0); o += ',';
  o += "\"data\":"; o += (haveData() ? "true" : "false"); o += ',';
  o += "\"cal\":"; o += (calibrating ? "true" : "false"); o += ',';
  o += "\"calLeft\":"; o += calLeft(); o += ',';
  o += "\"detect\":"; o += (detected ? "true" : "false"); o += ',';
  sendJsonNum(o, "ratio", motionRatio(), 3);
  sendJsonNum(o, "level", motionLevel(), 3);
  sendJsonNum(o, "temp", temp, 1);
  sendJsonNum(o, "hum", hum, 0);
  o += "\"std\":"; o += linkStandard(); o += ',';
  o += "\"stdName\":\""; o += stdName(linkStandard()); o += "\",";
  o += "\"w6\":"; o += (wifi6 ? "true" : "false"); o += ',';
  o += "\"he\":"; o += (unsigned long)fmtHEs; o += ',';
  o += "\"ht\":"; o += (unsigned long)fmtHTs; o += ',';
  o += "\"leg\":"; o += (unsigned long)fmtLEGs; o += ',';
  o += "\"csiLen\":"; o += (int)s_csiLen; o += ',';
  o += "\"rssi\":"; o += (int)s_rssi; o += ',';
  o += "\"pps\":"; o += (unsigned long)pps; o += ',';
  o += "\"up\":"; o += (unsigned long)(millis() / 1000);
  o += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", o);
}

static void handleScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); server.send(200, "application/json", "{\"scanning\":true}"); return; }
  if (n == WIFI_SCAN_RUNNING) { server.send(200, "application/json", "{\"scanning\":true}"); return; }
  String o = "{\"scanning\":false,\"nets\":[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    if (!first) o += ',';
    first = false;
    o += "{\"ssid\":\""; o += jsonEsc(s); o += "\",\"rssi\":"; o += WiFi.RSSI(i);
    o += ",\"open\":"; o += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false"); o += '}';
  }
  o += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", o);
}

static void handleSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (ssid.length() < 1 || ssid.length() > 32 || (pass.length() && (pass.length() < 8 || pass.length() > 63))) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"Invalid name or password (8-63 characters).\"}");
    return;
  }
  saveCreds(ssid, pass);
  server.send(200, "application/json", "{\"ok\":true}");
  delay(800);
  ESP.restart();                                         // reboot straight into the home Wi-Fi
}

static void handleCaptive() {
  server.sendHeader("Location", String("http://") + netIp + "/setup");
  server.send(302, "text/plain", "");
}

static void startServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/setup", HTTP_GET, []() { server.send_P(200, "text/html", SETUP_HTML); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/recal", HTTP_POST, []() { startCalibration(); server.send(200, "text/plain", "ok"); });
  server.on("/api/wifimode", HTTP_POST, []() {            // m=4 or m=6; done in loop() after the reply
    String m = server.arg("m");
    if (m == "4" || m == "6") { server.send(200, "application/json", "{\"ok\":true}"); pendingWifi = (m == "6") ? 6 : 4; }
    else server.send(400, "application/json", "{\"ok\":false}");
  });
  server.on("/api/forget", HTTP_POST, []() { clearCreds(); server.send(200, "text/plain", "ok"); });
  server.onNotFound([]() {
    if (netMode == NET_AP) handleCaptive();              // generate_204, hotspot-detect.html, ...
    else server.send(404, "text/plain", "Not found");
  });
  server.begin();
}

// =====================================================================================
//  TFT screens
// =====================================================================================
static void drawNet() {
  header("WIFI / NETWORK");
  canvas.setTextSize(2);
  if (netMode == NET_AP) {
    canvas.setTextColor(COL_YEL); canvas.setCursor(4, 18); canvas.print("SETUP MODE");
    canvas.setTextSize(1);
    fitPrint(4, 38, COL_GRAY, "1) Join this Wi-Fi:", 21);
    fitPrint(4, 48, COL_WHITE, netSsid, 21);
    fitPrint(4, 58, COL_GRAY, String("   pass: ") + apPass, 21);
    fitPrint(4, 72, COL_GRAY, "2) Open in browser:", 21);
    fitPrint(4, 82, COL_YEL, String("http://") + netIp, 21);
    fitPrint(4, 96, COL_GRAY, String("Phones joined: ") + WiFi.softAPgetStationNum(), 21);
    fitPrint(4, 107, COL_GRAY, String("Offers Wi-Fi ") + (wifi6 ? "6 (11ax)" : "4 (11n)"), 21);
  } else {
    canvas.setTextColor(COL_GREEN); canvas.setCursor(4, 18); canvas.print("ONLINE");
    canvas.setTextSize(1);
    fitPrint(4, 38, COL_GRAY, "Wi-Fi network:", 21);
    fitPrint(4, 48, COL_WHITE, netSsid, 21);
    fitPrint(4, 62, COL_GRAY, "IP address:", 21);
    fitPrint(4, 72, COL_YEL, netIp, 21);
    fitPrint(4, 86, COL_GRAY, "or wiradar.local", 21);
    fitPrint(4, 97, COL_GRAY, String(WiFi.RSSI()) + " dBm  offer W" + (wifi6 ? "6" : "4"), 21);
    int ls = linkStandard();
    fitPrint(4, 108, ls == 4 ? COL_GREEN : COL_YEL, String("Link: ") + stdTft(ls), 21);
  }
  fitPrint(4, 119, COL_GRAY, "5s:AP  10s:Wi-Fi 4/6", 21);
  push();
}

// =====================================================================================
//  Button
// =====================================================================================
static uint32_t btnEdge = 0;
static bool readBtn() { return digitalRead(PIN_BTN) == LOW || digitalRead(PIN_BOOT) == LOW; }

static void nextMode() {
  switch (mode) {
    case MODE_NET:   mode = MODE_DHT;   break;
    case MODE_DHT:   mode = MODE_RADAR; break;
    case MODE_RADAR: mode = MODE_GRAPH; break;
    default:         mode = MODE_NET;   break;
  }
}

static void handleButton() {                             // actions happen on release (overlay shows which)
  bool d = readBtn();
  uint32_t now = millis();
  if (d && !btnDown && now - btnEdge > 40) { btnDown = true; btnT = now; btnEdge = now; }
  else if (!d && btnDown && now - btnEdge > 40) {
    btnDown = false; btnEdge = now;
    uint32_t held = now - btnT;
    Serial.printf("[btn] released after %lu ms\n", (unsigned long)held);
    if (held >= 10000)      pendingWifi = wifi6 ? 4 : 6;  // switch Wi-Fi 4 <-> Wi-Fi 6
    else if (held >= 5000)  requestAP = true;             // setup Access Point again
    else if (held >= 1000)  startCalibration();           // recalibrate radar
    else                    nextMode();                   // next screen
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
  bool ok = true;
  if (netMode == NET_AP) startAP();
  else if (savedSsid.length()) ok = startSTA(savedSsid, savedPass);
  if (!ok) startAP();                                    // could not rejoin: open the setup AP
  mode = MODE_NET;
}

void setup() {
  s_serMux = xSemaphoreCreateMutex();
  Serial.setTxBufferSize(8192);
  Serial.begin(921600);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  climateStart();

  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  tft.initR(TFT_TAB);
  tft.setSPISpeed(27000000);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);
  splash("Starting...");
  Serial.println("\n=== " PROJECT_NAME " ===");

  WiFi.onEvent(onWiFiEvent);
  loadCreds();
  loadApPass();
  loadWifiMode();
  bool ok = false;
  if (savedSsid.length()) {
    splash("Connecting to", savedSsid.substring(0, 20).c_str());
    ok = startSTA(savedSsid, savedPass);
    if (!ok) Serial.println("[net] could not join the saved Wi-Fi, opening setup AP");
  }
  if (!ok) startAP();
  startServer();
  mode = MODE_NET;
  randomSeed(micros());
}

void loop() {
  static uint32_t loopStart = 0, loopMax = 0, loopCnt = 0;
  { uint32_t t = millis(); if (loopStart) { uint32_t dt = t - loopStart; if (dt > loopMax) loopMax = dt; } loopStart = t; loopCnt++; }
  while (Serial.available()) {                          // 'C' = raw CSI on, 'c' = off
    int ch = Serial.read();
    if (ch == 'C') { if ((int32_t)(s_rawUntil - millis()) <= 0) s_rawSeq = 0; s_rawUntil = millis() + 3000; }
    else if (ch == 'c') s_rawUntil = 0;
    else if (ch == 'S') s_mirrorUntil = millis() + 3000;  // screen mirror keep-alive
    else if (ch == 'N') nextMode();                       // virtual button: next screen
    else if (ch == 'R') startCalibration();
    else if (ch == '4') pendingWifi = 4;                 // laptop: Wi-Fi 4 mode
    else if (ch == '6') pendingWifi = 6;                 // laptop: Wi-Fi 6 mode
  }
  server.handleClient();
  if (netMode == NET_AP) dns.processNextRequest();
  handleButton();
  uint32_t now = millis();

  if (pendingWifi) { int w = pendingWifi; pendingWifi = 0; applyWifiMode(w); }
  if (requestAP) {                                       // 5 s hold: recreate the Access Point
    requestAP = false;
    splash("Starting setup", "Access Point...");
    startAP();
    mode = MODE_NET;
  }
  if (pendingStation) {                                  // a phone joined our AP: use it as CSI source
    pendingStation = false;
    setCsiSource(pendingMac);
    pingStart(pendingIp);
    Serial.printf("[net] phone joined, CSI source %02x:%02x:%02x:%02x:%02x:%02x\n",
                  pendingMac[0], pendingMac[1], pendingMac[2], pendingMac[3], pendingMac[4], pendingMac[5]);
  }

  updateMotionState(now);


  // radar animates (40 ms); graph scrolls every 260 ms; the other screens change about once a second.
  // A held button needs a quick refresh for its progress bar; a screen change redraws at once.
  static uint32_t lastDraw = 0; static int drawnMode = -1;
  uint32_t drawEvery = btnDown ? 60u : mode == MODE_RADAR ? 40u : mode == MODE_GRAPH ? 130u : 500u;
  if ((int)mode != drawnMode || now - lastDraw >= drawEvery) {
    drawnMode = (int)mode; lastDraw = now;
    switch (mode) {
      case MODE_NET:   drawNet(); break;
      case MODE_DHT:   drawDht(); break;
      case MODE_RADAR: sweep += 7.0f; if (sweep >= 360) sweep -= 360; drawRadar(); break;
      case MODE_GRAPH: drawGraph(); break;
    }
  }

  // Machine-readable state for the PC viewer (scripts/radar.sh), 10 lines per second
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
    int ll = snprintf(lg, sizeof(lg), "[%s] %s  ratio=%.2f pps=%lu data=%d%s  loops/2s=%lu maxloop=%lums\n", netMode == NET_AP ? "AP" : "STA", netIp.c_str(),
                      motionRatio(), (unsigned long)pps, haveData(), detected ? "  MOTION" : "", (unsigned long)loopCnt, (unsigned long)loopMax);
    if (serLock()) { Serial.write((const uint8_t *)lg, ll); serUnlock(); }
    loopCnt = 0; loopMax = 0;
  }
  delay(2);
}
