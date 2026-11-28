// CSI maths without any Arduino / ESP-IDF dependency, so it can be unit-tested on a PC (tests/).
// Shared by both firmwares (copied into the Lite folder by scripts/sync_shared.py).
//
// The ESP32-C6 core has no FPU: float maths runs in software. Per packet the original code did ~245 sqrtf,
// ~245 divisions and ~500 float adds/multiplies, 100 times a second. This version uses integers:
//   amplitude  ~ alpha-max-beta-min  (123*max + 51*min) / 128     (error < 4 %, and it cancels because the
//                                                                  baseline is built with the same formula)
//   normalise  amplitude / mean       Q16 fixed point (65536 = 1.0), one integer division per sub-carrier
//   baseline   Q24 fixed point, slow exponential average (~0.003 per packet)
// Only the final per-packet motion value is converted to float (once).
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define CSI_NSUB 256

struct CsiBaseline {
  int n;                       // number of sub-carriers this baseline belongs to (0 = free slot)
  bool init;
  int32_t b[CSI_NSUB];         // Q24 normalised amplitude
};

// Normalised amplitude of every sub-carrier of one CSI packet, Q16 (65536 = the packet's mean amplitude).
// buf: int8 pairs as delivered by the Wi-Fi driver (imag, real). Returns false when the packet is unusable.
static inline bool csi_normalize(const int8_t *buf, int n, int32_t *out_q16) {
  int32_t amp[CSI_NSUB];                         // Q4
  int32_t sum = 0;
  for (int i = 0; i < n; i++) {
    int re = abs((int)buf[2 * i + 1]), im = abs((int)buf[2 * i]);
    int mx = re > im ? re : im, mn = re > im ? im : re;
    amp[i] = (123 * mx + 51 * mn) >> 3;          // (123/128)*mx + (51/128)*mn, scaled by 16
    sum += amp[i];
  }
  if (sum <= 0) return false;
  int32_t mean = (sum + n / 2) / n;              // Q4
  if (mean <= 0) return false;
  for (int i = 0; i < n; i++) {
    int64_t v = ((int64_t)amp[i] << 16) / mean;
    out_q16[i] = v > (16 << 16) ? (16 << 16) : (int32_t)v;   // clamp: keeps the Q24 maths inside int32
  }
  return true;
}

static inline void csi_baseline_reset(CsiBaseline *s, int n) {
  s->n = n; s->init = false;
}

// First packet of a baseline: copy it. Later packets: returns the mean absolute deviation from the baseline
// (1.0 = as large as the mean amplitude) and moves the baseline slowly towards the packet. Returns -1 for the
// initialising packet, or when no sub-carrier carried signal.
static inline float csi_baseline_update(CsiBaseline *s, const int32_t *a_q16, int n) {
  if (!s->init) {
    for (int i = 0; i < n; i++) s->b[i] = a_q16[i] << 8;
    s->init = true;
    return -1.0f;
  }
  int32_t acc = 0;                               // sum of |a-b| in Q16: <= 245 * 2^20, fits int32
  int used = 0;
  for (int i = 0; i < n; i++) {
    int32_t a24 = a_q16[i] << 8;
    int32_t diff = a24 - s->b[i];
    bool quiet = a_q16[i] < 3277 && s->b[i] < 838861;        // both below 0.05: sub-carrier carries no signal
    s->b[i] += (diff * 3) >> 10;                 // ~0.003 per packet (also for quiet sub-carriers, as before)
    if (quiet) continue;
    acc += (diff < 0 ? -diff : diff) >> 8;
    used++;
  }
  if (!used) return -1.0f;
  return (float)acc / (float)used / 65536.0f;
}

// ---- calibration floor ----
// Median of a small sample set (insertion sort; n <= 128). Robust against someone walking by during calibration.
static inline float csi_median(float *v, int n) {
  for (int i = 1; i < n; i++) {
    float x = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return n ? ((n & 1) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2])) : 0.0f;
}

// Slow drift of the floor while the area has been quiet: moves `floor` towards `motion` by `rate`, but never
// further than [lo, hi] times the value found at calibration. Call about once a second.
static inline float csi_adapt_floor(float floor, float calibrated, float motion, float rate) {
  float lo = calibrated * 0.5f, hi = calibrated * 2.0f;
  float f = floor + rate * (motion - floor);
  if (f < lo) f = lo;
  if (f > hi) f = hi;
  return f < 0.004f ? 0.004f : f;
}
