#pragma once
#include <Arduino.h>

struct EnvData {
  float temp_c;        // °C
  float humidity;      // %
  uint16_t light_raw;  // 0..4095
  float light_percent; // 0..100 (%)
};

bool env_sensors_setup();
bool env_sensors_read(EnvData& out);
void env_sensors_loop();  // gọi trong loop (tự xử lý interval & Serial log)
