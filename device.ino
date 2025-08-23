#include <WiFi.h>
#include "config.h"
#include "camera_uploader.h"
#include "soil_sensor.h"
#include "env_sensors.h" 
#include "metrics_publisher.h"

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WiFi] Connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    if (millis() - start > 20000) {
      Serial.println("\n[WiFi] Timeout. Retry...");
      WiFi.disconnect(true);
      delay(1000);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      start = millis();
    }
  }
  Serial.printf("\n[WiFi] Connected. IP=%s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  String devId = WiFi.macAddress();
  devId.replace(":", "");
  Serial.printf("[SYS] DeviceId=%s\n", devId.c_str());

  connectWiFi();

  if (!camera_uploader_setup(devId)) {
    Serial.println("[FATAL] Camera init failed");
    while (true) delay(1000);
  }

  soil_sensor_setup();
  env_sensors_setup();   
  metrics_setup(devId);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost. Reconnecting...");
    connectWiFi();
  }

  camera_uploader_loop();
  soil_sensor_loop();
  env_sensors_loop();    
  metrics_loop();
  
  delay(5);
}
