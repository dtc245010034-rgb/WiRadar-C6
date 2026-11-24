// ESP32-C6 + man hinh TFT ST7735 (1.44" 128x128) + DHT11 + 1 nut bam
//  - Che do 1: Temperature / Humidity (DHT11)
//  - Che do 2: Wi-Fi CSI RADAR - motion detection, radar-style display
//  - Che do 3: MOTION GRAPH - 30 s history of the motion level (like the PC viewer)
// Bam nut ngan: DHT -> RADAR -> GRAPH -> DHT. Giu nut >1s (radar/graph): hieu chinh lai.
// Board: ESP32C6 Dev Module | USB CDC On Boot: Disabled | Flash 16MB
// Day noi: xem WIRING.md

#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <DHT.h>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "secrets.h"

// ---------------- Chan ket noi (doi neu board ban khac) ----------------
#define PIN_SCK   6
#define PIN_MOSI  7
#define PIN_CS    18
#define PIN_DC    19
#define PIN_RST   20
#define PIN_DHT   1
#define PIN_BTN   2     // nut noi giua GPIO2 va GND

// Man 1.44" 128x128 thuong dung INITR_144GREENTAB. Neu lech/sai mau thu INITR_BLACKTAB.
#define TFT_TAB   INITR_144GREENTAB

// ---------------- Tham so radar (chinh de nhay/cham hon) ----------------
#define SEND_FREQUENCY   100     // goi ping moi giay toi router
#define CAL_SECONDS      5       // thoi gian hieu chinh khi vao radar
#define DETECT_RATIO     1.8f    // phat hien khi motion > nen * he so nay
#define FULL_RATIO       4.0f    // motion >= nen * he so nay = vach day

#define W 128
#define H 128

Adafruit_ST7735 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 canvas(W, H);
DHT dht(PIN_DHT, DHT11);

// ---------------- CSI: do muc chuyen dong ----------------
#define NSUB 64
static float s_base[NSUB];
static bool  s_baseInit = false;
static volatile float s_motion = 0;     // muc chuyen dong da lam muot
static volatile uint32_t s_pkts = 0;

static void csi_cb(void *ctx, wifi_csi_info_t *info) {
  if (!info || !info->buf || memcmp(info->mac, ctx, 6)) return;
  int n = info->len / 2;
  if (n > NSUB) n = NSUB;
  if (n < 16) return;

  float a[NSUB];
  float sum = 0;
  for (int i = 0; i < n; i++) {
    int re = info->buf[2 * i + 1], im = info->buf[2 * i];
    a[i] = sqrtf((float)(re * re + im * im));
    sum += a[i];
  }
  if (sum <= 0) return;
  float mean = sum / n;
  for (int i = 0; i < n; i++) a[i] /= mean;      // chuan hoa: bo anh huong tang ich AGC

  if (!s_baseInit) {
    for (int i = 0; i < n; i++) s_base[i] = a[i];
    s_baseInit = true;
    return;
  }
  float d = 0; int used = 0;
  for (int i = 0; i < n; i++) {
    if (a[i] < 0.05f && s_base[i] < 0.05f) continue;   // bo song mang bien/rong
    d += fabsf(a[i] - s_base[i]);
    used++;
  }
  if (!used) return;
  d /= used;
  for (int i = 0; i < n; i++) s_base[i] += 0.003f * (a[i] - s_base[i]);  // nen tu thich nghi cham
  s_motion = 0.95f * s_motion + 0.05f * d;
  s_pkts++;
}

static void csi_init() {
  wifi_csi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.enable = true;
  cfg.acquire_csi_legacy = true;
  cfg.acquire_csi_ht20 = true;
  cfg.acquire_csi_ht40 = true;
  cfg.acquire_csi_he_stbc = 2;
  static wifi_ap_record_t ap;
  esp_wifi_sta_get_ap_info(&ap);
  esp_wifi_set_csi_config(&cfg);
  esp_wifi_set_csi_rx_cb(csi_cb, ap.bssid);
  esp_wifi_set_csi(true);
}

static void ping_router_start() {
  static esp_ping_handle_t handle = NULL;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = 0;
  cfg.interval_ms = 1000 / SEND_FREQUENCY;
  cfg.task_stack_size = 3072;
  cfg.data_size = 1;
  cfg.target_addr.u_addr.ip4.addr = (uint32_t)WiFi.gatewayIP();
  cfg.target_addr.type = ESP_IPADDR_TYPE_V4;
  esp_ping_callbacks_t cbs = {};
  esp_ping_new_session(&cfg, &cbs, &handle);
  esp_ping_start(handle);
}

