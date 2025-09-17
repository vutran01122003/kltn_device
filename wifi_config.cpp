#include "wifi_config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

static Preferences prefs;       // NVS storage
static WebServer server(80);    // lightweight HTTP server
static bool portalActive = false;

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
  s += F("<p>Device will reboot after saving. Nếu connect fail thì portal sẽ bật lại.</p>");
  s += F("</body></html>");
  return s;
}

static void startPortal(const char* apSsid, const char* apPass) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, (apPass && strlen(apPass)) ? apPass : nullptr);
  IPAddress ip = WiFi.softAPIP();

  server.on("/", HTTP_GET, [] { server.send(200, "text/html", htmlIndex()); });
  server.on("/save", HTTP_POST, [] {
    String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
    String pass = server.hasArg("pass") ? server.arg("pass") : "";
    prefs.begin("net", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    server.send(200, "text/plain", "Saved. Rebooting...\n");
    delay(500);
    ESP.restart();
  });
  server.on("/reset", HTTP_POST, [] {
    prefs.begin("net", false);
    prefs.clear();
    prefs.end();
    server.send(200, "text/plain", "Cleared. Rebooting...\n");
    delay(500);
    ESP.restart();
  });
  server.begin();
  portalActive = true;
  Serial.printf("[WiFiCfg] Portal tại http://%s\n", ip.toString().c_str());
}

static bool tryConnectFromPrefs(uint32_t timeoutMs = 15000) {
  prefs.begin("net", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.isEmpty()) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.isEmpty() ? nullptr : pass.c_str());
  Serial.printf("[WiFiCfg] Connecting tới '%s'...\n", ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFiCfg] Connected: %s\n", WiFi.localIP().toString().c_str());
    portalActive = false;
    return true;
  }
  Serial.println("[WiFiCfg] Kết nối thất bại");
  return false;
}

bool wifi_config_begin(const char* apSsid, const char* apPass) {
  // if (tryConnectFromPrefs()) return true;
  startPortal(apSsid, apPass);
  return false;
}

void wifi_config_loop() {
  if (portalActive) server.handleClient();
}

void wifi_config_factory_reset() {
  prefs.begin("net", false);
  prefs.clear();
  prefs.end();
  ESP.restart();
}

bool wifi_config_is_portal_active() { return portalActive; }
