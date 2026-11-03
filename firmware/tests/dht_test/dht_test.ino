/*
 * DHT temperature + humidity test - works with a DHT22 (white) OR a DHT11 (blue).
 * It finds out by itself which one you have, then prints a reading every 2 s.
 *
 * Wiring (same for both): DATA -> D4, VCC (+) -> 5V, GND (-) -> GND
 *   bare 4-pin sensor only: 10k resistor from DATA to 5V
 * Serial Monitor: 9600 baud.
 * Library: "DHT sensor library" by Adafruit (+ "Adafruit Unified Sensor")
 *
 * How it tells them apart: both use the same wire but pack the numbers
 * differently. Read a DHT11 as if it were a DHT22 and you get humidity over 100 %.
 * Read a DHT22 as if it were a DHT11 and you get a temperature below 3 C.
 * So we try both and keep the one that makes sense.
 */
#include <DHT.h>

#define PIN_DHT 4
DHT dht22(PIN_DHT, DHT22);
DHT dht11(PIN_DHT, DHT11);

uint8_t model = 0;        // 22 or 11 once found
uint8_t tryNext = 22;
uint8_t failsInRow = 0;
uint16_t good = 0, bad = 0, searchTries = 0;

bool readAs(uint8_t m, float& t, float& h) {
  DHT& s = (m == 11) ? dht11 : dht22;
  t = s.readTemperature(false, true);
  h = s.readHumidity();
  if (isnan(t) || isnan(h) || h > 100.5) return false;
  if (m == 11) return t >= 3 && t <= 60;
  return t > -40 && t < 80;
}

void printReading(float t, float h) {
  Serial.print(F("Temperature: ")); Serial.print(t, 1);
  Serial.print(F(" C    Humidity: ")); Serial.print(h, 1);
  Serial.print(F(" %    (good ")); Serial.print(good);
  Serial.print(F(", failed ")); Serial.print(bad); Serial.println(F(")"));
}

void setup() {
  Serial.begin(9600);
  dht22.begin();
  dht11.begin();
  Serial.println();
  Serial.println(F("DHT test - finding out which sensor is fitted (DHT22 or DHT11)..."));
}

void loop() {
  delay(2100);            // neither sensor can be read faster than this
  float t, h;

  // ---- not found yet: try DHT22, then DHT11, then DHT22 ...
  if (model == 0) {
    if (readAs(tryNext, t, h)) {
      model = tryNext;
      failsInRow = 0;
      good++;
      Serial.println();
      if (model == 22) Serial.println(F(">>> Found a DHT22 (white sensor): 0.1 C steps, -40..80 C"));
      else             Serial.println(F(">>> Found a DHT11 (blue sensor): whole degrees only, 0..50 C"));
      Serial.println(F("Breathe on it: humidity should jump up within a few seconds."));
      Serial.println();
      printReading(t, h);
      return;
    }
    searchTries++;
    Serial.print(F("  not answering as a DHT"));
    Serial.print(tryNext);
    Serial.println(F(" - trying the other type..."));
    if (searchTries % 4 == 0) {
      Serial.println(F("  Still nothing. Check: DATA on D4? VCC on 5V and GND on GND (not swapped)?"));
      Serial.println(F("  Bare 4-pin sensor: pin 1 VCC, pin 2 DATA, pin 3 empty, pin 4 GND + 10k DATA->5V"));
    }
    tryNext = (tryNext == 22) ? 11 : 22;
    return;
  }

  // ---- found: normal readings
  if (readAs(model, t, h)) {
    good++;
    failsInRow = 0;
    printReading(t, h);
    return;
  }
  bad++;
  failsInRow++;
  Serial.print(F("NO ANSWER from the DHT"));
  Serial.print(model);
  Serial.print(F("   (good ")); Serial.print(good);
  Serial.print(F(", failed ")); Serial.print(bad); Serial.println(F(")"));
  Serial.println(F("  Loose wire? Wiggle DATA (D4), VCC and GND one at a time."));
  if (failsInRow >= 5) {   // lost it - start looking again (handles a swapped sensor too)
    Serial.println(F("  Lost the sensor - searching again..."));
    model = 0;
    tryNext = 22;
  }
}