// ---------------- Trang thai ----------------
enum Mode { MODE_DHT, MODE_RADAR, MODE_GRAPH };
static Mode mode = MODE_DHT;

static float floorMotion = 0.02f;        // muc nhieu nen (do luc hieu chinh)
static bool  calibrating = true;
static uint32_t calStart = 0;
static float calSum = 0; static int calN = 0;

static float temp = NAN, hum = NAN;
static uint32_t lastDht = 0;

static float sweep = 0;                  // goc quet (do)
struct Blip { uint32_t t; bool on; };   // expanding ripple
static Blip blips[5];
static uint32_t lastBlip = 0;
#define HIST_N      116           // 116 samples * 260 ms = ~30 s
#define HIST_MS     260
static float histo[HIST_N];              // motion / floor, newest last
static int histN = 0;
static uint32_t lastHist = 0, lastPps = 0, lastPktsCnt = 0, pps = 0;

#define C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t COL_BG = C(0, 0, 0), COL_GRID = C(0, 70, 30), COL_GREEN = C(0, 255, 90),
                      COL_RED = C(255, 50, 40), COL_YEL = C(255, 200, 0), COL_WHITE = C(255, 255, 255),
                      COL_GRAY = C(110, 110, 110), COL_BLUE = C(60, 160, 255), COL_ORG = C(255, 140, 30);

static void push() { tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H); }

static void startCalibration() {
  calibrating = true; calStart = millis(); calSum = 0; calN = 0;
  s_baseInit = false;
  histN = 0;
}

// ---------------- Temperature / Humidity screen ----------------
static void drawDht() {
  canvas.fillScreen(COL_BG);
  canvas.setTextWrap(false);

  canvas.setTextColor(COL_GRAY); canvas.setTextSize(1);
  canvas.setCursor(4, 3); canvas.print("TEMP / HUMIDITY");
  canvas.drawFastHLine(0, 13, W, COL_GRID);

  // temperature
  canvas.setTextColor(COL_ORG); canvas.setTextSize(1);
  canvas.setCursor(8, 19); canvas.print("TEMPERATURE");
  canvas.setTextSize(3);
  canvas.setCursor(8, 29);
  if (isnan(temp)) canvas.print("--"); else canvas.printf("%.0f", temp);
  canvas.setTextSize(2); canvas.print(" C");
  canvas.drawRoundRect(8, 57, 112, 7, 3, COL_GRAY);          // scale 0-50 C
  if (!isnan(temp)) {
    int w = constrain((int)(temp / 50.0f * 108), 0, 108);
    if (w > 0) canvas.fillRoundRect(10, 59, w, 3, 1, COL_ORG);
  }

  // humidity
  canvas.setTextColor(COL_BLUE); canvas.setTextSize(1);
  canvas.setCursor(8, 70); canvas.print("HUMIDITY");
  canvas.setTextSize(3);
  canvas.setCursor(8, 80);
  if (isnan(hum)) canvas.print("--"); else canvas.printf("%.0f", hum);
  canvas.setTextSize(2); canvas.print(" %");
  canvas.drawRoundRect(8, 107, 112, 7, 3, COL_GRAY);         // scale 0-100 %
  if (!isnan(hum)) {
    int w = constrain((int)(hum / 100.0f * 108), 0, 108);
    if (w > 0) canvas.fillRoundRect(10, 109, w, 3, 1, COL_BLUE);
  }

  canvas.setTextColor(COL_GRAY); canvas.setTextSize(1);
  canvas.setCursor(4, 119); canvas.print("Press: RADAR");
  push();
}

