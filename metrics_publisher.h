#pragma once
#include <Arduino.h>
#include "soil_sensor.h"
#include "env_sensors.h"

void metrics_setup(const String& deviceId);
void metrics_set_soil(const SoilData& d);
void metrics_set_env(const EnvData& d);
void metrics_loop();   // gọi trong loop(); sẽ gửi JSON mỗi METRICS_INTERVAL_MS
