// ESP32-C6: thu CSI tu router va day ra Serial (dinh dang CSI_DATA cua esp-csi)
// Board: ESP32C6 Dev Module | USB CDC On Boot: Disabled | Flash 16MB
// Wi-Fi nam trong secrets.h

#include <WiFi.h>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "secrets.h"

#define BAUD            921600
#define SEND_FREQUENCY  100   // so goi ping moi giay toi router

static uint32_t s_count = 0;
static char s_line[6000];

static void csi_cb(void *ctx, wifi_csi_info_t *info) {
  if (!info || !info->buf) return;
  if (memcmp(info->mac, ctx, 6)) return;          // chi lay goi tu router

  const wifi_pkt_rx_ctrl_t *rx = &info->rx_ctrl;
  if (s_count == 0) {
    Serial.println("type,seq,mac,rssi,rate,noise_floor,fft_gain,agc_gain,channel,local_timestamp,sig_len,rx_format,len,first_word,data");
  }

  int n = snprintf(s_line, sizeof(s_line),
      "CSI_DATA,%lu,%02x:%02x:%02x:%02x:%02x:%02x,%d,%d,%d,%d,%d,%d,%lu,%d,%d,%d,%d,\"[%d",
      (unsigned long)s_count,
      info->mac[0], info->mac[1], info->mac[2], info->mac[3], info->mac[4], info->mac[5],
      rx->rssi, rx->rate, rx->noise_floor,
      0, 0,                                        // fft_gain, agc_gain (khong bu gain)
      rx->channel, (unsigned long)rx->timestamp, rx->sig_len, rx->cur_bb_format,
      info->len, info->first_word_invalid, (int)info->buf[0]);

  for (int i = 1; i < info->len && n < (int)sizeof(s_line) - 8; i++) {
    n += snprintf(s_line + n, sizeof(s_line) - n, ",%d", (int)info->buf[i]);
  }
  n += snprintf(s_line + n, sizeof(s_line) - n, "]\"\n");
  Serial.write((const uint8_t *)s_line, n);
  s_count++;
}

static void csi_init() {
  wifi_csi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.enable = true;
  cfg.acquire_csi_legacy = true;      // chi LLTF cho tuong thich router
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
  cfg.count = 0;                                   // ping vo han
  cfg.interval_ms = 1000 / SEND_FREQUENCY;
  cfg.task_stack_size = 3072;
  cfg.data_size = 1;
  cfg.target_addr.u_addr.ip4.addr = (uint32_t)WiFi.gatewayIP();
  cfg.target_addr.type = ESP_IPADDR_TYPE_V4;

  esp_ping_callbacks_t cbs = {};
  esp_ping_new_session(&cfg, &cbs, &handle);
  esp_ping_start(handle);
}

void setup() {
  Serial.begin(BAUD);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                            // tat tiet kiem nang luong de CSI deu
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  csi_init();
  ping_router_start();
}

void loop() {
  delay(1000);
}