// ---------------- Radar screen ----------------
static void drawRadar() {
  const int cx = 64, cy = 58, R = 42;
  float m = s_motion;
  bool detect = !calibrating && m > floorMotion * DETECT_RATIO;
  float level = calibrating ? 0 :
      constrain((m - floorMotion) / (floorMotion * (FULL_RATIO - 1.0f)), 0.0f, 1.0f);
  uint16_t ring = detect ? COL_RED : COL_GRID;

  canvas.fillScreen(COL_BG);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.setTextColor(COL_GREEN);
  canvas.setCursor(4, 3); canvas.print("WI-FI RADAR");
  canvas.setTextColor(COL_GRAY);
  canvas.setCursor(W - 28, 3); canvas.printf("%lup", (unsigned long)(s_pkts % 1000));
  canvas.drawFastHLine(0, 13, W, COL_GRID);

  // grid
  canvas.drawCircle(cx, cy, R, ring);
  canvas.drawCircle(cx, cy, R * 2 / 3, COL_GRID);
  canvas.drawCircle(cx, cy, R / 3, COL_GRID);
  canvas.drawFastHLine(cx - R, cy, 2 * R, COL_GRID);
  canvas.drawFastVLine(cx, cy - R, 2 * R, COL_GRID);
  if (detect) canvas.drawCircle(cx, cy, R + 1, ring);

  // sweep beam + fading trail
  for (int k = 7; k >= 0; k--) {
    float ang = (sweep - k * 4.0f) * DEG_TO_RAD;
    uint8_t g = 255 - k * 30;
    canvas.drawLine(cx, cy, cx + (int)(R * cosf(ang)), cy + (int)(R * sinf(ang)),
                    detect ? C(g, g / 5, g / 6) : C(0, g, g / 3));
  }

  if (calibrating) {
    uint32_t el = millis() - calStart;
    int left = CAL_SECONDS - (int)(el / 1000);
    canvas.fillRect(8, cy - 18, 112, 36, COL_BG);
    canvas.drawRect(8, cy - 18, 112, 36, COL_YEL);
    canvas.setTextColor(COL_YEL);
    canvas.setCursor(14, cy - 12); canvas.print("CALIBRATING...");
    canvas.setCursor(14, cy - 1);  canvas.print("Clear the area");
    canvas.setCursor(14, cy + 8);  canvas.printf("Please wait  %ds", left > 0 ? left : 0);
  } else {
    // expanding ripples from the center while motion is detected (strength only, no position)
    uint32_t now = millis();
    uint32_t period = 700 - (uint32_t)(450 * level);
    if (detect && now - lastBlip > period) {
      for (auto &b : blips) if (!b.on) { b.on = true; b.t = now; break; }
      lastBlip = now;
    }
    for (auto &b : blips) if (b.on) {
      uint32_t age = now - b.t;
      if (age > 1500) { b.on = false; continue; }
      uint8_t f = 255 - age * 255 / 1500;
      canvas.drawCircle(cx, cy, 2 + (R - 2) * age / 1500, C(f, f / 6, 0));
    }
    if (detect) {
      int cr = 3 + (int)(level * 9) + (int)((now / 120) % 2);   // pulsing core
      canvas.fillCircle(cx, cy, cr, C(255, 90, 60));
    }
  }

  // motion level bar + status
  canvas.drawRect(4, 104, W - 8, 6, COL_GRAY);
  int bw = (int)(level * (W - 12));
  if (bw > 0) canvas.fillRect(6, 106, bw, 2, detect ? COL_RED : COL_GREEN);

  canvas.setTextSize(1);
  canvas.setCursor(4, 112);
  if (calibrating)  { canvas.setTextColor(COL_YEL);   canvas.print("Calibrating..."); }
  else if (detect)  { canvas.setTextColor(COL_RED);   canvas.print("MOTION DETECTED!"); }
  else              { canvas.setTextColor(COL_GREEN); canvas.print("Area clear"); }
  canvas.setTextColor(COL_GRAY);
  canvas.setCursor(4, 120); canvas.print("Hold: recalibrate");
  push();
}

// ---------------- Shared motion state (radar + graph) ----------------
static float motionRatio() { return calibrating ? 1.0f : (float)s_motion / floorMotion; }

static void updateMotionState(uint32_t now) {
  if (calibrating) {
    calSum += s_motion; calN++;
    if (now - calStart > CAL_SECONDS * 1000UL) {
      if (calN > 0 && calSum > 0) floorMotion = max(calSum / calN, 0.004f);
      calibrating = false;
    }
  }
  if (now - lastHist >= HIST_MS) {                 // one history sample every 260 ms
    lastHist = now;
    if (histN < HIST_N) histo[histN++] = motionRatio();
    else { memmove(histo, histo + 1, (HIST_N - 1) * sizeof(float)); histo[HIST_N - 1] = motionRatio(); }
  }
  if (now - lastPps >= 1000) {                     // packets per second
    lastPps = now; pps = s_pkts - lastPktsCnt; lastPktsCnt = s_pkts;
  }
}

