#include "wifi_config.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_wifi.h>   

// NVS của app để lưu SSID/PASS nhập từ portal
static const char* NS_NET   = "net";
static const char* KEY_SSID = "ssid";
static const char* KEY_PASS = "pass";

static Preferences prefs;
static WebServer   server(80);
static bool        portalActive  = false;   
static bool        portalStarted = false;  

// Theo dõi rớt mạng để auto-bật portal
static uint32_t    disconnectedSince = 0;

static String htmlIndex() {
  String s;
  s += F("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  s += F("<title>Wi-Fi Setup</title><style>body{font-family:system-ui;margin:24px}input,button{font-size:16px;padding:8px;margin:6px 0;width:100%}code{background:#eee;padding:2px 6px;border-radius:4px}</style></head><body>");
  s += F("<h2>ESP32 Wi-Fi Setup</h2>");
  s += F("<form method=\"POST\" action=\"/save\">");
  s += F("<label>SSID<br><input name=\"ssid\" placeholder=\"Wi-Fi name\" required></label><br>");
  s += F("<label>Password<br><input name=\"pass\" placeholder=\"Wi-Fi password (blank for open)\"></label><br>");
  s += F("<button type=submit>Save & Connect</button></form><hr>");
  s += F("<form method=\"POST\" action=\"/reset\"><button style=\"background:#e11;color:#fff\" type=submit>Factory reset Wi-Fi</button></form>");
#if PORTAL_ALWAYS_ON
  s += F("<p>Portal ALWAYS-ON.</p>");
#else
  s += F("<p>Portal auto bật khi rớt mạng quá lâu.</p>");
#endif
  s += F("</body></html>");
  return s;
}

static void staStopHard() {
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  esp_wifi_disconnect();  
  delay(50);

  esp_wifi_stop();         
  delay(50);

  WiFi.mode(WIFI_OFF);
  delay(50);

  esp_wifi_start();
  delay(50);
}

static bool tryConnect(const String& ssid, const String& pass, uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS) {
  if (ssid.isEmpty()) return false;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  #if PORTAL_ALWAYS_ON
    WiFi.mode(WIFI_AP_STA);
  #else
    WiFi.mode(WIFI_STA);
  #endif

  WiFi.disconnect(true, true);
  delay(100);

  Serial.printf("[WiFiCfg] Connecting tới '%s'...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFiCfg] Connected: %s\n", WiFi.localIP().toString().c_str());
    disconnectedSince = 0;      
    return true;
  }
  Serial.println("[WiFiCfg] Kết nối thất bại");
  return false;
}

static bool tryConnectFromNvs(uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS) {
  prefs.begin(NS_NET, true);
  String ssid = prefs.getString(KEY_SSID, "");
  String pass = prefs.getString(KEY_PASS, "");
  prefs.end();
  return tryConnect(ssid, pass, timeoutMs);
}

// ================ Portal =================
static void startPortal(const char* apSsid, const char* apPass) {
  if (portalStarted) return; 

  // Bật AP và web server
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, (apPass && strlen(apPass)) ? apPass : nullptr);
  IPAddress ip = WiFi.softAPIP();

  server.on("/", HTTP_GET, [] { server.send(200, "text/html", htmlIndex()); });

  server.on("/status", HTTP_GET, [] {
    String json = "{";
    json += "\"portal\":"    + String(portalActive ? "true" : "false");
    json += ",\"sta_status\":"+ String((int)WiFi.status());
    json += ",\"ip\":\""     + WiFi.localIP().toString() + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/scan", HTTP_GET, [] {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
              ",\"enc\":" + String(WiFi.encryptionType(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_POST, [] {
    String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
    String pass = server.hasArg("pass") ? server.arg("pass") : "";
    Serial.printf("[WiFiCfg] Save request: ssid='%s', len(pass)=%d\n", ssid.c_str(), pass.length());

    if (pass.length() > 0 && pass.length() < 8) {
      server.send(400, "text/plain", "Password must be >= 8 chars for WPA2.");
      return;
    }

    server.sendHeader("Connection", "close");
    
    staStopHard();

    WiFi.mode(WIFI_AP_STA);

    Serial.println("[WiFiCfg] Applying new credentials...");
    WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      // Lưu vào NVS để boot sau ƯU TIÊN NVS
      prefs.begin(NS_NET, false);
      prefs.putString(KEY_SSID, ssid);
      prefs.putString(KEY_PASS, pass);
      prefs.end();

      String ip = WiFi.localIP().toString();
      Serial.printf("[WiFiCfg] Connected: %s\n", ip.c_str());
      server.send(200, "text/plain", String("Connected: ") + ip + "\nRebooting...\n");
      delay(300);
      ESP.restart();
      return;
    }

    Serial.println("[WiFiCfg] Kết nối thất bại (sau khi áp dụng mới). Giữ portal.");
    server.send(200, "text/plain", "Connection failed. Stay on portal.\n");
  });

  // Xóa NVS app + credentials của Wi-Fi core
  server.on("/reset", HTTP_POST, [] {
    prefs.begin(NS_NET, false);
    prefs.clear();
    prefs.end();
    esp_wifi_restore();

    server.send(200, "text/plain", "Factory reset Wi-Fi done. Rebooting...\n");
    delay(300);
    ESP.restart();
  });

  server.begin();
  portalActive  = true;
  portalStarted = true;
  Serial.printf("[WiFiCfg] Portal tại http://%s\n", ip.toString().c_str());
}

static inline void ensurePortal(const char* apSsid, const char* apPass) {
  if (!portalStarted) startPortal(apSsid, apPass);
}

// ============== Public APIs ==============
bool wifi_config_begin(const char* apSsid, const char* apPass, uint32_t connectTimeoutMs) {
  // ƯU TIÊN NVS
  if (tryConnectFromNvs(connectTimeoutMs)) {
    #if PORTAL_ALWAYS_ON
        ensurePortal(apSsid, apPass);
    #endif
        return true;
  }

  // FALLBACK config.h
  #ifdef WIFI_SSID
    if (strlen(WIFI_SSID) > 0 && tryConnect(String(WIFI_SSID), String(WIFI_PASS), connectTimeoutMs)) {
  #if PORTAL_ALWAYS_ON
      ensurePortal(apSsid, apPass);
  #endif
      return true;
    }
  #endif

  // Không vào được: bật portal
  startPortal(apSsid, apPass);
  return false;
}

void wifi_config_loop() {
  // Xử lý server
  #if PORTAL_ALWAYS_ON
  // luôn đảm bảo AP+server chạy
    ensurePortal(AP_SETUP_SSID, AP_SETUP_PASS); 
    server.handleClient();
  #else
    if (portalActive) server.handleClient();

    // Auto-bật portal khi rớt mạng quá lâu
  #if AUTO_PORTAL_ON_DISCONNECT
    if (WiFi.status() == WL_CONNECTED) {
      disconnectedSince = 0; // đang online
    } else {
      if (disconnectedSince == 0) disconnectedSince = millis();
      if (!portalStarted && (millis() - disconnectedSince >= DISCONNECT_BEFORE_PORTAL_MS)) {
        Serial.println("[WiFiCfg] STA mất kết nối lâu -> bật portal");
        startPortal(AP_SETUP_SSID, AP_SETUP_PASS);
      }
    }
  #endif
  #endif
}

void wifi_config_factory_reset() {
  prefs.begin(NS_NET, false);
  prefs.clear();
  prefs.end();
  esp_wifi_restore();
  ESP.restart();
}

bool wifi_config_is_portal_active() { return portalActive; }
