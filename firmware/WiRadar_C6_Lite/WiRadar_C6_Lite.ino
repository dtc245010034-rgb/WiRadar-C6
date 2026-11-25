// WiRadar-C6 Lite  -  Wi-Fi CSI motion detection on ESP32-C6, hardware only (no web, no setup AP)
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

// ---------------- Radar tuning ----------------
#define SEND_FREQUENCY   100     // pings per second toward the CSI source
#define CAL_SECONDS      5
#define DETECT_RATIO     1.8f    // detected when motion > floor * this
#define RELEASE_RATIO    1.45f   // released below this (hysteresis)
#define FULL_RATIO       4.0f    // level bar is full at floor * this
#define HOLD_MS          800     // keep "detected" this long after the last peak
#define RAW_EVERY        16      // raw CSI stream (run_viewer.sh): 1 packet out of N (~6/s at 100 pkt/s)

#define W 128
#define H 128

Adafruit_ST7735 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 canvas(W, H);
DHT dht(PIN_DHT, DHT11);

// =====================================================================================
//  CSI -> motion level
// =====================================================================================
#define NSUB 256                      // HE20 CSI has up to 245 sub-carriers; HT20 has fewer
struct BaseSlot { int n; bool init; float b[NSUB]; };
static BaseSlot s_slots[3];           // one baseline per CSI length (legacy / HT / HE frames differ)
static float s_a[NSUB];
static volatile bool s_baseInit = false;
static volatile uint32_t s_fmtHE = 0, s_fmtHT = 0, s_fmtLEG = 0;   // frames by 802.11 format
static volatile int s_csiLen = 0;
static volatile float s_motion = 0;
static volatile uint32_t s_pkts = 0;
static volatile uint32_t s_lastPktMs = 0;
static volatile int s_rssi = 0;
static uint8_t s_csiMac[6] = {0};
static volatile bool s_csiMacSet = false;

// Serial is written from two tasks (Wi-Fi task for raw CSI, main loop for STATE/log): one lock keeps lines whole.
static SemaphoreHandle_t s_serMux = NULL;
static inline bool serLock()   { return s_serMux && xSemaphoreTake(s_serMux, pdMS_TO_TICKS(30)) == pdTRUE; }
static inline void serUnlock() { if (s_serMux) xSemaphoreGive(s_serMux); }

static volatile uint32_t s_rawUntil = 0;     // raw stream is on while 'C' keep-alives arrive (3 s)
static uint32_t s_rawSeq = 0;
static char s_rawLine[6000];

static void printRawCsi(wifi_csi_info_t *info) {   // same format as Espressif's esp-csi examples
  const wifi_pkt_rx_ctrl_t *rx = &info->rx_ctrl;
  int n = snprintf(s_rawLine, sizeof(s_rawLine),
      "CSI_DATA,%lu,%02x:%02x:%02x:%02x:%02x:%02x,%d,%d,%d,%d,%d,%d,%lu,%d,%d,%d,%d,\"[%d",
      (unsigned long)s_rawSeq++, info->mac[0], info->mac[1], info->mac[2], info->mac[3], info->mac[4], info->mac[5],
      rx->rssi, rx->rate, rx->noise_floor, 0, 0, rx->channel, (unsigned long)rx->timestamp, rx->sig_len,
      rx->cur_bb_format, info->len, info->first_word_invalid, (int)info->buf[0]);
  for (int i = 1; i < info->len && n < (int)sizeof(s_rawLine) - 8; i++)
    n += snprintf(s_rawLine + n, sizeof(s_rawLine) - n, ",%d", (int)info->buf[i]);
  n += snprintf(s_rawLine + n, sizeof(s_rawLine) - n, "]\"\n");
  if (!serLock()) return;
  if (s_rawSeq == 1)                                   // header once, right before the first row
    Serial.print("type,seq,mac,rssi,rate,noise_floor,fft_gain,agc_gain,channel,local_timestamp,sig_len,rx_format,len,first_word,data\n");
  Serial.write((const uint8_t *)s_rawLine, n);
  serUnlock();
}