// ---------------- Motion graph screen ----------------
static void drawGraph() {
  bool detect = !calibrating && (float)s_motion > floorMotion * DETECT_RATIO;
  canvas.fillScreen(COL_BG);
  canvas.setTextWrap(false);
  canvas.setTextColor(COL_GRAY); canvas.setTextSize(1);
  canvas.setCursor(4, 3); canvas.print("MOTION GRAPH 30s");
  canvas.drawFastHLine(0, 13, W, COL_GRID);

  canvas.setTextSize(2);
  canvas.setCursor(4, 19);
  if (calibrating)  { canvas.setTextColor(COL_YEL);   canvas.print("CALIB."); }
  else if (detect)  { canvas.setTextColor(COL_RED);   canvas.print("MOTION"); }
  else              { canvas.setTextColor(COL_GREEN); canvas.print("CLEAR"); }

  const int gx = 4, gy = 38, gw = 120, gh = 64;
  canvas.drawRect(gx, gy, gw, gh, COL_GRID);
  const float ymax = FULL_RATIO + 0.5f;
  auto Y = [&](float r) -> int {
    r = constrain(r, 0.0f, ymax);
    return gy + gh - 2 - (int)((gh - 4) * r / ymax);
  };
  for (int x = gx + 1; x < gx + gw - 1; x += 3) canvas.drawPixel(x, Y(1.0f), COL_GRAY);          // baseline
  for (int x = gx + 1; x < gx + gw - 1; x += 6) canvas.drawFastHLine(x, Y(DETECT_RATIO), 3, COL_YEL);  // threshold

  for (int i = 1; i < histN; i++) {                // newest sample at the right edge
    int x1 = gx + gw - 2 - (histN - 1 - i), x0 = x1 - 1;
    canvas.drawLine(x0, Y(histo[i - 1]), x1, Y(histo[i]), histo[i] > DETECT_RATIO ? COL_RED : COL_GREEN);
  }

  canvas.setTextSize(1);
  canvas.setTextColor(COL_WHITE);
  canvas.setCursor(4, 106); canvas.printf("Now x%.1f  %lu pkt/s", motionRatio(), (unsigned long)pps);
  canvas.setTextColor(COL_GRAY);
  canvas.setCursor(4, 118); canvas.print("Hold: recalibrate");
  push();
}

// ---------------- Nut bam ----------------
static bool btnDown = false; static uint32_t btnT = 0; static bool longDone = false;
static void handleButton() {
  bool d = digitalRead(PIN_BTN) == LOW;
  uint32_t now = millis();
  if (d && !btnDown && now - btnT > 40) { btnDown = true; btnT = now; longDone = false; }
  else if (d && btnDown && !longDone && now - btnT > 1000) {
    longDone = true;
    if (mode != MODE_DHT) startCalibration();
  } else if (!d && btnDown && now - btnT > 40) {
    btnDown = false;
    if (!longDone) {
      if (mode == MODE_DHT)        { mode = MODE_RADAR; startCalibration(); }
      else if (mode == MODE_RADAR)   mode = MODE_GRAPH;
      else                           mode = MODE_DHT;
    }
    btnT = now;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  dht.begin();

  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  tft.initR(TFT_TAB);
  tft.setSPISpeed(27000000);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  canvas.fillScreen(COL_BG);
  canvas.setTextColor(COL_WHITE); canvas.setTextSize(1);
  canvas.setCursor(10, 50); canvas.print("Connecting Wi-Fi");
  canvas.setCursor(10, 64); canvas.print(WIFI_SSID);
  push();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(300);

  if (WiFi.status() == WL_CONNECTED) {
    csi_init();
    ping_router_start();
  } else {
    canvas.fillScreen(COL_BG);
    canvas.setTextColor(COL_RED);
    canvas.setCursor(10, 50); canvas.print("Wi-Fi failed");
    canvas.setCursor(10, 64); canvas.print("Radar disabled");
    push(); delay(2500);
  }
  randomSeed(micros());
}

void loop() {
  handleButton();
  uint32_t now = millis();

  if (mode == MODE_DHT) {
    if (now - lastDht > 2000) {
      lastDht = now;
      float t = dht.readTemperature(), h = dht.readHumidity();
      if (!isnan(t)) temp = t;
      if (!isnan(h)) hum = h;
    }
    drawDht();
    delay(100);
  } else {
    updateMotionState(now);
    if (mode == MODE_RADAR) {
      sweep += 7.0f; if (sweep >= 360) sweep -= 360;
      drawRadar();
      delay(20);
    } else {
      drawGraph();
      delay(40);
    }
    static uint32_t lastLog = 0;
    if (now - lastLog > 1000) {
      lastLog = now;
      Serial.printf("motion=%.4f floor=%.4f pkts=%lu\n", (float)s_motion, floorMotion, (unsigned long)s_pkts);
    }
  }
}
