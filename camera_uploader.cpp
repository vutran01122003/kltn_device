#include "camera_uploader.h"
#include "config.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "board_config.h"

static String g_deviceId;
static uint32_t g_lastUpload = 0;

static bool uploadJpeg(uint8_t* data, size_t len) {
  if (!data || !len) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, UPLOAD_URL)) return false;

  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  http.addHeader("Content-Type", "image/jpeg");
  if (g_deviceId.length()) http.addHeader("X-Device-Id", g_deviceId);

  int code = http.POST(data, len);
  http.end();
  return code > 0 && code < 400;
}

static bool cameraInit() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAME_SIZE;
  config.jpeg_quality = JPEG_QUALITY;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_count     = 1;

  if (!psramFound()) {
    if (config.frame_size > FRAMESIZE_SVGA) config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV2640_PID) {
    s->set_vflip(s, CAM_VFLIP);
    s->set_hmirror(s, CAM_HMIRROR);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  // Warm-up 2 frame
  for (int i = 0; i < 2; ++i) {
    camera_fb_t *warm = esp_camera_fb_get();
    if (warm) esp_camera_fb_return(warm);
    delay(50);
  }

  Serial.println("[CAM] Ready");
  return true;
}

bool camera_uploader_setup(const String& deviceId) {
  g_deviceId = deviceId;
  if (!cameraInit()) return false;
  g_lastUpload = millis() - UPLOAD_INTERVAL_MS;
  return true;
}

void camera_uploader_loop() {
  const uint32_t now = millis();
  if (now - g_lastUpload < UPLOAD_INTERVAL_MS) return;
  g_lastUpload = now;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("[CAM] fb_get FAILED"); return; }

  bool ok = uploadJpeg(fb->buf, fb->len);
  Serial.printf("[UP] %s, bytes=%u\n", ok ? "OK" : "FAIL", (unsigned)fb->len);

  esp_camera_fb_return(fb);
}
