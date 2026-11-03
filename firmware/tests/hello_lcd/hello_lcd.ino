/*
 * Prints HELLO continuously on the LCD and in the Serial Monitor.
 * Wiring: LCD GND -> GND, VCC -> 5V, SDA -> A4, SCL -> A5
 * Serial Monitor: 9600 baud.
 * Library: "LiquidCrystal I2C" by Frank de Brabander
 *
 * The count tells you if the Arduino is restarting:
 * if it keeps going back to 1 (and "STARTED" prints again), the board reset.
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C* lcd;
unsigned long count = 0;

void setup() {
  Serial.begin(9600);
  Wire.begin();

  // Most LCDs are at address 0x27, some at 0x3F - use whichever answers
  Wire.beginTransmission(0x27);
  uint8_t addr = (Wire.endTransmission() == 0) ? 0x27 : 0x3F;

  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();

  Serial.println(F("===== STARTED ====="));
}

void loop() {
  count++;

  // LCD line 1: HELLO   line 2: the count
  lcd->setCursor(0, 0);
  lcd->print(F("HELLO           "));
  lcd->setCursor(0, 1);
  lcd->print(F("Count: "));
  lcd->print(count);
  lcd->print(F("     "));

  // Serial Monitor
  Serial.print(F("Hello "));
  Serial.println(count);

  delay(1000);   // once per second
}
