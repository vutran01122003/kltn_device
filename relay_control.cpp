#include "relay_control.h"
#include "config.h"
#include "wifi_config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static String   g_devId;
static uint32_t g_lastPoll       = 0;
static int64_t  g_serverOffsetMs = INT64_MIN;  // server_now_ms - millis()
static bool     g_relayOn        = false;
static uint64_t g_offAtMs        = 0;          // deadline tắt (theo server time)
static uint32_t g_lastHash       = 0;          // để chống kích hoạt trùng

static inline uint64_t nowServerMs() {
  if (g_serverOffsetMs == INT64_MIN) return 0; // chưa sync
  return (uint64_t)((int64_t)millis() + g_serverOffsetMs);
}

static void relay_write(bool on) {
  g_relayOn = on;
  digitalWrite(RELAY_PIN, on ? RELAY_DEFAULT_ACTIVE_LEVEL : !RELAY_DEFAULT_ACTIVE_LEVEL);
  Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");
}

static uint32_t fnv1a(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i=0;i<n;i++){ h ^= p[i]; h *= 16777619u; }
  return h;
}

static uint32_t hash_control(bool isActive, uint64_t scheduleMs, uint32_t durationMs) {
  uint8_t buf[1+8+4];
  buf[0] = isActive ? 1 : 0;
  for (int i=0;i<8;i++) buf[1+i] = (scheduleMs >> (56 - i*8)) & 0xFF;
  buf[9] = (durationMs >> 24) & 0xFF;
  buf[10]= (durationMs >> 16) & 0xFF;
  buf[11]= (durationMs >> 8 ) & 0xFF;
  buf[12]= (durationMs      ) & 0xFF;
  return fnv1a(buf, sizeof(buf));
}

// Helper parse JSON 
static bool parse_uint64(const char* key, const String& body, uint64_t& out) {
  String k1 = "\"" + String(key) + "\"";
  int k = body.indexOf(k1);
  if (k < 0) k = body.indexOf(key);
  if (k < 0) return false;

  int colon = body.indexOf(':', k);
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int)body.length() && (body[i] == ' ')) i++;

  uint64_t v = 0;
  bool got = false;
  while (i < (int)body.length() && isdigit(body[i])) {
    v = v * 10 + (body[i] - '0');
    i++;
    got = true;
  }
  if (!got) return false;
  out = v;
  return true;
}

static bool parse_bool(const char* key, const String& body, bool& out) {
  String k1 = "\"" + String(key) + "\"";
  int k = body.indexOf(k1);
  if (k < 0) k = body.indexOf(key);
  if (k < 0) return false;

  int colon = body.indexOf(':', k);
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int)body.length() && (body[i] == ' ')) i++;

  if (body.startsWith("true", i))  { out = true;  return true; }
  if (body.startsWith("false", i)) { out = false; return true; }
  return false;
}

// Gọi HTTP/HTTPS tới server
static bool poll_control_once() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setTimeout(6000);
  client.setInsecure(); // tạm thời chấp nhận mọi chứng chỉ HTTPS

  HTTPClient http;
  String url = String(CONTROL_URL) + "?device_id=" + g_devId;

  if (!http.begin(client, url)) return false;

  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-Device-Id", g_devId);
  http.setConnectTimeout(4000);
  http.setTimeout(6000);

  int code = http.GET();
  if (code <= 0) {
    Serial.printf("[CTRL] GET failed, code=%d\n", code);
    http.end();
    return false;
  }

  String enc = http.header("Content-Encoding");
  if (enc.length() && enc.indexOf("gzip") >= 0) {
    Serial.println("[CTRL] Server responded gzip; please ensure Accept-Encoding: identity");
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  bool isActive = false;
  uint64_t scheduleMs = 0, durationMs = 0, nowMs = 0;

  bool hasActive   = parse_bool("is_active", body, isActive);
  bool hasSchedule = parse_uint64("schedule_ms", body, scheduleMs);
  bool hasDuration = parse_uint64("duration_ms", body, durationMs);
  bool hasNow      = parse_uint64("now_ms", body, nowMs);

  if (!hasActive)  { Serial.println("[CTRL] missing is_active"); return false; }
  if (!hasDuration){ durationMs = 0; }
  if (!hasSchedule){ scheduleMs = 0; }

  if (hasNow) {
    g_serverOffsetMs = (int64_t)nowMs - (int64_t)millis();
  } else if (g_serverOffsetMs == INT64_MIN) {
    Serial.println("[CTRL] missing now_ms on first sync");
    return false;
  }

  uint32_t h = hash_control(isActive, scheduleMs, (uint32_t)durationMs);
  bool duplicate = (h == g_lastHash);
  uint64_t tNow = nowServerMs();

  if (!isActive) {
    if (g_relayOn) relay_write(false);
    g_offAtMs = 0;
  } else {
    bool shouldStart = false;
    if (scheduleMs == 0) shouldStart = true;
    else if (tNow >= scheduleMs) shouldStart = true;

    if (shouldStart) {
      if (!duplicate) {
        relay_write(true);
        if (durationMs > 0) {
          g_offAtMs = tNow + durationMs;
          Serial.printf("[CTRL] Scheduled OFF at %llu (duration=%llu ms)\n",
            (unsigned long long)g_offAtMs, (unsigned long long)durationMs);
        } else {
          g_offAtMs = 0;
        }
      } else {
        if (!g_relayOn) relay_write(true);
      }
    } else {
      if (g_relayOn) relay_write(false);
      g_offAtMs = 0;
    }
  }

  g_lastHash = h;
  return true;
}

// Setup & Loop
void relay_control_setup(const String& deviceId) {
  g_devId = deviceId;
  pinMode(RELAY_PIN, OUTPUT);
  relay_write(false);
  g_lastPoll = 0;
  g_serverOffsetMs = INT64_MIN;
  Serial.println("[CTRL] Relay control ready");
}

void relay_control_loop() {
  // Auto-off khi đến hạn
  if (g_relayOn && g_offAtMs > 0) {
    uint64_t tNow = nowServerMs();
    if (tNow > 0 && tNow >= g_offAtMs) {
      Serial.println("[CTRL] Auto OFF by deadline");
      relay_write(false);
      g_offAtMs = 0;
    }
  }

  uint32_t now = millis();
  if (now - g_lastPoll < CONTROL_FETCH_INTERVAL_MS) return;
  g_lastPoll = now;

  bool ok = poll_control_once();
  Serial.printf("[CTRL] poll %s\n", ok ? "OK" : "FAIL");
}
