// TFT screens (radar, graph, climate), screen mirror, splash. Shared by both firmwares.
// Define WIRADAR_FULL before including for the full firmware (setup-AP hints, NET screen first).
// Needs config.h, csi_core.h and wifi_mode.h first. drawNet() lives in each sketch (it differs).
#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "config.h"

Adafruit_ST7735 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 canvas(W, H);
static float temp = NAN, hum = NAN;                      // DHT11 values, written by the sketch

enum Mode { MODE_NET, MODE_DHT, MODE_RADAR, MODE_GRAPH };
#ifdef WIRADAR_FULL
static Mode mode = MODE_NET;
#else
static Mode mode = MODE_DHT;
#endif
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

// ---- screen mirror for the PC (run3_viewer.sh): sends the same 128x128 frame the TFT gets ----
static uint32_t s_mirrorUntil = 0;                       // on while 'S' keep-alives arrive (3 s)
static void mirrorFrame(bool force = false) {
  static uint32_t last = 0;
  static uint8_t rle[12000];                             // runs of (count, colour hi, colour lo)
  static char b64[16100];
  uint32_t now = millis();
  if ((int32_t)(s_mirrorUntil - now) <= 0 || (!force && now - last < 120)) return;
  last = now;
  const uint16_t *px = canvas.getBuffer();
  size_t n = 0;
  for (int i = 0; i < W * H;) {
    uint16_t c = px[i]; int run = 1;
    while (run < 255 && i + run < W * H && px[i + run] == c) run++;
    if (n + 3 > sizeof(rle)) return;                     // picture too busy: skip this frame
    rle[n++] = run; rle[n++] = c >> 8; rle[n++] = c & 0xFF;
    i += run;
  }
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  memcpy(b64, "FRAME,", 6); o = 6;
  for (size_t i = 0; i < n; i += 3) {                    // length is a multiple of 3: no padding
    uint32_t v = ((uint32_t)rle[i] << 16) | ((uint32_t)rle[i + 1] << 8) | rle[i + 2];
    b64[o++] = T[v >> 18]; b64[o++] = T[(v >> 12) & 63]; b64[o++] = T[(v >> 6) & 63]; b64[o++] = T[v & 63];
  }
  b64[o++] = '\n';
  if (serLock()) { Serial.write((const uint8_t *)b64, o); serUnlock(); }
}

static void push() {
  if (btnDown && millis() - btnT > 1000) {               // held: show what releasing now will do
    uint32_t held = millis() - btnT;
#ifdef WIRADAR_FULL
    const char *act = held >= 10000 ? (wifi6 ? "Release: Wi-Fi 4" : "Release: Wi-Fi 6")
                    : held >= 5000  ? "Release: setup AP" : "Release: recalibrate";
    const char *hint = "5s:AP 10s:W4/6";
#else
    const char *act = held >= 10000 ? (wifi6 ? "Release: Wi-Fi 4" : "Release: Wi-Fi 6") : "Release: recalibrate";
    const char *hint = "10s: Wi-Fi 4/6";
#endif
    canvas.fillRect(4, 90, 120, 36, COL_BG);
    canvas.drawRect(4, 90, 120, 36, COL_YEL);
    canvas.setTextSize(1);
    canvas.setTextColor(COL_YEL);  canvas.setCursor(9, 95);  canvas.print(act);
    canvas.setTextColor(COL_GRAY); canvas.setCursor(9, 105); canvas.print(hint);
    canvas.drawRect(9, 116, 110, 5, COL_GRAY);
    canvas.fillRect(10, 117, (int)(108UL * min<uint32_t>(held, 10000) / 10000), 3, held >= 10000 ? COL_GREEN : COL_YEL);
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
  mirrorFrame();
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
      canvas.setCursor(14, cy - 1);  canvas.print(netMode == NET_AP ? "Join the setup Wi-Fi" : "Waiting for packets");
      canvas.setCursor(14, cy + 8);  canvas.print(netMode == NET_AP ? "with your phone" : "from the router");
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

static void splash(const char *l1, const char *l2 = "") {
  canvas.fillScreen(COL_BG);
  canvas.setTextWrap(false);
  canvas.setTextColor(COL_GREEN); canvas.setTextSize(2); canvas.setCursor(10, 30); canvas.print("WiRadar");
  canvas.setTextColor(COL_WHITE); canvas.setTextSize(1);
  canvas.setCursor(10, 56); canvas.print(l1);
  canvas.setCursor(10, 68); canvas.print(l2);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
  mirrorFrame(true);
}