static void csi_cb(void *ctx, wifi_csi_info_t *info) {
  if (!info || !info->buf || !s_csiMacSet || memcmp(info->mac, s_csiMac, 6)) return;
  static uint32_t rawTick = 0;                         // raw stream: every RAW_EVERY-th packet; the old Python tool only keeps up with a few rows/s
  if ((int32_t)(s_rawUntil - millis()) > 0 && (rawTick++ % RAW_EVERY) == 0) printRawCsi(info);
  int fmt = info->rx_ctrl.cur_bb_format;               // 0/1 legacy, 2 HT (Wi-Fi 4), 4..7 HE (Wi-Fi 6)
  if (fmt >= 4 && fmt <= 7) s_fmtHE++; else if (fmt == 2) s_fmtHT++; else s_fmtLEG++;
  s_csiLen = info->len;

  int n = info->len / 2;
  if (n > NSUB) n = NSUB;
  if (n < 16) return;

  float sum = 0;
  for (int i = 0; i < n; i++) {
    int re = info->buf[2 * i + 1], im = info->buf[2 * i];
    s_a[i] = sqrtf((float)(re * re + im * im));
    sum += s_a[i];
  }
  if (sum <= 0) return;
  float mean = sum / n;
  for (int i = 0; i < n; i++) s_a[i] /= mean;        // removes AGC gain changes

  s_rssi = info->rx_ctrl.rssi;
  s_lastPktMs = millis();
  s_pkts++;

  BaseSlot *sl = NULL;                                // baseline for this CSI length
  for (auto &x : s_slots) if (x.n == n) { sl = &x; break; }
  if (!sl) {
    for (auto &x : s_slots) if (x.n == 0) { sl = &x; break; }
    if (!sl) sl = &s_slots[0];
    sl->n = n; sl->init = false;
  }
  if (!sl->init) {
    for (int i = 0; i < n; i++) sl->b[i] = s_a[i];
    sl->init = true;
    s_baseInit = true;
    return;
  }
  float d = 0; int used = 0;
  for (int i = 0; i < n; i++) {
    if (s_a[i] < 0.05f && sl->b[i] < 0.05f) continue;
    d += fabsf(s_a[i] - sl->b[i]);
    used++;
  }
  if (!used) return;
  d /= used;
  for (int i = 0; i < n; i++) sl->b[i] += 0.003f * (s_a[i] - sl->b[i]);
  s_motion = 0.95f * s_motion + 0.05f * d;
}

static void csiEnable() {
  wifi_csi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.enable = true;
  cfg.acquire_csi_legacy = true;
  cfg.acquire_csi_ht20 = true;
  cfg.acquire_csi_ht40 = true;
  cfg.acquire_csi_su = true;                 // HE-LTF of HE SU frames (Wi-Fi 6)
  cfg.acquire_csi_he_stbc = 2;
  esp_wifi_set_csi_config(&cfg);
  esp_wifi_set_csi_rx_cb(csi_cb, NULL);
  esp_wifi_set_csi(true);
}

// ---- ping: makes the CSI source (router, or the phone in AP mode) transmit ----
static esp_ping_handle_t s_ping = NULL;
static void pingStop() {
  if (s_ping) { esp_ping_stop(s_ping); esp_ping_delete_session(s_ping); s_ping = NULL; }
}
static void pingStart(uint32_t ip4) {                 // ip4 in network byte order
  pingStop();
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = 0;
  cfg.interval_ms = 1000 / SEND_FREQUENCY;
  cfg.task_stack_size = 3072;
  cfg.data_size = 1;
  cfg.target_addr.u_addr.ip4.addr = ip4;
  cfg.target_addr.type = ESP_IPADDR_TYPE_V4;
  esp_ping_callbacks_t cbs = {};
  esp_ping_new_session(&cfg, &cbs, &s_ping);
  esp_ping_start(s_ping);
}

// ---- calibration + detection state ----
static float floorMotion = 0.02f;
static bool calibrating = true;
static uint32_t calStart = 0;
static float calSum = 0; static int calN = 0;
static bool detected = false;
static uint32_t detectUntil = 0;

