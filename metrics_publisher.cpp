#include "metrics_publisher.h"
#include "config.h"

#include <WiFi.h>
#include <HTTPClient.h>

static String   g_deviceId;
static bool     hasSoil = false, hasEnv = false;
static SoilData lastSoil;
static EnvData  lastEnv;
static uint32_t lastSend = 0;

static bool postJson(const char* json, size_t len) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, METRICS_URL)) return false;

  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  http.addHeader("Content-Type", "application/json");
  // Không nằm trong schema nhưng hữu ích cho backend log/định danh
  if (g_deviceId.length()) http.addHeader("X-Device-Id", g_deviceId);

  int code = http.POST((uint8_t*)json, len);
  http.end();
  return code > 0 && code < 400;
}

void metrics_setup(const String& deviceId) {
  g_deviceId = deviceId;
  lastSend   = 0;
}

void metrics_set_soil(const SoilData& d) {
  lastSoil = d;
  hasSoil  = true;
}

void metrics_set_env(const EnvData& d) {
  lastEnv = d;
  hasEnv  = true;
}

void metrics_loop() {
  const uint32_t now = millis();
  if (now - lastSend < METRICS_INTERVAL_MS) return;
  if (!hasSoil && !hasEnv) return; // chưa có gì để gửi

  lastSend = now;

  // Build JSON: chỉ add field có dữ liệu
  char buf[512];
  size_t pos = 0;

  auto append = [&](const char* s) {
    size_t l = strlen(s);
    if (pos + l >= sizeof(buf)) return false;
    memcpy(buf + pos, s, l);
    pos += l;
    return true;
  };
  auto appendFmt = [&](const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf + pos, sizeof(buf) - pos, fmt, ap);
    va_end(ap);
    if (n <= 0) return false;
    pos += (size_t)n;
    return pos < sizeof(buf);
  };

  bool first = true;
  auto addNumber = [&](const char* key, float val) {
    if (!first) append(",");
    first = false;
    return appendFmt("\"%s\":%.3f", key, val);
  };
  auto addUInt = [&](const char* key, unsigned val) {
    if (!first) append(",");
    first = false;
    return appendFmt("\"%s\":%u", key, val);
  };

  append("{");

  // ENV → airTemperature, airHumidity, lightRaw
  if (hasEnv) {
    addNumber("airTemperature", lastEnv.temp_c);
    addNumber("airHumidity",    lastEnv.humidity);
    addUInt  ("lightRaw",       lastEnv.light_raw);
  }

  // SOIL → soilTemperature, soilHumidity, N, P, K, ph
  if (hasSoil) {
    addNumber("soilTemperature", lastSoil.temperature);
    addNumber("soilHumidity",    lastSoil.humidity);
    addUInt  ("nitrogen",        lastSoil.nitrogen);
    addUInt  ("phosphorus",      lastSoil.phosphorus);
    addUInt  ("potassium",       lastSoil.potassium);
    addNumber("ph",              lastSoil.ph); // 0..14
  }

  append("}");

  const bool ok = postJson(buf, pos);
  Serial.printf("[READING] send %s, %u bytes\n", ok ? "OK" : "FAIL", (unsigned)pos);
}
