/*
 * MPU6050 test - accelerometer + gyroscope, read straight from its registers
 * (no library needed - this is exactly how the main program talks to it).
 *
 * Wiring (GY-521 board): VCC -> 5V, GND -> GND, SCL -> A5, SDA -> A4
 *                        AD0 and INT: leave empty
 * Serial Monitor: 9600 baud.
 *
 * What it prints twice a second:
 *   Acc   - acceleration in g on each axis (gravity alone = 1 g, straight down)
 *   Gyro  - how fast it is turning, degrees per second (0 when still)
 *   Tilt  - roll / pitch angles worked out from gravity
 *   Shake - vibration: how much the acceleration jumps around, in milli-g
 *   Chip  - temperature of the MPU6050 chip itself
 */
#include <Wire.h>

uint8_t addr = 0;
bool lost = false;
float gyroOff[3] = { 0, 0, 0 };

// The last 20 readings of total acceleration, to work out the shake
const uint8_t N = 20;
float magBuf[N];
uint8_t magCount = 0;

// ---------------------------------------------------------------- I2C helpers
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;   // no answer
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool answers(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

// Raw readings: ax ay az, temp, gx gy gz (each 16-bit, high byte first)
bool readAll(int16_t* v) {
  uint8_t b[14];
  if (!readRegs(0x3B, b, 14)) return false;
  for (uint8_t i = 0; i < 7; i++) v[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]);
  return true;
}

// ---------------------------------------------------------------- set-up
void setupMPU() {
  writeReg(0x6B, 0x01);   // PWR_MGMT_1: wake up, use the gyro's clock
  delay(10);
  writeReg(0x1A, 0x03);   // CONFIG: low-pass filter 44 Hz (smooths noise)
  writeReg(0x1B, 0x08);   // GYRO_CONFIG:  +-500 deg/s -> 65.5 counts per deg/s
  writeReg(0x1C, 0x08);   // ACCEL_CONFIG: +-4 g       -> 8192 counts per g
  delay(50);
}

void calibrateGyro() {
  Serial.println(F("Keep the sensor STILL for 2 seconds (measuring gyro offset)..."));
  long sum[3] = { 0, 0, 0 };
  int16_t v[7];
  uint8_t got = 0;
  for (uint8_t i = 0; i < 100; i++) {
    if (readAll(v)) { sum[0] += v[4]; sum[1] += v[5]; sum[2] += v[6]; got++; }
    delay(20);
  }
  for (uint8_t k = 0; k < 3; k++) gyroOff[k] = got ? (float)sum[k] / got : 0;
  Serial.println(F("Done. Now move it around!"));
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(25000, true);   // a loose SDA/SCL wire can't freeze the Arduino
  Serial.println();
  Serial.println(F("===== MPU6050 TEST ====="));

  while (true) {
    if (answers(0x68)) { addr = 0x68; break; }
    if (answers(0x69)) { addr = 0x69; break; }
    Serial.println(F("No MPU6050 found at 0x68 / 0x69."));
    Serial.println(F("  Check: SDA -> A4, SCL -> A5 (not swapped), VCC -> 5V, GND -> GND"));
    delay(1500);
  }

  uint8_t who = 0;
  readRegs(0x75, &who, 1);                // WHO_AM_I register
  Serial.print(F("Found MPU6050 at 0x"));
  Serial.print(addr, HEX);
  Serial.print(F(",  chip ID = 0x"));
  Serial.print(who, HEX);
  Serial.println(who == 0x68 ? F("  (genuine)") : F("  (clone chip - works the same)"));

  setupMPU();
  calibrateGyro();
}

// ---------------------------------------------------------------- printing
void printSigned(float x, uint8_t decimals) {
  if (x >= 0) Serial.print('+');
  Serial.print(x, decimals);
}

void loop() {
  static uint32_t lastSample = 0, lastPrint = 0;
  uint32_t now = millis();
  if (now - lastSample < 10) return;      // read 100 times a second
  lastSample = now;

  int16_t v[7];
  if (!readAll(v)) {
    if (!lost) {
      lost = true;
      Serial.println(F("!! MPU6050 stopped answering - a wire is loose (SDA/SCL/VCC/GND)"));
    }
    return;
  }
  if (lost) {                              // it came back: it may have reset, so set it up again
    lost = false;
    setupMPU();
    Serial.println(F("   MPU6050 back"));
  }

  // Convert raw counts to real units
  float ax = v[0] / 8192.0, ay = v[1] / 8192.0, az = v[2] / 8192.0;           // g
  float gx = (v[4] - gyroOff[0]) / 65.5;                                       // deg/s
  float gy = (v[5] - gyroOff[1]) / 65.5;
  float gz = (v[6] - gyroOff[2]) / 65.5;
  float chipC = v[3] / 340.0 + 36.53;                                          // datasheet formula

  // Shake: keep the last 20 values of total acceleration |a|
  float mag = sqrt(ax * ax + ay * ay + az * az);
  magBuf[magCount % N] = mag;
  magCount++;

  if (now - lastPrint < 500) return;      // print twice a second (9600 baud is slow)
  lastPrint = now;

  // Shake = RMS of how far |a| moves away from its average (in milli-g)
  uint8_t n = magCount < N ? magCount : N;
  float mean = 0;
  for (uint8_t i = 0; i < n; i++) mean += magBuf[i];
  mean /= n;
  float var = 0;
  for (uint8_t i = 0; i < n; i++) var += (magBuf[i] - mean) * (magBuf[i] - mean);
  float shakeMg = sqrt(var / n) * 1000.0;

  // Tilt from gravity
  float roll  = atan2(ay, az) * 57.2958;                              // tip left / right
  float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 57.2958;        // nose up / down

  Serial.print(F("Acc g X:")); printSigned(ax, 2);
  Serial.print(F(" Y:"));      printSigned(ay, 2);
  Serial.print(F(" Z:"));      printSigned(az, 2);
  Serial.print(F(" | Gyro deg/s X:")); printSigned(gx, 0);
  Serial.print(F(" Y:"));      printSigned(gy, 0);
  Serial.print(F(" Z:"));      printSigned(gz, 0);
  Serial.print(F(" | Roll:")); printSigned(roll, 0);
  Serial.print(F(" Pitch:"));  printSigned(pitch, 0);
  Serial.print(F(" | Shake:")); Serial.print(shakeMg, 0);
  Serial.print(F(" mg | Chip ")); Serial.print(chipC, 1);
  Serial.println(F(" C"));
}
