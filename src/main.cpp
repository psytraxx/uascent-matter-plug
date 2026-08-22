#include <Arduino.h>
#include <bluefruit.h>

constexpr uint8_t USER_LED_R = LED_RED;
constexpr uint8_t USER_LED_B = LED_BLUE;
constexpr uint8_t USER_LED_G = LED_GREEN;
constexpr bool LED_ACTIVE_LOW = true;
constexpr unsigned long COLOR_TIME_MS = 300;
constexpr unsigned long DARK_TIME_MS = 120;

struct Color {
  bool red;
  bool green;
  bool blue;
};

const Color colors[] = {
  {true, false, false},
  {false, true, false},
  {false, false, true},
};

void setChannel(uint8_t pin, bool on) {
  const bool level = LED_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void setColor(const Color &color) {
  setChannel(USER_LED_R, color.red);
  setChannel(USER_LED_G, color.green);
  setChannel(USER_LED_B, color.blue);
}

void turnOff() {
  setColor({false, false, false});
}

void printAddress(const ble_gap_evt_adv_report_t *report) {
  for (int8_t index = 5; index >= 0; --index) {
    if (report->peer_addr.addr[index] < 0x10) {
      Serial.print('0');
    }
    Serial.print(report->peer_addr.addr[index], HEX);
    if (index > 0) {
      Serial.print(':');
    }
  }
}

void scanCallback(ble_gap_evt_adv_report_t *report) {
  char name[32] = {0};
  const bool hasName = Bluefruit.Scanner.parseReportByType(
      report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
      reinterpret_cast<uint8_t *>(name), sizeof(name) - 1);

  Serial.print("Device: ");
  printAddress(report);
  Serial.print("  RSSI: ");
  Serial.print(report->rssi);
  Serial.print(" dBm  ");
  Serial.print(report->type.scan_response ? "scan response" : "advertisement");
  Serial.print("  Name: ");
  Serial.println(hasName ? name : "(unknown)");

  Bluefruit.Scanner.resume();
}

void setup() {
  pinMode(USER_LED_R, OUTPUT);
  pinMode(USER_LED_G, OUTPUT);
  pinMode(USER_LED_B, OUTPUT);
  turnOff();

  Serial.begin(115200);
  const unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 5000) {
    delay(10);
  }

  Serial.println("BLE scanner starting...");

  Bluefruit.begin(0, 0);
  Bluefruit.setName("XIAO BLE Sniffer");
  Bluefruit.Scanner.setRxCallback(scanCallback);
  Bluefruit.Scanner.setInterval(160, 80);
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.start(0);
}

void loop() {
  for (const Color &color : colors) {
    setColor(color);
    delay(COLOR_TIME_MS);
    turnOff();
    delay(DARK_TIME_MS);
  }
}