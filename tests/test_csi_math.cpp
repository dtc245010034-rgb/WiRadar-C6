// Host test for firmware/WiRadar_C6/csi_math.h: compares the integer pipeline with the original float one.
//   g++ -O2 -std=c++17 -Wall -Wextra -o /tmp/test_csi_math tests/test_csi_math.cpp && /tmp/test_csi_math
// The CSI frames are SYNTHETIC (static multipath + noise + a moving-scatterer component). They check that the
// integer maths agrees with the float maths; they do not replace measurements on the real board.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include "../firmware/WiRadar_C6/csi_math.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ---- the original float implementation (copied from the old csi_cb) ----
struct FloatRef {
  int n = 0; bool init = false; float b[CSI_NSUB]; float motion = 0;
  bool feed(const int8_t *buf, int len) {
    int nn = len / 2; if (nn > CSI_NSUB) nn = CSI_NSUB; if (nn < 16) return false;
    float a[CSI_NSUB], sum = 0;
    for (int i = 0; i < nn; i++) { int re = buf[2*i+1], im = buf[2*i]; a[i] = std::sqrt((float)(re*re + im*im)); sum += a[i]; }
    if (sum <= 0) return false;
    float mean = sum / nn; for (int i = 0; i < nn; i++) a[i] /= mean;
    if (!init) { for (int i = 0; i < nn; i++) b[i] = a[i]; init = true; n = nn; return true; }
    float d = 0; int used = 0;
    for (int i = 0; i < nn; i++) { if (a[i] < 0.05f && b[i] < 0.05f) continue; d += std::fabs(a[i] - b[i]); used++; }
    if (!used) return true;
    d /= used;
    for (int i = 0; i < nn; i++) b[i] += 0.003f * (a[i] - b[i]);
    motion = 0.95f * motion + 0.05f * d;
    return true;
  }
};

struct IntImpl {
  CsiBaseline s; float motion = 0;
  IntImpl() { csi_baseline_reset(&s, 0); }
  void feed(const int8_t *buf, int len) {
    int nn = len / 2; if (nn > CSI_NSUB) nn = CSI_NSUB; if (nn < 16) return;
    int32_t a[CSI_NSUB];
    if (!csi_normalize(buf, nn, a)) return;
    float d = csi_baseline_update(&s, a, nn);
    if (d >= 0) motion = 0.95f * motion + 0.05f * d;
  }
};

struct Scene {
  std::mt19937 rng{12345};
  int nsub; std::vector<float> hr, hi, dr, di;      // static channel, scatterer direction
  explicit Scene(int n) : nsub(n), hr(n), hi(n), dr(n), di(n) {
    std::normal_distribution<float> g(0, 1);
    for (int i = 0; i < n; i++) { float ph = g(rng) * 3; float amp = 25 + 10 * g(rng); amp = amp < 6 ? 6 : amp;
      hr[i] = amp * std::cos(ph); hi[i] = amp * std::sin(ph); dr[i] = g(rng); di[i] = g(rng); }
  }
  std::vector<int8_t> frame(float motion, float t) {  // motion: amplitude of the moving component (0 = still)
    std::normal_distribution<float> noise(0, 0.8f);
    std::vector<int8_t> out(nsub * 2);
    for (int i = 0; i < nsub; i++) {
      float ph = t * (0.6f + 0.01f * i);
      float re = hr[i] + motion * (dr[i] * std::cos(ph) - di[i] * std::sin(ph)) + noise(rng);
      float im = hi[i] + motion * (dr[i] * std::sin(ph) + di[i] * std::cos(ph)) + noise(rng);
      auto c = [](float v) { v = std::round(v); return (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v)); };
      out[2*i] = c(im); out[2*i+1] = c(re);
    }
    return out;
  }
};

