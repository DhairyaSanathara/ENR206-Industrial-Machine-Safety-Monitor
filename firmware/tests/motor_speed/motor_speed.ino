/*
 * MOTOR SPEED FROM THE SERIAL MONITOR
 * Built on pwm_finder.ino (the sketch that worked on this board), same wiring:
 *
 *   Relay IN -> D9          (switches the 12 V to the L298N)
 *   L298N ENA (jumper cap OFF) -> 10k resistor -> D5, D6, D10 or D11
 *   L298N IN1 -> + rail (5V), IN2 -> - rail (GND)   (fixed direction)
 *   E-STOP button (optional): D3 <-> - rail
 *
 * The same speed signal goes out on all four pins 5, 6, 10 and 11, so it works
 * whichever of them your ENA wire is on.
 * IMPORTANT: nothing else may be plugged into D5, D6, D10 or D11 while this runs
 * (keep the HC-SR04 unplugged for this test).
 *
 * SERIAL MONITOR (9600 baud, any line ending):
 *   0 - 100 then Enter   speed in %      (0 = stop, then the relay switches off)
 *   +  /  -              speed +10 % / -10 %
 *   x                    stop at once
 */

const uint8_t PINS[] = { 5, 6, 10, 11 };
#define PIN_RELAY 9
#define PIN_ESTOP 3

const bool    RELAY_ACTIVE_LOW = true;  // same setting as pwm_finder
const uint8_t PWM_MIN          = 70;    // 1 % = this PWM. Motor only hums at low %? raise it
const uint8_t RAMP_STEP        = 5;     // PWM change every 10 ms -> 0 to full in ~0.5 s
const uint16_t RELAY_SETTLE_MS = 150;   // relay closes before the motor is driven
const uint16_t RELAY_OFF_MS    = 400;   // after stopping, wait, then cut the 12 V

uint8_t  targetPct = 0;     // what you asked for
uint8_t  pwmNow = 0;        // what the pins put out now
bool     relayOn = false;
uint32_t relayOnAt = 0, zeroSince = 0, lastStep = 0;
int16_t  numIn = -1;        // number being typed
uint32_t numInMs = 0;

// ------------------------------------------------------------------ helpers
uint8_t pctToPwm(uint8_t pct) {
  if (pct == 0) return 0;
  return PWM_MIN + (uint16_t)(255 - PWM_MIN) * pct / 100;
}

void allPins(uint8_t pwm) {
  for (uint8_t i = 0; i < sizeof(PINS); i++) analogWrite(PINS[i], pwm);
}

void setRelay(bool on) {
  relayOn = on;
  digitalWrite(PIN_RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
  if (on) relayOnAt = millis();
}

bool estopPressed() { return digitalRead(PIN_ESTOP) == LOW; }

void stopNow(const __FlashStringHelper* why) {
  targetPct = 0;
  pwmNow = 0;
  allPins(0);
  setRelay(false);
  Serial.print(F("STOPPED - "));
  Serial.println(why);
}

void setSpeed(int16_t pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (pct > 0 && estopPressed()) { Serial.println(F("Release the E-STOP button first")); return; }
  targetPct = pct;
  Serial.print(F("> speed "));
  Serial.print(pct);
  Serial.println(pct ? F(" %") : F(" % (stopping)"));
}

void printSpeed() {
  Serial.print(F("  running at "));
  Serial.print(targetPct);
  Serial.print(F(" %   (PWM "));
  Serial.print(pwmNow);
  Serial.print(F("/255, about "));
  Serial.print(10.0 * pwmNow / 255, 1);      // ~10 V usable after the L298N's ~2 V drop
  Serial.println(F(" V average at the motor)"));
}

// ------------------------------------------------------------------ serial input
void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      numIn = (numIn < 0 ? 0 : numIn) * 10 + (c - '0');
      if (numIn > 999) numIn = 999;
      numInMs = millis();
      continue;
    }
    if (numIn >= 0) { setSpeed(numIn); numIn = -1; }   // Enter ends the number
    if (c == '+') setSpeed(targetPct + 10);
    if (c == '-') setSpeed(targetPct < 10 ? 0 : targetPct - 10);
    if (c == 'x' || c == 'X') stopNow(F("x typed"));
  }
  // "No line ending" in the Serial Monitor: take the number after a short pause
  if (numIn >= 0 && millis() - numInMs > 500) { setSpeed(numIn); numIn = -1; }
}

// ------------------------------------------------------------------ motor
void motorTask() {
  uint32_t now = millis();
  if (now - lastStep < 10) return;
  lastStep = now;

  uint8_t want = pctToPwm(targetPct);
  if (want > 0 && !relayOn) {                // 1. power first
    setRelay(true);
    Serial.println(F("  relay ON - 12 V to the driver"));
    return;
  }
  if (relayOn && now - relayOnAt < RELAY_SETTLE_MS) return;   // 2. let it close

  uint8_t before = pwmNow;                   // 3. ramp smoothly to the new speed
  if (pwmNow < want)      pwmNow = (want - pwmNow > RAMP_STEP) ? pwmNow + RAMP_STEP : want;
  else if (pwmNow > want) pwmNow = (pwmNow - want > RAMP_STEP) ? pwmNow - RAMP_STEP : want;
  allPins(pwmNow);
  if (before != pwmNow && pwmNow == want && want > 0) printSpeed();

  if (pwmNow == 0) {                         // 4. stopped: cut the power after a moment
    if (before != 0) zeroSince = now;
    if (want == 0 && relayOn && now - zeroSince >= RELAY_OFF_MS) {
      setRelay(false);
      Serial.println(F("  motor stopped - relay OFF"));
    }
  }
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  for (uint8_t i = 0; i < sizeof(PINS); i++) pinMode(PINS[i], OUTPUT);
  allPins(0);
  setRelay(false);                           // relay OFF before anything else
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_ESTOP, INPUT_PULLUP);

  Serial.begin(9600);
  Serial.println();
  Serial.println(F("MOTOR SPEED - type a speed 0-100 and press Enter (0 = stop)"));
  Serial.println(F("              + / - = +-10 %     x = stop at once"));
}

void loop() {
  if (relayOn && estopPressed()) stopNow(F("E-STOP button"));
  readSerial();
  motorTask();
}
