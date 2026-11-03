/*
 * PWM FINDER - which Arduino pin can really control the motor speed?
 *
 * If some pins on your Arduino are damaged, this sketch shows which PWM pin can
 * still control the motor. It drives the
 * SAME changing speed on every PWM-capable pin at once: 5, 6, 10 and 11.
 * You move the ENA wire from pin to pin and watch the motor.
 *
 * BEFORE YOU START (SMPS off):
 *   1. Unplug the HC-SR04 TRIG and ECHO wires from D10 / D11 (this sketch
 *      drives those pins - the sensor must not be connected during the test).
 *   2. L298N: ENA jumper cap OFF. IN1 -> + rail (5V), IN2 -> - rail (GND).
 *   3. Wire: L298N ENA input -> 10k resistor -> a loose jumper wire.
 *   4. Relay IN -> D9 (the sketch switches the relay ON).
 *
 * THEN: switch on, open the Serial Monitor (9600) and plug the loose wire into
 *       D5, watch for 10 s; then D6, D10, D11.
 *
 *   The motor follows  STOP -> SLOW -> FAST  every 8 s   = that pin WORKS
 *   The motor runs at one steady speed all the time       = pin dead (or wrong ENA pin)
 *   The motor never moves                                 = pin dead (stuck LOW)
 *
 * If NO pin works, the wire is on the L298N's 5V pin instead of ENA, or the cap
 * is still on: move it to the other ENA pin and try again.
 */

const uint8_t PINS[] = { 5, 6, 10, 11 };
#define PIN_RELAY 9
const bool RELAY_ACTIVE_LOW = true;     // same setting as in your other sketches

void allPins(uint8_t pwm) {
  for (uint8_t i = 0; i < sizeof(PINS); i++) analogWrite(PINS[i], pwm);
}

void setup() {
  for (uint8_t i = 0; i < sizeof(PINS); i++) pinMode(PINS[i], OUTPUT);
  allPins(0);
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? LOW : HIGH);   // relay ON: 12 V to the L298N
  pinMode(PIN_RELAY, OUTPUT);

  Serial.begin(9600);
  Serial.println(F("PWM FINDER - plug the ENA wire into D5, D6, D10 or D11 and watch the motor"));
  Serial.println(F("The motor should follow: STOP (2 s) -> SLOW (3 s) -> FAST (3 s)"));
}

void loop() {
  Serial.println(F("STOP"));
  allPins(0);
  delay(2000);
  Serial.println(F("  SLOW"));
  allPins(110);
  delay(3000);
  Serial.println(F("    FAST"));
  allPins(255);
  delay(3000);
}
