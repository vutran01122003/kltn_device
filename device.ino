#include <WiFi.h>
#include "config.h"
#include "wifi_config.h"
#include "camera_uploader.h"
#include "soil_sensor.h"
#include "env_sensors.h"
#include "metrics_publisher.h"
#include "relay_control.h"

void setup() {
  Serial.begin(115200);
  delay(200);

  String devId = "esp32-01";
  devId.replace(":", "");
  Serial.printf("[SYS] DeviceId=%s\n", devId.c_str());

  wifi_config_begin(AP_SETUP_SSID, AP_SETUP_PASS, WIFI_CONNECT_TIMEOUT_MS);

  if (!camera_uploader_setup(devId)) {
    Serial.println("[FATAL] Camera init failed");
    while (true) delay(1000);
  }

  // soil_sensor_setup();
  env_sensors_setup();
  metrics_setup(devId);
  relay_control_setup(devId);
}

void loop() {
  wifi_config_loop();

  // camera_uploader_loop();
  // soil_sensor_loop();
  env_sensors_loop();
  metrics_loop();
  relay_control_loop();

  if (WiFi.status() != WL_CONNECTED && !wifi_config_is_portal_active()) {
    static uint32_t last = 0;
    if (millis() - last > 3000) {
      last = millis();
      Serial.println("[WiFi] Not connected (STA). Tự reconnect hoặc sẽ bật portal nếu fail nhiều.");
    }
  }

  delay(5);
}
