#include <Arduino.h>

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
  {true, false, false},  // red
  {false, true, false},  // green
  {false, false, true},  // blue
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

void setup() {
  pinMode(USER_LED_R, OUTPUT);
  pinMode(USER_LED_G, OUTPUT);
  pinMode(USER_LED_B, OUTPUT);
  turnOff();
}

void loop() {
  for (const Color &color : colors) {
    setColor(color);
    delay(COLOR_TIME_MS);
    turnOff();
    delay(DARK_TIME_MS);
  }

}