#include "relay_control.h"
#include "config.h"
#include "wifi_config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static String   g_devId;
static uint32_t g_lastPoll       = 0;
static int64_t  g_serverOffsetMs = INT64_MIN;  // server_now_ms - millis()

struct ChannelState {
  const char* key;    // "light" hoặc "pump" trong JSON
  uint8_t pin;        // chân relay tương ứng
  bool relayOn;       // trạng thái hiện tại
  uint64_t offAtMs;   // deadline tự tắt (theo server time)
  uint32_t lastHash;  // FNV1a hash để chống kích hoạt trùng
};

static ChannelState g_channels[] = {
  { "light", LIGHT_RELAY_PIN, false, 0, 0 }, // pin 47
  { "pump",  PUMP_RELAY_PIN,  false, 0, 0 }  // pin 21
};

static inline uint64_t nowServerMs() {
  if (g_serverOffsetMs == INT64_MIN) return 0; // chưa sync
  return (uint64_t)((int64_t)millis() + g_serverOffsetMs);
}

static void relay_write(ChannelState& ch, bool on) {
  ch.relayOn = on;
  digitalWrite(ch.pin, on ? RELAY_DEFAULT_ACTIVE_LEVEL : !RELAY_DEFAULT_ACTIVE_LEVEL);
  Serial.printf("[RELAY-%s] %s (pin %d)\n",
                ch.key,
                on ? "ON" : "OFF",
                ch.pin);
}

// FNV1a Hash 
static uint32_t fnv1a(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

static uint32_t hash_control(bool isActive, uint64_t scheduleMs, uint32_t durationMs) {
  uint8_t buf[1 + 8 + 4];
  buf[0] = isActive ? 1 : 0;
  for (int i = 0; i < 8; i++) buf[1 + i] = (scheduleMs >> (56 - i * 8)) & 0xFF;
  buf[9]  = (durationMs >> 24) & 0xFF;
  buf[10] = (durationMs >> 16) & 0xFF;
  buf[11] = (durationMs >> 8 ) & 0xFF;
  buf[12] = (durationMs      ) & 0xFF;
  return fnv1a(buf, sizeof(buf));
}

// JSON helpers
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

static bool extract_device_json(const String& body, const char* devKey, String& out) {
  String keyStr = "\"" + String(devKey) + "\"";
  int keyPos = body.indexOf(keyStr);
  if (keyPos < 0) return false;

  
  int braceStart = body.indexOf('{', keyPos);
  if (braceStart < 0) return false;

  int depth = 0;
  int i;
  for (i = braceStart; i < (int)body.length(); ++i) {
    char c = body[i];
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) {
        break; 
      }
    }
  }

  if (depth != 0) return false;  
  out = body.substring(braceStart, i + 1);
  return true;
}

static void handle_channel(ChannelState& ch,
                           bool isActive,
                           uint64_t scheduleMs,
                           uint64_t durationMs) {
  uint32_t h = hash_control(isActive, scheduleMs, (uint32_t)durationMs);
  bool duplicate = (h == ch.lastHash);
  uint64_t tNow = nowServerMs();

  if (!isActive) {
    if (ch.relayOn) relay_write(ch, false);
    ch.offAtMs = 0;
  } else {
    bool shouldStart = false;
    if (scheduleMs == 0) shouldStart = true;
    else if (tNow >= scheduleMs) shouldStart = true;

    if (shouldStart) {
      if (!duplicate) {
        // lần mới thì bật relay, set deadline nếu có duration
        relay_write(ch, true);
        if (durationMs > 0) {
          ch.offAtMs = tNow + durationMs;
          Serial.printf("[CTRL-%s] Scheduled OFF at %llu (duration=%llu ms)\n",
                        ch.key,
                        (unsigned long long)ch.offAtMs,
                        (unsigned long long)durationMs);
        } else {
          ch.offAtMs = 0;
        }
      } else {
        // lệnh cũ, nhưng nếu relay đang OFF thì bật lại
        if (!ch.relayOn) relay_write(ch, true);
      }
    } else {
      // chưa tới giờ chạy
      if (ch.relayOn) relay_write(ch, false);
      ch.offAtMs = 0;
    }
  }

  ch.lastHash = h;
}

// HTTP poll
static bool poll_control_once() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  // WiFiClient client;
  client.setTimeout(6000);
  client.setInsecure(); 

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

  bool anyOk = false;

  for (int idx = 0; idx < (int)(sizeof(g_channels)/sizeof(g_channels[0])); ++idx) {
    ChannelState& ch = g_channels[idx];

    String devJson;
    if (!extract_device_json(body, ch.key, devJson)) {
      Serial.printf("[CTRL-%s] Key not found in JSON\n", ch.key);
      continue; 
    }

    bool isActive = false;
    uint64_t scheduleMs = 0, durationMs = 0, nowMs = 0;

    bool hasActive   = parse_bool("is_active", devJson, isActive);
    bool hasSchedule = parse_uint64("schedule_ms", devJson, scheduleMs);
    bool hasDuration = parse_uint64("duration_ms", devJson, durationMs);
    bool hasNow      = parse_uint64("now_ms", devJson, nowMs);

    if (!hasActive) {
      Serial.printf("[CTRL-%s] missing is_active\n", ch.key);
      continue;
    }
    if (!hasDuration) durationMs = 0;
    if (!hasSchedule) scheduleMs = 0;

    // sync server time
    if (hasNow) {
      g_serverOffsetMs = (int64_t)nowMs - (int64_t)millis();
    } else if (g_serverOffsetMs == INT64_MIN) {
      Serial.printf("[CTRL-%s] missing now_ms on first sync\n", ch.key);
      continue;
    }

    handle_channel(ch, isActive, scheduleMs, durationMs);
    anyOk = true;
  }

  return anyOk;
}

void relay_control_setup(const String& deviceId) {
  g_devId = deviceId;
  g_lastPoll = 0;
  g_serverOffsetMs = INT64_MIN;

  // init 2 relay pins
  for (int i = 0; i < (int)(sizeof(g_channels)/sizeof(g_channels[0])); ++i) {
    ChannelState& ch = g_channels[i];
    pinMode(ch.pin, OUTPUT);
    ch.relayOn  = false;
    ch.offAtMs  = 0;
    ch.lastHash = 0;
    relay_write(ch, false);
  }

  Serial.println("[CTRL] Relay control ready (light + pump)");
}

void relay_control_loop() {
  uint64_t tNow = nowServerMs();
  if (tNow > 0) {
    for (int i = 0; i < (int)(sizeof(g_channels)/sizeof(g_channels[0])); ++i) {
      ChannelState& ch = g_channels[i];
      if (ch.relayOn && ch.offAtMs > 0 && tNow >= ch.offAtMs) {
        Serial.printf("[CTRL-%s] Auto OFF by deadline\n", ch.key);
        relay_write(ch, false);
        ch.offAtMs = 0;
      }
    }
  }

  // Poll server theo chu kỳ
  uint32_t now = millis();
  if (now - g_lastPoll < CONTROL_FETCH_INTERVAL_MS) return;
  g_lastPoll = now;

  bool ok = poll_control_once();
  Serial.printf("[CTRL] poll %s\n", ok ? "OK" : "FAIL");
}
