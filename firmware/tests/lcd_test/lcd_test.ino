/*
 * LCD quick test (16x2 with I2C backpack).
 * Finds the LCD's address by itself, then shows text and a seconds counter.
 * Wiring: GND -> GND, VCC -> 5V, SDA -> A4, SCL -> A5
 * Serial Monitor: 9600 baud.
 * Library: "LiquidCrystal I2C" by Frank de Brabander
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C* lcd = nullptr;
uint8_t lcdAddr = 0;

bool present(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(25000, true);   // never hang if a wire is loose
  Serial.println();
  Serial.println(F("LCD test - scanning the I2C bus (A4/A5)..."));

  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (!present(a)) continue;
    found++;
    Serial.print(F("  found device at 0x"));
    if (a < 16) Serial.print('0');
    Serial.print(a, HEX);
    if ((a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F)) {
      Serial.println(F("  <- LCD backpack"));
      if (!lcdAddr) lcdAddr = a;
    } else if (a == 0x68 || a == 0x69) {
      Serial.println(F("  <- MPU6050"));
    } else {
      Serial.println();
    }
  }

  if (!found) {
    Serial.println(F("NOTHING found on the I2C bus."));
    Serial.println(F("  Check: SDA -> A4, SCL -> A5 (not swapped), VCC -> 5V, GND -> GND."));
    return;
  }
  if (!lcdAddr) {
    Serial.println(F("Something answered, but no LCD address (0x20-0x27 or 0x38-0x3F)."));
    Serial.println(F("  The LCD backpack is not connected or not powered."));
    return;
  }

  lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
  lcd->init();
  lcd->backlight();

  // Flash the backlight twice so you can see the Arduino controls it
  for (uint8_t i = 0; i < 2; i++) {
    delay(300); lcd->noBacklight();
    delay(300); lcd->backlight();
  }

  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(F("LCD OK  addr "));
  lcd->print(lcdAddr, HEX);

  Serial.print(F("LCD started at 0x"));
  Serial.println(lcdAddr, HEX);
  Serial.println(F("Screen should show:  LCD OK  addr ..  and a counter on line 2."));
  Serial.println(F("Backlight on but no letters? Turn the small blue screw on the back slowly."));
}

void loop() {
  if (!lcd) return;
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  lcd->setCursor(0, 1);
  lcd->print(F("Running: "));
  lcd->print(millis() / 1000);
  lcd->print(F(" s   "));
}
