#pragma once
#include <Arduino.h>

struct EnvData {
  float temp_c;       
  float humidity;     
  uint16_t light_raw;  
  float light_percent; 
};

bool env_sensors_setup();
bool env_sensors_read(EnvData& out);
void env_sensors_loop();  
