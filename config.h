#pragma once
#include <Arduino.h>

// WiFi 
#define APP_NS_WIFI          "wifi"    
#define APP_KEY_SSID         "ssid"
#define APP_KEY_PASS         "pass"

#define AP_SSID_PREFIX       "ESP32-SETUP-"
#define AP_PASSWORD          ""        //  đặt password cho AP

#define DNS_PORT             53
#define HTTP_PORT            80

#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define WIFI_RETRY_MS           5000UL

// Nút nhấn để ép bật portal (nhấn giữ)
#define BUTTON_PIN           0         
#define LONG_PRESS_MS        1500UL
#define LOG_TAG              "[WiFiCFG] "

// Upload
#define UPLOAD_URL            "https://kltn-api-dpqj.onrender.com/api/health-check/weekly-image"
#define UPLOAD_INTERVAL_MS    300000UL 
#define METRICS_URL             "https://kltn-api-dpqj.onrender.com/api/readings"  
#define METRICS_INTERVAL_MS     5000UL   

// Control
#define CONTROL_URL              "https://kltn-api-dpqj.onrender.com/api/device-control"
#define RELAY_PIN                   21
#define CONTROL_FETCH_INTERVAL_MS   5000
#define RELAY_DEFAULT_ACTIVE_LEVEL  HIGH  // Đổi thành LOW nếu relay của bạn active-low

// Camera quality
#define FRAME_SIZE            FRAMESIZE_QVGA  // EX: QVGA/VGA/SVGA...
#define JPEG_QUALITY          8              

// Camera orientation
#define CAM_VFLIP             1
#define CAM_HMIRROR           1

// Soil (RS485 NPK)
#define NPK_BAUD              4800
#define SOIL_UPDATE_INTERVAL_MS 5000UL

// Pin RS485 (DE/RE control) 
#define RS485_DE_PIN          0
#define RS485_RE_PIN          1

// Pin UART for NPK (RX/TX):
#define NPK_RX_PIN            38
#define NPK_TX_PIN            45

// DHT11 & Light
#define DHT_PIN                 14
#define DHT_TYPE                11     
#define ENV_UPDATE_INTERVAL_MS  5000UL 

#define LIGHT_PIN               19     
#define LIGHT_SAMPLES           8       
#define LIGHT_ADC_MAX           4095.0  
#define LIGHT_PERCENT_INVERT    0       // 0: sáng% = raw%, 1: đảo ngược (tùy module)


