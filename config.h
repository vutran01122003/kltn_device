#pragma once
#include <Arduino.h>

// WiFi & Upload
#define WIFI_SSID             "P001"
#define WIFI_PASS             "99990000"


// Upload
#define UPLOAD_URL            "http://192.168.0.102:3000/upload"
#define UPLOAD_INTERVAL_MS    10000UL 
#define METRICS_URL             "http://192.168.0.102:3000/readings"  
#define METRICS_INTERVAL_MS     5000UL   

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