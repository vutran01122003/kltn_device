#pragma once
#include <Arduino.h>
#include "config.h"

bool wifi_config_begin(const char* apSsid = AP_SETUP_SSID, const char* apPass = AP_SETUP_PASS, uint32_t connectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS);
void wifi_config_loop();
void wifi_config_factory_reset();
bool wifi_config_is_portal_active();
