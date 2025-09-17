#pragma once
#include <Arduino.h>

bool wifi_config_begin(const char* apSsid = "Device-Setup", const char* apPass = "12345678");
void wifi_config_loop();
void wifi_config_factory_reset();
bool wifi_config_is_portal_active();
