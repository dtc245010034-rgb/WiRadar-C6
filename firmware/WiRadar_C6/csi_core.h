// CSI capture -> motion level, ping source, calibration and detection. Shared by both firmwares.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "config.h"
#include "csi_math.h"

// =====================================================================================
//  CSI -> motion level
// =====================================================================================
#define NSUB CSI_NSUB                 // HE20 CSI has up to 245 sub-carriers; HT20 has fewer
static CsiBaseline s_slots[3];        // one baseline per CSI length (legacy / HT / HE frames differ); maths in csi_math.h
static int32_t s_a[NSUB];             // normalised amplitudes of the current packet (Q16)
static volatile bool s_baseInit = false;
static volatile bool s_resetReq = false;   // set by startCalibration() (other task); csi_cb clears the baselines itself
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
static const size_t RAW_LINE_MAX = 6000;
static char *s_rawLine = NULL;                    // allocated on the first raw packet (only when scripts/raw.sh asks for it)

static void printRawCsi(wifi_csi_info_t *info) {   // same format as Espressif's esp-csi examples
  const wifi_pkt_rx_ctrl_t *rx = &info->rx_ctrl;
  if (!s_rawLine) { s_rawLine = (char *)malloc(RAW_LINE_MAX); if (!s_rawLine) return; }
  int n = snprintf(s_rawLine, RAW_LINE_MAX,
      "CSI_DATA,%lu,%02x:%02x:%02x:%02x:%02x:%02x,%d,%d,%d,%d,%d,%d,%lu,%d,%d,%d,%d,\"[%d",
      (unsigned long)s_rawSeq++, info->mac[0], info->mac[1], info->mac[2], info->mac[3], info->mac[4], info->mac[5],
      rx->rssi, rx->rate, rx->noise_floor, 0, 0, rx->channel, (unsigned long)rx->timestamp, rx->sig_len,
      rx->cur_bb_format, info->len, info->first_word_invalid, (int)info->buf[0]);
  for (int i = 1; i < info->len && n < (int)RAW_LINE_MAX - 8; i++)
    n += snprintf(s_rawLine + n, RAW_LINE_MAX - n, ",%d", (int)info->buf[i]);
  n += snprintf(s_rawLine + n, RAW_LINE_MAX - n, "]\"\n");
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

  if (s_resetReq) {                                    // calibration restarted: only this task touches the baselines
    s_resetReq = false;
    for (auto &x : s_slots) csi_baseline_reset(&x, 0);
  }
  int n = info->len / 2;
  if (n > NSUB) n = NSUB;
  if (n < 16) return;
  if (!csi_normalize(info->buf, n, s_a)) return;       // amplitude / mean: removes AGC gain changes

  s_rssi = info->rx_ctrl.rssi;
  s_lastPktMs = millis();
  s_pkts++;

  CsiBaseline *sl = NULL;                              // baseline for this CSI length
  for (auto &x : s_slots) if (x.n == n) { sl = &x; break; }
  if (!sl) {
    for (auto &x : s_slots) if (x.n == 0) { sl = &x; break; }
    if (!sl) sl = &s_slots[0];
    csi_baseline_reset(sl, n);
  }
  bool wasInit = sl->init;
  float d = csi_baseline_update(sl, s_a, n);
  if (!wasInit) { s_baseInit = true; return; }
  if (d < 0) return;
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
#define CAL_SAMPLES 128                                // one sample every 50 ms during calibration
static float calBuf[CAL_SAMPLES]; static int calN = 0; static uint32_t calLastSample = 0;
static float calFloor = 0.02f;                         // floor found at calibration (floorMotion may drift within 0.5x..2x of it)
static uint32_t quietSince = 0, lastAdapt = 0;
static bool detected = false;
static uint32_t detectUntil = 0;

#define HIST_N   116                                   // 116 x 260 ms = ~30 s
#define HIST_MS  260
static float histo[HIST_N];                            // ring buffer: oldest sample at histHead
static int histN = 0, histHead = 0;
static inline float histAt(int i) { return histo[(histHead + i) % HIST_N]; }   // i = 0 is the oldest
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
  calibrating = true; calStart = millis(); calN = 0; calLastSample = 0;
  s_baseInit = false; histN = 0; histHead = 0; detected = false; quietSince = 0;
  s_resetReq = true;                                   // the Wi-Fi task clears the baselines at its next packet
}

static void updateMotionState(uint32_t now) {
  bool data = haveData();
  if (calibrating) {
    if (!data || !s_baseInit) { calStart = now; calN = 0; }   // clock starts with the data
    else {
      if (calN == 0 || now - calLastSample >= 50) {
        calLastSample = now;
        if (calN < CAL_SAMPLES) calBuf[calN++] = s_motion;
      }
      if (now - calStart > CAL_SECONDS * 1000UL) {
        if (calN > 0) floorMotion = max(csi_median(calBuf, calN), 0.004f);   // median: someone walking by does not inflate it
        calFloor = floorMotion; quietSince = 0; lastAdapt = now;
        calibrating = false;
      }
    }
  } else if (!data) {
    detected = false;
  } else {
    float m = s_motion;
    if (m > floorMotion * DETECT_RATIO) { detected = true; detectUntil = now + HOLD_MS; }
    else if (detected && m < floorMotion * RELEASE_RATIO && now > detectUntil) detected = false;
    // after 10 s of quiet the floor drifts slowly (within 0.5x..2x of the calibrated value) with temperature / AGC changes
    if (detected || m > floorMotion * RELEASE_RATIO) quietSince = 0;
    else if (!quietSince) quietSince = now ? now : 1;
    else if (now - quietSince > 10000 && now - lastAdapt >= 1000) { lastAdapt = now; floorMotion = csi_adapt_floor(floorMotion, calFloor, m, 0.02f); }
  }
  if (now - lastHist >= HIST_MS) {
    lastHist = now;
    if (histN < HIST_N) histo[(histHead + histN++) % HIST_N] = motionRatio();
    else { histo[histHead] = motionRatio(); histHead = (histHead + 1) % HIST_N; }
  }
  if (now - lastPps >= 1000) {
    lastPps = now; pps = s_pkts - lastPktsCnt; lastPktsCnt = s_pkts;
    fmtHEs = s_fmtHE - lastHE; fmtHTs = s_fmtHT - lastHT; fmtLEGs = s_fmtLEG - lastLEG;
    lastHE = s_fmtHE; lastHT = s_fmtHT; lastLEG = s_fmtLEG;
  }
}

static void setCsiSource(const uint8_t *mac) {
  memcpy(s_csiMac, mac, 6);
  s_csiMacSet = true;
  startCalibration();
}