#define HIST_N   116                                   // 116 x 260 ms = ~30 s
#define HIST_MS  260
static float histo[HIST_N];
static int histN = 0;
static uint32_t lastHist = 0, lastPps = 0, lastPktsCnt = 0, pps = 0;
static uint32_t lastHE = 0, lastHT = 0, lastLEG = 0, fmtHEs = 0, fmtHTs = 0, fmtLEGs = 0;   // frames per second by format

static bool haveData() { return s_pkts > 0 && (millis() - s_lastPktMs) < 1500; }
static float motionRatio() { return calibrating ? 1.0f : (float)s_motion / floorMotion; }
static float motionLevel() {
  if (calibrating) return 0;
  return constrain(((float)s_motion - floorMotion) / (floorMotion * (FULL_RATIO - 1.0f)), 0.0f, 1.0f);
}
static int calLeft() {
  int l = CAL_SECONDS - (int)((millis() - calStart) / 1000);
  return l < 0 ? 0 : l;
}

static void startCalibration() {
  calibrating = true; calStart = millis(); calSum = 0; calN = 0;
  s_baseInit = false; histN = 0; detected = false;
  for (auto &x : s_slots) { x.n = 0; x.init = false; }
}

static void updateMotionState(uint32_t now) {
  bool data = haveData();
  if (calibrating) {
    if (!data || !s_baseInit) { calStart = now; calSum = 0; calN = 0; }   // clock starts with the data
    else {
      calSum += s_motion; calN++;
      if (now - calStart > CAL_SECONDS * 1000UL) {
        if (calN > 0) floorMotion = max(calSum / calN, 0.004f);
        calibrating = false;
      }
    }
  } else if (!data) {
    detected = false;
  } else {
    float m = s_motion;
    if (m > floorMotion * DETECT_RATIO) { detected = true; detectUntil = now + HOLD_MS; }
    else if (detected && m < floorMotion * RELEASE_RATIO && now > detectUntil) detected = false;
  }
  if (now - lastHist >= HIST_MS) {
    lastHist = now;
    if (histN < HIST_N) histo[histN++] = motionRatio();
    else { memmove(histo, histo + 1, (HIST_N - 1) * sizeof(float)); histo[HIST_N - 1] = motionRatio(); }
  }
  if (now - lastPps >= 1000) {
    lastPps = now; pps = s_pkts - lastPktsCnt; lastPktsCnt = s_pkts;
    fmtHEs = s_fmtHE - lastHE; fmtHTs = s_fmtHT - lastHT; fmtLEGs = s_fmtLEG - lastLEG;
    lastHE = s_fmtHE; lastHT = s_fmtHT; lastLEG = s_fmtLEG;
  }
}

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

// =====================================================================================
//  Wi-Fi (station only)
// =====================================================================================
static float temp = NAN, hum = NAN;
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
  Serial.printf("[net] connected to %s, IP %s\n", ssid.c_str(), netIp.c_str());
  return true;
}

// =====================================================================================
//  TFT screens
// =====================================================================================
enum Mode { MODE_NET, MODE_DHT, MODE_RADAR, MODE_GRAPH };
static Mode mode = MODE_DHT;
static float sweep = 0;
struct Ripple { uint32_t t; bool on; };
static Ripple ripples[5];
static uint32_t lastRipple = 0, lastDht = 0;

#define C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t COL_BG = C(0, 0, 0), COL_GRID = C(0, 70, 30), COL_GREEN = C(0, 255, 90),
                      COL_RED = C(255, 50, 40), COL_YEL = C(255, 200, 0), COL_WHITE = C(255, 255, 255),
                      COL_GRAY = C(110, 110, 110), COL_BLUE = C(60, 160, 255), COL_ORG = C(255, 140, 30);

// button state (used by the overlay)
static bool btnDown = false; static uint32_t btnT = 0;

