#include "metrics_publisher.h"
#include "env_sensors.h"
#include "config.h"

#include <DHT.h>

static DHT dht(DHT_PIN, DHT_TYPE);
static uint32_t g_lastEnvRead = 0;

// Đọc ADC nhiều mẫu rồi lấy trung bình
static uint16_t readLightAveraged() {
  uint32_t acc = 0;
  for (int i = 0; i < LIGHT_SAMPLES; ++i) {
    acc += analogRead(LIGHT_PIN);
    delay(2);
  }
  return (uint16_t)(acc / LIGHT_SAMPLES);
}

bool env_sensors_setup() {
  // DHT
  dht.begin();

  // ADC (tùy core esp32, mặc định đã là 12-bit)
#if defined(ARDUINO_ARCH_ESP32)
  analogReadResolution(12);
#endif

  // Nếu cần cấu hình attenuation (tùy module ánh sáng):
  // analogSetPinAttenuation(LIGHT_PIN, ADC_11db); // đo dải rộng hơn (≈3.3V)

  // Bỏ lần đọc đầu của DHT cho ổn định
  (void)dht.readTemperature();
  (void)dht.readHumidity();
  delay(100);

  Serial.println("[ENV] DHT & Light ready");
  g_lastEnvRead = millis() - ENV_UPDATE_INTERVAL_MS; // đọc ngay lần đầu
  return true;
}

bool env_sensors_read(EnvData& out) {
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // °C

  // Nếu lỗi (NaN), thử lại nhẹ
  if (isnan(h) || isnan(t)) {
    delay(80);
    h = dht.readHumidity();
    t = dht.readTemperature();
  }
  if (isnan(h) || isnan(t)) {
    Serial.println("[ENV] DHT read failed");
    return false;
  }

  uint16_t raw = readLightAveraged();

  float percent = (raw / LIGHT_ADC_MAX) * 100.0f;
#if LIGHT_PERCENT_INVERT
  percent = 100.0f - percent;
#endif
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  out.temp_c = t;
  out.humidity = h;
  out.light_raw = raw;
  out.light_percent = percent;
  return true;
}

void env_sensors_loop() {
  const uint32_t now = millis();
  if (now - g_lastEnvRead < ENV_UPDATE_INTERVAL_MS) return;
  g_lastEnvRead = now;

  EnvData d;
  if (env_sensors_read(d)) {
    Serial.printf("[ENV] T=%.1f°C, H=%.1f%%, Light=%u (%.1f%%)\n", d.temp_c, d.humidity, d.light_raw, d.light_percent);
    metrics_set_env(d);
  } else {
    Serial.println("[ENV] read FAILED");
  }
}
