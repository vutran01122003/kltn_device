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

  wifi_config_setup();

  if (!camera_uploader_setup(DEVICE_ID)) {
    Serial.println("[FATAL] Camera init failed");
    while (true) delay(1000);
  }

  soil_sensor_setup();
  env_sensors_setup();
  metrics_setup(DEVICE_ID);
  relay_control_setup(DEVICE_ID);
}

void loop() {
  wifi_config_loop();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000UL) {
    lastPrint = millis();
    if (wifi_is_connected()) {
      Serial.printf("[SYS] DeviceId=%s\n", DEVICE_ID);
      Serial.printf(LOG_TAG "Connected SSID=%s, IP=%s\n", wifi_current_ssid().c_str(), wifi_local_ip().c_str());
    } else {
      Serial.println(LOG_TAG "Not connected");
    }
  }

  delay(10);


  camera_uploader_loop();
  soil_sensor_loop();
  env_sensors_loop();
  metrics_loop();
  relay_control_loop();

  delay(5);
}
