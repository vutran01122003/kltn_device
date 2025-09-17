#include <WiFi.h>
#include "config.h"
#include "camera_uploader.h"
#include "soil_sensor.h"
#include "env_sensors.h"
#include "metrics_publisher.h"
#include "wifi_config.h"

void setup() {
  Serial.begin(115200);
  delay(200);

  String devId = WiFi.macAddress();
  devId.replace(":", "");
  Serial.printf("[SYS] DeviceId=%s\n", devId.c_str());

  wifi_config_begin("MyDevice-Setup", "12345678");

  if (!camera_uploader_setup(devId)) {
    Serial.println("[FATAL] Camera init failed");
    while (true) delay(1000);
  }

  soil_sensor_setup();
  env_sensors_setup();
  metrics_setup(devId);
}

void loop() {
  wifi_config_loop();


  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t last = 0;
    if (millis() - last > 3000) {
      last = millis();
      Serial.println("[WiFi] Not connected (STA). Portal có thể đang chạy nếu chưa cấu hình.");
    }
  }

  camera_uploader_loop();
  soil_sensor_loop();
  env_sensors_loop();
  metrics_loop();

  delay(5);
}