static void push() {
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

static void header(const char *title) {
  canvas.fillScreen(COL_BG);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.setTextColor(COL_GRAY);
  canvas.setCursor(4, 3); canvas.print(title);
  canvas.drawFastHLine(0, 13, W, COL_GRID);
}

static void fitPrint(int x, int y, uint16_t col, String s, int maxChars) {
  if ((int)s.length() > maxChars) s = s.substring(0, maxChars - 1) + "~";
  canvas.setTextColor(col); canvas.setCursor(x, y); canvas.print(s);
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

static void drawDht() {
  header("TEMP / HUMIDITY");
  canvas.setTextColor(COL_ORG); canvas.setCursor(8, 19); canvas.print("TEMPERATURE");
  canvas.setTextSize(3); canvas.setCursor(8, 29);
  if (isnan(temp)) canvas.print("--"); else canvas.printf("%.0f", temp);
  canvas.setTextSize(2); canvas.print(" C");
  canvas.drawRoundRect(8, 57, 112, 7, 3, COL_GRAY);
  if (!isnan(temp)) {
    int w = constrain((int)(temp / 50.0f * 108), 0, 108);
    if (w > 0) canvas.fillRoundRect(10, 59, w, 3, 1, COL_ORG);
  }
  canvas.setTextSize(1); canvas.setTextColor(COL_BLUE); canvas.setCursor(8, 70); canvas.print("HUMIDITY");
  canvas.setTextSize(3); canvas.setCursor(8, 80);
  if (isnan(hum)) canvas.print("--"); else canvas.printf("%.0f", hum);
  canvas.setTextSize(2); canvas.print(" %");
  canvas.drawRoundRect(8, 107, 112, 7, 3, COL_GRAY);
  if (!isnan(hum)) {
    int w = constrain((int)(hum / 100.0f * 108), 0, 108);
    if (w > 0) canvas.fillRoundRect(10, 109, w, 3, 1, COL_BLUE);
  }
  canvas.setTextSize(1);
  fitPrint(4, 119, COL_GRAY, isnan(temp) ? "No sensor on GPIO1" : "Press: next screen", 21);
  push();
}

static void drawRadar() {
  const int cx = 64, cy = 58, R = 42;
  bool data = haveData();
  bool det = detected && !calibrating && data;
  float level = motionLevel();
  uint16_t ring = det ? COL_RED : COL_GRID;

  header("WI-FI RADAR");
  canvas.setTextColor(COL_GREEN); canvas.setCursor(4, 3); canvas.print("WI-FI RADAR");
  canvas.setTextColor(linkStandard() == 4 ? COL_GREEN : COL_YEL); canvas.setCursor(74, 3); canvas.print(stdTag(linkStandard()));
  canvas.setTextColor(COL_GRAY); canvas.setCursor(W - 40, 3); canvas.printf("%lu/s", (unsigned long)pps);

  canvas.drawCircle(cx, cy, R, ring);
  canvas.drawCircle(cx, cy, R * 2 / 3, COL_GRID);
  canvas.drawCircle(cx, cy, R / 3, COL_GRID);
  canvas.drawFastHLine(cx - R, cy, 2 * R, COL_GRID);
  canvas.drawFastVLine(cx, cy - R, 2 * R, COL_GRID);
  if (det) canvas.drawCircle(cx, cy, R + 1, ring);

  for (int k = 7; k >= 0; k--) {
    float ang = (sweep - k * 4.0f) * DEG_TO_RAD;
    uint8_t g = 255 - k * 30;
    canvas.drawLine(cx, cy, cx + (int)(R * cosf(ang)), cy + (int)(R * sinf(ang)),
                    det ? C(g, g / 5, g / 6) : C(0, g, g / 3));
  }

  uint32_t now = millis();
  if (calibrating || !data) {
    canvas.fillRect(8, cy - 18, 112, 36, COL_BG);
    canvas.drawRect(8, cy - 18, 112, 36, COL_YEL);
    canvas.setTextColor(COL_YEL);
    if (!data) {
      canvas.setCursor(14, cy - 12); canvas.print("NO CSI DATA");
      canvas.setCursor(14, cy - 1);  canvas.print("Waiting for packets");
      canvas.setCursor(14, cy + 8);  canvas.print("from the router");
    } else {
      canvas.setCursor(14, cy - 12); canvas.print("CALIBRATING...");
      canvas.setCursor(14, cy - 1);  canvas.print("Clear the area");
      canvas.setCursor(14, cy + 8);  canvas.printf("Please wait  %ds", calLeft());
    }
  } else {
    uint32_t period = 700 - (uint32_t)(450 * level);
    if (det && now - lastRipple > period) {
      for (auto &r : ripples) if (!r.on) { r.on = true; r.t = now; break; }
      lastRipple = now;
    }
    for (auto &r : ripples) if (r.on) {
      uint32_t age = now - r.t;
      if (age > 1500) { r.on = false; continue; }
      uint8_t f = 255 - age * 255 / 1500;
      canvas.drawCircle(cx, cy, 2 + (R - 2) * age / 1500, C(f, f / 6, 0));
    }
    if (det) canvas.fillCircle(cx, cy, 3 + (int)(level * 9) + (int)((now / 120) % 2), C(255, 90, 60));
  }

  canvas.drawRect(4, 104, W - 8, 6, COL_GRAY);
  int bw = (int)(level * (W - 12));
  if (bw > 0) canvas.fillRect(6, 106, bw, 2, det ? COL_RED : COL_GREEN);
  canvas.setTextSize(1); canvas.setCursor(4, 112);
  if (!data)             { canvas.setTextColor(COL_YEL);   canvas.print("No data"); }
  else if (calibrating)  { canvas.setTextColor(COL_YEL);   canvas.print("Calibrating..."); }
  else if (det)          { canvas.setTextColor(COL_RED);   canvas.print("MOTION DETECTED!"); }
  else                   { canvas.setTextColor(COL_GREEN); canvas.print("Area clear"); }
  canvas.setTextColor(COL_GRAY); canvas.setCursor(4, 120); canvas.print("Hold: recalibrate");
  push();
}

static void drawGraph() {
  bool data = haveData();
  bool det = detected && !calibrating && data;
  header("MOTION GRAPH 30s");
  canvas.setTextColor(linkStandard() == 4 ? COL_GREEN : COL_YEL); canvas.setCursor(104, 3); canvas.print(stdTag(linkStandard()));
  canvas.setTextSize(2); canvas.setCursor(4, 19);
  if (!data)            { canvas.setTextColor(COL_YEL);   canvas.print("NO DATA"); }
  else if (calibrating) { canvas.setTextColor(COL_YEL);   canvas.print("CALIB."); }
  else if (det)         { canvas.setTextColor(COL_RED);   canvas.print("MOTION"); }
  else                  { canvas.setTextColor(COL_GREEN); canvas.print("CLEAR"); }

  const int gx = 4, gy = 38, gw = 120, gh = 64;
  canvas.drawRect(gx, gy, gw, gh, COL_GRID);
  const float ymax = FULL_RATIO + 0.5f;
  auto Y = [&](float r) -> int {
    r = constrain(r, 0.0f, ymax);
    return gy + gh - 2 - (int)((gh - 4) * r / ymax);
  };
  for (int x = gx + 1; x < gx + gw - 1; x += 3) canvas.drawPixel(x, Y(1.0f), COL_GRAY);
  for (int x = gx + 1; x < gx + gw - 1; x += 6) canvas.drawFastHLine(x, Y(DETECT_RATIO), 3, COL_YEL);
  for (int i = 1; i < histN; i++) {
    int x1 = gx + gw - 2 - (histN - 1 - i), x0 = x1 - 1;
    canvas.drawLine(x0, Y(histo[i - 1]), x1, Y(histo[i]), histo[i] > DETECT_RATIO ? COL_RED : COL_GREEN);
  }
  canvas.setTextSize(1); canvas.setTextColor(COL_WHITE);
  canvas.setCursor(4, 106); canvas.printf("Now x%.1f  %lu pkt/s", motionRatio(), (unsigned long)pps);
  canvas.setTextColor(COL_GRAY); canvas.setCursor(4, 118); canvas.print("Hold: recalibrate");
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
static void splash(const char *l1, const char *l2 = "") {
  canvas.fillScreen(COL_BG);
  canvas.setTextWrap(false);
  canvas.setTextColor(COL_GREEN); canvas.setTextSize(2); canvas.setCursor(10, 30); canvas.print("WiRadar");
  canvas.setTextColor(COL_WHITE); canvas.setTextSize(1);
  canvas.setCursor(10, 56); canvas.print(l1);
  canvas.setCursor(10, 68); canvas.print(l2);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
}

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
  tft.setRotation(0);
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