static void test_pipeline(int nsub, const char *name) {
  Scene sc(nsub); FloatRef f; IntImpl q;
  std::vector<float> fm, qm;
  float t = 0; int agree = 0, total = 0;
  float floorF = 0, floorQ = 0; int calN = 0;
  for (int k = 0; k < 2600; k++) {
    float m = (k >= 1000 && k < 1300) ? 6.0f : ((k >= 1800 && k < 1900) ? 14.0f : 0.0f);   // two bursts of motion
    auto fr = sc.frame(m, t); t += 0.1f;
    f.feed(fr.data(), (int)fr.size()); q.feed(fr.data(), (int)fr.size());
    if (k >= 300 && k < 800) { floorF += f.motion; floorQ += q.motion; calN++; }   // calibration window
    fm.push_back(f.motion); qm.push_back(q.motion);
  }
  floorF /= calN; floorQ /= calN;
  CHECK(std::fabs(floorQ - floorF) / floorF < 0.10f, "%s: floor differs: float %.5f int %.5f", name, floorF, floorQ);
  // detection decisions (ratio > 1.8) must agree except right at the threshold
  float peakF = 0, peakQ = 0;
  for (size_t k = 800; k < fm.size(); k++) {
    float rf = fm[k] / floorF, rq = qm[k] / floorQ;
    if (rf > peakF) peakF = rf;
    if (rq > peakQ) peakQ = rq;
    if (std::fabs(rf - 1.8f) > 0.25f) { total++; if ((rf > 1.8f) == (rq > 1.8f)) agree++; }
  }
  CHECK(total > 0 && agree == total, "%s: detection decisions disagree %d/%d", name, total - agree, total);
  CHECK(peakF > 1.8f && peakQ > 1.8f, "%s: burst not detected (float %.2f int %.2f)", name, peakF, peakQ);
  CHECK(std::fabs(peakQ - peakF) / peakF < 0.15f, "%s: peak ratio differs: float %.2f int %.2f", name, peakF, peakQ);
  std::printf("%-6s floor float %.4f int %.4f | peak ratio float %.2f int %.2f | decisions agree %d/%d\n",
              name, floorF, floorQ, peakF, peakQ, agree, total);
}

static void test_edge_cases() {
  int32_t out[CSI_NSUB];
  int8_t zeros[64] = {0};
  CHECK(!csi_normalize(zeros, 32, out), "all-zero packet must be rejected");
  int8_t big[2 * CSI_NSUB]; std::memset(big, 127, sizeof(big));
  CHECK(csi_normalize(big, CSI_NSUB, out), "saturated packet should normalise");
  for (int i = 0; i < CSI_NSUB; i++) CHECK(out[i] > 60000 && out[i] < 70000, "flat packet -> ratio 1.0, got %d", out[i]);
  int8_t neg[64]; std::memset(neg, -128, sizeof(neg));                      // abs(-128) = 128 must not overflow
  CHECK(csi_normalize(neg, 32, out), "-128 samples");
  // one huge sub-carrier must be clamped, not overflow
  int8_t spike[64]; std::memset(spike, 0, sizeof(spike)); spike[1] = 127; spike[3] = 1; spike[5] = 1;
  CHECK(csi_normalize(spike, 32, out) && out[0] <= (16 << 16), "spike clamp");
  CsiBaseline b; csi_baseline_reset(&b, 32);
  int32_t a[32]; for (int i = 0; i < 32; i++) a[i] = 65536;
  CHECK(csi_baseline_update(&b, a, 32) < 0, "first packet initialises");
  float d = csi_baseline_update(&b, a, 32); CHECK(d >= 0 && d < 1e-4f, "identical packet -> d~0, got %g", d);
  for (int i = 0; i < 32; i++) a[i] = 2 * 65536;
  d = csi_baseline_update(&b, a, 32); CHECK(std::fabs(d - 1.0f) < 0.01f, "double amplitude -> d~1, got %g", d);
}

static void test_floor_helpers() {
  float v1[] = {5, 1, 3, 2, 4};                       CHECK(std::fabs(csi_median(v1, 5) - 3) < 1e-6, "median odd");
  float v2[] = {4, 1, 3, 2};                          CHECK(std::fabs(csi_median(v2, 4) - 2.5f) < 1e-6, "median even");
  float v3[] = {0.02f, 0.021f, 0.019f, 0.02f, 0.9f};  // someone walks by during calibration
  CHECK(std::fabs(csi_median(v3, 5) - 0.02f) < 0.002f, "median ignores outlier");
  CHECK(csi_median(v3, 0) == 0.0f, "median of nothing");
  float f = 0.02f;
  for (int i = 0; i < 100000; i++) f = csi_adapt_floor(f, 0.02f, 5.0f, 0.01f);     // huge motion: capped at 2x
  CHECK(std::fabs(f - 0.04f) < 1e-5f, "floor capped at 2x, got %g", f);
  for (int i = 0; i < 100000; i++) f = csi_adapt_floor(f, 0.02f, 0.0f, 0.01f);     // silence: floored at 0.5x
  CHECK(std::fabs(f - 0.01f) < 1e-5f, "floor limited to 0.5x, got %g", f);
  CHECK(csi_adapt_floor(0.004f, 0.005f, 0.0f, 1.0f) >= 0.004f, "absolute minimum");
}

int main() {
  test_pipeline(52, "HT20");     // 64-sample HT CSI has ~52 usable sub-carriers
  test_pipeline(245, "HE20");
  test_edge_cases();
  test_floor_helpers();
  std::printf(g_fail ? "FAILED (%d)\n" : "all tests passed\n", g_fail);
  return g_fail ? 1 : 0;
}
