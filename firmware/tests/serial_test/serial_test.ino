/*
 * SERIAL TEST - is the USB link itself clean?
 * Run it with NOTHING connected to the Arduino except the USB cable.
 * It sends 10 lines a second. Every line should be perfect and the
 * line number should go up by 1 each time, forever.
 *
 * Serial Monitor: set the speed (bottom right) to the same number as BAUD.
 */
#define BAUD 9600        // the Serial Monitor must be set to the same number

unsigned long n = 0;

void setup() {
  Serial.begin(BAUD);
  Serial.println();
  Serial.println(F("===== SERIAL TEST STARTED ====="));
  Serial.println(F("(if you see this line again, the Arduino restarted)"));
}

void loop() {
  n++;
  Serial.print(F("line "));
  Serial.print(n);
  Serial.println(F("  The quick brown fox jumps over the lazy dog 0123456789"));
  delay(100);
}
