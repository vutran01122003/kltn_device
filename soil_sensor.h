#pragma once
#include <Arduino.h>

struct SoilData {
  float temperature;
  float humidity;
  uint16_t nitrogen;
  uint16_t phosphorus;
  uint16_t potassium;
  float ph;
};

bool soil_sensor_setup(); 
bool soil_sensor_read(SoilData& out);    
void soil_sensor_loop();                
