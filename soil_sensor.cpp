#include "metrics_publisher.h"
#include "soil_sensor.h"
#include "config.h"

#include <HardwareSerial.h>

// NPK query
static const uint8_t QUERY_DATA[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
static const size_t  RESPONSE_SIZE = 19;

// Use UART1
HardwareSerial NPKSerial(1);

static uint32_t g_lastSoilRead = 0;

bool soil_sensor_setup() {
  pinMode(RS485_DE_PIN, OUTPUT);
  pinMode(RS485_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);
  digitalWrite(RS485_RE_PIN, LOW);

  NPKSerial.begin(NPK_BAUD, SERIAL_8N1, NPK_RX_PIN, NPK_TX_PIN);
  NPKSerial.setTimeout(300);
  Serial.println("[SOIL] UART ready");
  return true;
}

static void rs485_tx_mode(bool tx) {
  digitalWrite(RS485_DE_PIN, tx ? HIGH : LOW);
  digitalWrite(RS485_RE_PIN, tx ? HIGH : LOW);
}

bool soil_sensor_read(SoilData& data) {
  rs485_tx_mode(true);
  delay(2);
  NPKSerial.flush();
  NPKSerial.write(QUERY_DATA, sizeof(QUERY_DATA));
  NPKSerial.flush();
  rs485_tx_mode(false);

  delay(120);

  // Đọc đủ 19 byte
  if (NPKSerial.available() < (int)RESPONSE_SIZE) {
    Serial.println("[SOIL] timeout/insufficient bytes");
    return false;
  }
  uint8_t resp[RESPONSE_SIZE];
  size_t got = NPKSerial.readBytes(resp, RESPONSE_SIZE);
  if (got != RESPONSE_SIZE) {
    Serial.printf("[SOIL] read %u/%u bytes\n", (unsigned)got, (unsigned)RESPONSE_SIZE);
    return false;
  }

  if (resp[0] != 0x01) {
    Serial.println("[SOIL] invalid device addr");
    return false;
  }

  // Parse by format
  data.humidity    = ((resp[3]  << 8) | resp[4])  / 10.0f;
  data.temperature = ((resp[5]  << 8) | resp[6])  / 10.0f;
  data.ph          = ((resp[9]  << 8) | resp[10]) / 10.0f;
  data.nitrogen    =  (resp[11] << 8) | resp[12];
  data.phosphorus  =  (resp[13] << 8) | resp[14];
  data.potassium   =  (resp[15] << 8) | resp[16];

  return true;
}

void soil_sensor_loop() {
  const uint32_t now = millis();
  if (now - g_lastSoilRead < SOIL_UPDATE_INTERVAL_MS) return;
  g_lastSoilRead = now;

  SoilData d;
  if (soil_sensor_read(d)) {
    Serial.printf("[SOIL] T=%.2fC, H=%.2f%%, pH=%.2f, N=%u, P=%u, K=%u\n", d.temperature, d.humidity, d.ph, d.nitrogen, d.phosphorus, d.potassium);
    metrics_set_soil(d); 
  } else {
    Serial.println("[SOIL] read FAILED");
  }
}
