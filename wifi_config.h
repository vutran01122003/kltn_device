#pragma once
#include <Arduino.h>

void wifi_config_setup();
void wifi_config_loop();
void wifi_config_force_portal();
void wifi_config_clear_credentials();

// Trạng thái nhanh
bool wifi_is_connected();
String wifi_local_ip();
String wifi_current_ssid();
