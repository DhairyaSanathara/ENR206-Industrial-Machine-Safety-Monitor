/*
 * LCD moving letters test - repairs the screen by itself when a wire glitches.
 *
 *   Line 1:  ABCD slides to the RIGHT
 *   Line 2:  EFGH slides to the LEFT
 *
 * Why the screen went blank / showed weird symbols:
 *   when a loose wire breaks contact for a split second, the LCD loses track
 *   of the data it is being sent (it talks in half-bytes, and gets out of step).
 *   Normal code never notices, so the garbage stays. This code:
 *     - checks every 200 ms that the LCD still answers
 *     - when it comes back after a dropout, starts the screen again
 *     - also re-syncs the screen every lap, so garbage clears within ~3 s
 *   The Serial Monitor tells you each time a wire dropped out.
 *
 * Wiring: LCD GND -> GND, VCC -> 5V, SDA -> A4, SCL -> A5
 * Serial Monitor: 9600 baud.
 * Library: "LiquidCrystal I2C" by Frank de Brabander
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const char WORD1[] = "ABCD";
const char WORD2[] = "EFGH";
const uint16_t STEP_MS = 200;     // speed: lower = faster

LiquidCrystal_I2C* lcd = nullptr;
uint8_t  addr = 0;
bool     lost = false;
uint16_t dropouts = 0;
uint32_t step = 0;
uint32_t lastStep = 0, lastReport = 0;

bool answers(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

uint8_t findLcd() {
  if (answers(0x27)) return 0x27;
  if (answers(0x3F)) return 0x3F;
  return 0;
}

// Sends one half-byte straight to the LCD through the I2C backpack
// (backpack pins: P0=RS, P1=RW, P2=EN, P3=backlight, P4-P7=data D4-D7)
void sendNibble(uint8_t n) {
  uint8_t v = (n << 4) | 0x08;          // RS=0, RW=0, backlight on
  Wire.beginTransmission(addr); Wire.write(v | 0x04); Wire.endTransmission();  // EN high
  delayMicroseconds(1);
  Wire.beginTransmission(addr); Wire.write(v);        Wire.endTransmission();  // EN low
}

// Puts the LCD back in step from ANY state (the datasheet's "reset by instruction"),
// without the library's 1-second start delay
void resync() {
  sendNibble(0x3); delay(5);
  sendNibble(0x3); delay(1);
  sendNibble(0x3); delay(1);
  sendNibble(0x2); delay(1);     // now in 4-bit mode, in step
  lcd->command(0x28);            // 4-bit, 2 lines, 5x8 font
  lcd->command(0x0C);            // display on, no cursor
  lcd->command(0x06);            // write left to right
  lcd->home();                   // undo any accidental screen shift
  lcd->backlight();
}

// Draws a 4-letter word on one line at position pos (wraps round the edges)
void drawWord(uint8_t row, const char* word, int pos) {
  char line[17];
  memset(line, ' ', 16);
  line[16] = '\0';
  for (uint8_t i = 0; i < 4; i++) line[(pos + i + 16) % 16] = word[i];
  lcd->setCursor(0, row);
  lcd->print(line);
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(25000, true);   // a loose SDA/SCL can no longer freeze the Arduino
  Serial.println(F("===== MOVING LETTERS TEST STARTED ====="));

  while (!(addr = findLcd())) {
    Serial.println(F("No LCD found (0x27 / 0x3F). Check SDA->A4, SCL->A5, VCC->5V, GND->GND"));
    delay(1000);
  }
  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  Serial.print(F("LCD found at 0x"));
  Serial.println(addr, HEX);
  Serial.println(F("Now gently wiggle each LCD wire. A message appears here for every dropout."));
}

void loop() {
  uint32_t now = millis();

  // Status line once every 5 seconds
  if (now - lastReport >= 5000) {
    lastReport = now;
    Serial.print(F("running "));
    Serial.print(now / 1000);
    Serial.print(F(" s   dropouts: "));
    Serial.println(dropouts);
  }

  if (now - lastStep < STEP_MS) return;
  lastStep = now;

  // 1. Is the LCD still answering?
  if (!answers(addr)) {
    if (!lost) {
      lost = true;
      dropouts++;
      Serial.print(F("!! LCD stopped answering - a wire is loose (dropout "));
      Serial.print(dropouts);
      Serial.println(F(")"));
    }
    return;
  }
  if (lost) {               // it is back: restart the screen cleanly
    lost = false;
    delay(100);             // let the LCD power up fully
    resync();
    lcd->clear();
    Serial.println(F("   LCD back - screen restarted"));
  }

  // 2. Every full lap, re-sync so any garbage clears by itself
  if (step % 16 == 0 && step > 0) {
    resync();
  }

  // 3. Move the letters one place
  drawWord(0, WORD1, step % 16);          // ABCD -> right
  drawWord(1, WORD2, 12 - (int)(step % 16)); // EFGH <- left
  step++;
}
