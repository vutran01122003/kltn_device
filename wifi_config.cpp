#include "wifi_config.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static Preferences prefs;
static WebServer   server(HTTP_PORT);
static DNSServer   dnsServer;

static bool   portalActive   = false;
static String storedSSID, storedPass;

// Helpers 
static String makeApName() {
  uint8_t mac[6];
  WiFi.macAddress(mac); 
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
  return String(buf);
}

static void loadCredentials() {
  prefs.begin(APP_NS_WIFI, true);
  storedSSID = prefs.getString(APP_KEY_SSID, "");
  storedPass = prefs.getString(APP_KEY_PASS, "");
  prefs.end();
}

static void saveCredentials(const String& ssid, const String& pass) {
  prefs.begin(APP_NS_WIFI, false);
  prefs.putString(APP_KEY_SSID, ssid);
  prefs.putString(APP_KEY_PASS, pass);
  prefs.end();
  storedSSID = ssid;
  storedPass = pass;
}

void wifi_config_clear_credentials() {
  prefs.begin(APP_NS_WIFI, false);
  prefs.remove(APP_KEY_SSID);
  prefs.remove(APP_KEY_PASS);
  prefs.end();
  storedSSID = "";
  storedPass = "";
}

static String htmlPage(const String &message = "") {
  String s;
  s += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>ESP32 Wi-Fi Setup</title>";
  s += "<style>body{font-family:system-ui;padding:16px}input{width:100%;padding:8px;margin:6px 0}";
  s += "button{padding:10px 14px}.msg{padding:8px;background:#eef;margin:8px 0;border-radius:6px}";
  s += "form{margin:0}</style></head><body>";
  s += "<h2>Thiết lập Wi-Fi</h2>";
  if (message.length()) s += "<div class='msg'>" + message + "</div>";
  s += "<form method='POST' action='/save'>";
  s += "<label>SSID</label><input name='ssid' placeholder='Tên Wi-Fi' required>";
  s += "<label>Password</label><input name='pass' placeholder='Mật khẩu' type='password'>";
  s += "<div style='display:flex;gap:8px;margin-top:10px'>";
  s += "<button type='submit'>Lưu & Kết nối</button>";
  s += "<button formmethod='post' formaction='/clear' type='submit'>Xoá cấu hình</button>";
  s += "</div></form>";
  s += "<p style='margin-top:12px;color:#666'>Kết nối tới AP: <b>" + makeApName() + "</b>, ";
  s += "mở <code>http://192.168.4.1</code> nếu không tự chuyển hướng.</p>";
  s += "</body></html>";
  return s;
}

// Portal control 
static void startPortalOnly() {
  if (portalActive) return;

  // TẮT STA, CHỈ BẬT AP
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);

  const String apName = makeApName();
  if (strlen(AP_PASSWORD) > 0) WiFi.softAP(apName.c_str(), AP_PASSWORD);
  else                         WiFi.softAP(apName.c_str());
  IPAddress apIP = WiFi.softAPIP();

  Serial.printf(LOG_TAG "Portal AP: %s, IP: %s\n", apName.c_str(), apIP.toString().c_str());

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", []() { server.send(200, "text/html", htmlPage()); });
  server.on("/save", HTTP_POST, []() {
    const String ssid = server.arg("ssid");
    const String pass = server.arg("pass");
    if (!ssid.length()) {
      server.send(400, "text/plain", "SSID required");
      return;
    }
    saveCredentials(ssid, pass);

    // Thử KẾT NỐI 1 LẦN. Nếu FAIL -> quay lại Portal-only.
    server.send(200, "text/html", htmlPage("Đã lưu. Đang thử kết nối..."));
    delay(300);

    // TẠM bật STA để thử 1 lần
    WiFi.mode(WIFI_AP_STA);      // vẫn giữ AP để người dùng không rớt trang
    WiFi.disconnect(true);
    delay(100);
    Serial.printf(LOG_TAG "Thử kết nối SSID: %s (one-shot)\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long t0 = millis();
    bool ok = false;
    while (millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
      if (WiFi.status() == WL_CONNECTED) { ok = true; break; }
      delay(200);
    }

    if (ok) {
      Serial.printf(LOG_TAG "Kết nối thành công. IP: %s\n", WiFi.localIP().toString().c_str());
      // Tắt Portal, CHỈ GIỮ STA
      dnsServer.stop();
      server.stop();
      portalActive = false;
      WiFi.mode(WIFI_STA);
    } else {
      Serial.println(LOG_TAG "Kết nối thất bại. Quay lại Portal-only.");
      // TẮT STA -> quay lại AP-only
      WiFi.disconnect(true);
      delay(100);
      WiFi.mode(WIFI_AP);
    }
  });
  server.on("/clear", HTTP_POST, []() {
    wifi_config_clear_credentials();
    server.send(200, "text/html", htmlPage("Đã xoá cấu hình. Nhập SSID mới và lưu lại."));
  });
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  portalActive = true;
}

// Thử kết nối 1 lần với timeout; nếu FAIL -> VÀO PORTAL-ONLY ngay.
static bool connectOnceOrPortal(unsigned long timeoutMs) {
  if (!storedSSID.length()) {
    Serial.println(LOG_TAG "Không có cấu hình -> Portal-only");
    startPortalOnly();
    return false;
  }

  Serial.printf(LOG_TAG "Thử kết nối SSID: %s (one-shot)\n", storedSSID.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(storedSSID.c_str(), storedPass.c_str());

  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf(LOG_TAG "Connected SSID=%s, IP=%s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
    delay(200);
  }

  Serial.println(LOG_TAG "Kết nối thất bại -> chuyển sang Portal-only & tắt STA");
  startPortalOnly();
  return false;
}

// ======== Public API ========
void wifi_config_setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  loadCredentials();

  // Giữ nút để ép Portal ngay từ đầu
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long t0 = millis();
    while (digitalRead(BUTTON_PIN) == LOW && millis() - t0 < LONG_PRESS_MS) delay(10);
    if (millis() - t0 >= LONG_PRESS_MS) {
      Serial.println(LOG_TAG "Long-press: Force Portal-only");
      startPortalOnly();
      return;
    }
  }

  // Thử 1 lần. FAIL => Portal-only (không retry)
  connectOnceOrPortal(WIFI_CONNECT_TIMEOUT_MS);
}

void wifi_config_loop() {
  if (portalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;
  }

  // Không portal & không retry:
  // Nếu đang mất kết nối trong lúc chạy ứng dụng -> vào Portal-only ngay, tắt STA
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(LOG_TAG "Mất kết nối -> Portal-only, không retry");
    startPortalOnly();
  }
}

void wifi_config_force_portal() {
  startPortalOnly();
}

bool wifi_is_connected() {
  return WiFi.status() == WL_CONNECTED;
}

String wifi_local_ip() {
  return wifi_is_connected() ? WiFi.localIP().toString() : "";
}

String wifi_current_ssid() {
  return wifi_is_connected() ? WiFi.SSID() : storedSSID;
}
