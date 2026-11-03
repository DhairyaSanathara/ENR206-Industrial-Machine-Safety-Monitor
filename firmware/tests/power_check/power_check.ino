/*
 * POWER CHECK - watches the Arduino's own 5V supply, no extra wires needed.
 *
 * The ATmega328P has a fixed 1.1 V reference inside it. Measuring that 1.1 V
 * against the 5V supply tells us what the supply is:  Vcc = 1.1 V x 1023 / reading
 * It checks about 5000 times a second, so it catches even very short drops.
 *
 * Use it to find a loose or shorting wire:
 *   leave everything connected, open the Serial Monitor (9600),
 *   then wiggle one wire at a time. A "!! 5V DROPPED" line means the wire
 *   you just touched is the problem. If "POWER CHECK STARTED" appears again,
 *   the supply dropped so far that the Arduino restarted.
 *
 * The number can be off by about +-0.3 V (the 1.1 V reference is not exact).
 * What matters is a SUDDEN DROP, not the exact value.
 */

uint16_t readVccMv() {
  ADCSRA |= _BV(ADSC);                      // start one measurement
  while (bit_is_set(ADCSRA, ADSC)) { }      // ~0.1 ms
  uint16_t raw = ADC;
  if (raw == 0) return 0;
  return (uint16_t)(1125300UL / raw);       // 1.1 V x 1023 x 1000 = 1125300
}

uint16_t normalMv = 0;       // the usual supply voltage, learned at start
uint16_t lowestEver = 65535;
uint32_t dips = 0;

void setup() {
  Serial.begin(9600);
  // ADC: reference = 5V supply (AVcc), input = internal 1.1 V
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
  delay(10);                                // let the 1.1 V reference settle
  readVccMv();                              // first reading is thrown away

  uint32_t sum = 0;
  for (uint8_t i = 0; i < 64; i++) sum += readVccMv();
  normalMv = sum / 64;

  Serial.println();
  Serial.println(F("===== POWER CHECK STARTED ====="));
  Serial.println(F("(if you see this line again, the Arduino restarted = power cut out)"));
  Serial.print(F("Normal 5V supply: "));
  Serial.print(normalMv / 1000.0, 2);
  Serial.println(F(" V   - now wiggle one wire at a time"));
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint16_t lowest = 65535;
  static bool inDip = false;

  uint16_t mv = readVccMv();
  if (mv < lowest) lowest = mv;
  if (mv < lowestEver) lowestEver = mv;

  // a drop of more than 0.25 V below normal = something pulled the supply down
  if (mv + 250 < normalMv) {
    if (!inDip) {
      inDip = true;
      dips++;
      Serial.print(F("!! 5V DROPPED to "));
      Serial.print(mv / 1000.0, 2);
      Serial.print(F(" V  (drop number "));
      Serial.print(dips);
      Serial.println(F(") <- whatever you just touched is the problem"));
    }
  } else if (mv + 100 >= normalMv) {
    inDip = false;
  }

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print(F("5V: "));
    Serial.print(mv / 1000.0, 2);
    Serial.print(F(" V   lowest this second: "));
    Serial.print(lowest / 1000.0, 2);
    Serial.print(F(" V   drops so far: "));
    Serial.print(dips);
    Serial.print(F("   running "));
    Serial.print(millis() / 1000);
    Serial.println(F(" s"));
    lowest = 65535;
  }
}
