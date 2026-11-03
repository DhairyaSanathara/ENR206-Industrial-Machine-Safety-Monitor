/*
 * ===========================================================================
 *   SMART MACHINE SAFETY MONITOR  -  v2.1
 *   Board : Arduino Uno R3        Serial : 9600 baud        Power : 12 V / 5 V SMPS
 * ===========================================================================
 *
 *  WHAT IT DOES
 *  A 12 V DC gear motor is our "factory machine". Three sensors watch it, and
 *  ten times a second the Arduino decides one of three things:
 *     SAFE     -> green LED, motor at the speed you set from the laptop
 *     WARNING  -> yellow LED, motor slows to 45 %, a beep every second
 *     E-STOP   -> red LED, relay cuts the motor's 12 V, alarm, and the machine
 *                 stays LOCKED until the danger is gone AND a person presses GO
 *
 *  SENSORS AND WHAT WE CALCULATE
 *     HC-SR04  distance to a person (median of 3 pings)          -> safety zone
 *     DHT22 or DHT11 (found automatically) air temperature at the motor
 *              + rise rate (slope of a straight line through ~30 s) -> overheat / fire
 *              + humidity (shown and logged only)
 *     MPU6050  vibration RMS in milli-g (100 samples/s, 0.5 s windows) -> imbalance,
 *              tilt from the installed position                       loose mount,
 *              vibration that COLLAPSES at a steady speed             knocked over,
 *                                                                     stall / jam
 *  Vibration limits are LEARNED: the first time the machine runs at a speed
 *  after power-up, it measures the healthy machine for ~4 s and sets its limits
 *  as multiples of that baseline. Each speed (10 % steps) has its own baseline,
 *  because a motor shakes less at 40 % than at 100 %. Baselines are KEPT, so a
 *  fault that appears later is caught. 'l' forgets them and learns again.
 *
 *  SAFETY LAYERS
 *     1. Relay is "energise to run": if the Arduino resets, loses power or a
 *        wire breaks, the relay drops out and the motor stops.
 *     2. E-STOP button on a hardware interrupt: the motor is cut within
 *        microseconds, whatever the program is doing.
 *     3. Watchdog timer: if the code ever freezes, the chip resets in 2 s (-> 1).
 *     4. Latching E-stop with "first-out" reason: never restarts by itself.
 *     5. No auto-start at power-up; GO is refused while a danger is present.
 *     6. Every sensor is checked at start-up and while running: a lost sensor
 *        stops the machine (FAULT / E-STOP), a jammed I2C bus cannot freeze it.
 *
 *  FINAL WIRING  (do all wiring with the SMPS switched OFF)
 *   Power  SMPS L / N / earth  <- mains cable (3-pin plug, earth on the metal case)
 *          SMPS +12V -> relay COM  and  Arduino VIN
 *          SMPS +5V  -> breadboard + rail          (sensors, LCD, relay)
 *          SMPS COM  -> breadboard - rail  and  L298N GND
 *          Arduino GND -> breadboard - rail        Arduino 5V pin: NOT used
 *   Relay  VCC -> + rail, GND -> - rail, IN -> D9, COM <- SMPS +12V, NO -> L298N +12V
 *   L298N  +12V <- relay NO, GND <- SMPS COM, 5V terminal: empty (12V-EN jumper ON)
 *          ENA (jumper cap OFF): ENA input pin -> 10k resistor -> D5 or D6
 *          (the speed signal is sent on BOTH D5 and D6 - either one works)
 *          IN1 -> + rail, IN2 -> - rail (fixed direction), OUT1/OUT2 -> motor
 *          (100 nF ceramic capacitor across the motor terminals)
 *   HC-SR04  VCC -> +, GND -> -, TRIG -> D11, ECHO -> D10
 *   DHT      + -> +, - -> -, DATA -> D4   (bare 4-pin sensor: 10k DATA -> +)
 *   MPU6050  VCC -> +, GND -> -, SCL -> A5, SDA -> A4   (XDA/XCL/AD0/INT empty)
 *            Fix it to the SAME base as the motor, or it cannot feel the motor.
 *   LCD      VCC -> +, GND -> -, SCL -> A5, SDA -> A4
 *   Buttons  GO: D2 <-> - rail        E-STOP: D3 <-> - rail
 *   LEDs     A0 green / A1 yellow / A2 red -> 220R -> LED long leg, short leg -> -
 *   Buzzer   + -> D8, - -> - rail
 *   Not used: D0, D1 (USB serial), D7, D12, D13, A3
 *
 *  SERIAL MONITOR, 9600 baud, any line ending
 *     g          GO: start, or reset after a stop
 *     30 / 60 / 100 + Enter   motor speed in % (10..100, steps of 10)
 *     + / -      speed +-10 %          x   emergency stop
 *     s          status in plain words z   re-zero tilt
 *     l          re-learn baselines    ?   help
 *  OUTPUT     n = readable labelled report every 2 s (default, for the Serial Monitor)
 *             d = CSV data every 0.5 s (for the website in web/ - it sends d itself)
 *  GRAPHS (Tools -> Serial Plotter, 9600; type into the plotter's message box)
 *     v = vibration graph   t = temperature graph   b = both   n = back to text
 *  Data mode (d): H,... column names  L,... limits  D,... row every 0.5 s
 *                 E,... events (state changes, speed, baseline...)  #... notes
 *
 *  LIBRARIES (Arduino IDE -> Tools -> Manage Libraries)
 *     "LiquidCrystal I2C" by Frank de Brabander
 *     "DHT sensor library" by Adafruit (say YES to "Adafruit Unified Sensor")
 *  The MPU6050 (and an optional INA219 current sensor) are read straight from
 *  their registers, so they need no library.
 *  Optional extras, found automatically if fitted: INA219 (motor current, power,
 *  overload) and a 12 V sense divider on A3 (10k / 4.7k, detects a welded relay).
 * ===========================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================== 1. PIN MAP ================================
#define PIN_BTN_GO      2   // green push button -> GND   (START / RESET)
#define PIN_BTN_ESTOP   3   // red push button   -> GND   (EMERGENCY STOP, interrupt pin)
#define PIN_DHT         4   // DHT22 or DHT11 DATA
#define PIN_BUZZER      8   // active buzzer (+)
#define PIN_RELAY       9   // relay module IN  (9 or 12 - the pin your relay wire is on)
#define PIN_LED_GREEN  A0   // analog pins work as normal digital outputs too
#define PIN_LED_YELLOW A1
#define PIN_LED_RED    A2
#define PIN_12V_SENSE  A3   // OPTIONAL 12 V sense divider (10k from L298N +12V, 4.7k to GND)
// I2C bus: SDA = A4, SCL = A5 -> LCD (0x27 or 0x3F), MPU6050 (0x68), INA219 (0x40)

// Motor: the machine only ever turns ONE way, so the L298N's direction inputs are
// wired straight to the breadboard (IN1 -> + rail, IN2 -> - rail). The Arduino
// controls only POWER (relay, D9) and SPEED (PWM on ENA).
// ENA_PIN = the pin that drives ENA: 5, 6, 10 or 11 (pwm_finder.ino shows which
// ones work on a damaged board). The ultrasonic moves out of the way by itself:
//   ENA 5 or 6 : TRIG D11, ECHO D10   (normal)
//   ENA 10     : TRIG D11, ECHO A3
//   ENA 11     : TRIG D13, ECHO D10
#define ENA_PIN         5
#define PIN_MOTOR_EN    ENA_PIN
// The same speed signal is ALSO sent on the other one of D5 / D6, so ENA works on
// either pin (like pwm_finder) - useful on a board with damaged pins.
#if ENA_PIN == 5
  #define PIN_MOTOR_EN2 6
#elif ENA_PIN == 6
  #define PIN_MOTOR_EN2 5
#endif
#if ENA_PIN == 10
  #define PIN_US_ECHO  A3
#else
  #define PIN_US_ECHO  10   // HC-SR04 ECHO
#endif
#if ENA_PIN == 11
  #define PIN_US_TRIG  13
#else
  #define PIN_US_TRIG  11   // HC-SR04 TRIG
#endif

// ============================ 2. HARDWARE OPTIONS ==========================
const bool RELAY_ACTIVE_LOW   = true;   // blue 1-channel modules with optocoupler: ON = LOW
const bool BUZZER_ACTIVE_LOW  = false;  // true only for "low level trigger" buzzer modules
const bool ESTOP_BUTTON_IS_NC = false;  // true if you fit a real mushroom E-stop (NC contact)
const bool USE_WATCHDOG       = true;   // keep true on an Uno R3
const uint32_t SERIAL_BAUD    = 9600;   // Serial Monitor must be set to the same
// Optional extras. Set to 1 only if you fit them (they cost program memory):
#define HAS_INA219      0               // INA219 current sensor on I2C (0x40)
#define HAS_12V_DIVIDER 0               // 10k / 4.7k divider from L298N +12V to A3
const bool USE_12V_SENSE      = HAS_12V_DIVIDER && (ENA_PIN != 10);
const float SENSE_RATIO       = (10.0 + 4.7) / 4.7;   // divider: 12 V becomes 3.84 V at A3
const float SHUNT_OHMS        = 0.1;    // only for the OPTIONAL INA219 (shunt marked R100)

// ============================== 3. SAFETY LIMITS ===========================
// Proximity, cm (smaller = more dangerous)
const float DIST_WARN_CM    = 40.0;
const float DIST_STOP_CM    = 15.0;
const float DIST_HYST_CM    = 5.0;     // must move 5 cm further away to clear a warning
const float DIST_MAX_CM     = 150.0;   // no echo / farther = "zone clear"
// Air temperature right next to the motor (DHT22 or DHT11), deg C
const float TEMP_WARN_C     = 40.0;
const float TEMP_STOP_C     = 50.0;
const float TEMP_HYST_C     = 2.0;
// Temperature rise rate, deg C per minute
const float TRATE_WARN      = 6.0;
const float TRATE_STOP      = 15.0;
const float TRATE_HYST      = 1.5;
// Vibration RMS, milli-g. Limits = learned baseline x factor
const float VIB_WARN_FACTOR = 2.0;
const float VIB_STOP_FACTOR = 3.5;
const float VIB_FLOOR_MG    = 40.0;    // never warn below this, even if baseline is tiny
const float VIB_ABS_STOP_MG = 800.0;   // always stop above this
// Tilt from installed position, degrees
const float TILT_WARN_DEG   = 10.0;
const float TILT_STOP_DEG   = 25.0;
const float TILT_HYST_DEG   = 2.0;
// Motor current, mA. Limits = learned baseline x factor, capped
const float CUR_WARN_FACTOR = 1.6;
const float CUR_STOP_FACTOR = 2.5;
const float CUR_FLOOR_MA    = 150.0;
const float CUR_ABS_STOP_MA = 1500.0;  // protects the L298N (2 A max)
const float BUS_MIN_V       = 8.0;     // below this while running = motor has no power
// Stall / jam: at a steady speed a turning motor always vibrates. If the vibration
// collapses below 35% of that speed's healthy baseline, the shaft has stopped.
const float STALL_FACTOR      = 0.35;
const float STALL_MIN_BASE_MG = 15.0;  // only check if the healthy motor clearly vibrates
// Motor speed. You set it in % from the laptop; the code turns % into PWM 0..255:
//   speed %  ->  PWM = PWM_MIN + (255 - PWM_MIN) x % / 100     (100 % = 255)
// (a geared motor only hums below ~25 % duty, so the % scale starts where it turns)
const uint8_t PWM_MIN        = 70;     // raise to 80-90 if your motor won't turn at 10 %
const uint8_t SPEED_DEFAULT  = 100;    // speed % after power-up
const uint8_t SPEED_MIN_PCT  = 10;
const uint8_t WARN_SPEED_PCT = 45;     // WARNING slows the motor to this (or keeps it lower)
const uint8_t RAMP_STEP      = 5;      // soft start: +5 PWM every 10 ms = 0.5 s to full speed

// ================================ 4. TIMING ================================
const uint16_t SAMPLE_US        = 10000;  // MPU6050 read every 10 ms = 100 Hz
const uint8_t  VIB_WINDOW       = 50;     // 50 samples = one vibration result per 0.5 s
const uint16_t PING_MS          = 70;     // HC-SR04 needs > 60 ms between pings
const uint16_t ECHO_TIMEOUT_US  = 10000;  // ~1.6 m max, so a ping never blocks long
const uint16_t TEMP_MS          = 2100;   // DHT22 can only measure once every 2 s
const uint8_t  TREND_N          = 15;     // temperature trend over the last ~30 s
const uint16_t INA_MS           = 100;
const uint16_t LOGIC_MS         = 100;    // decide the state 10x per second
const uint16_t LOG_MS           = 500;    // dashboard data row (d mode): 2x per second
const uint16_t TEXT_MS          = 2000;   // readable report in the Serial Monitor: every 2 s
const uint16_t LCD_MS           = 250;
const uint16_t LCD_PAGE_MS      = 2000;
const uint16_t DEBOUNCE_MS      = 25;
const uint16_t RELAY_SETTLE_MS  = 150;    // let relay contacts close before driving the motor
const uint16_t SPINUP_MS        = 2000;   // ignore start-up current/vibration
const uint8_t  LEARN_WINDOWS    = 8;      // 8 x 0.5 s = 4 s of baseline learning
const uint16_t WARN_DWELL_MS    = 3000;   // stay in WARNING until clear for 3 s
const uint8_t  VIB_STOP_WINDOWS = 2;      // vibration must be too high for 1 s to trip
const uint8_t  CUR_STOP_READS   = 3;      // current must be too high for 0.3 s to trip
const uint8_t  STALL_WINDOWS    = 3;      // vibration must stay collapsed for 1.5 s
const float    ACC_LSB_PER_G    = 8192.0; // MPU6050 at +/-4 g

// ========================= 5. STATES AND REASONS ===========================
enum : uint8_t { ST_IDLE, ST_RUN, ST_WARN, ST_ESTOP, ST_FAULT };

// Reasons in priority order: when two happen together, the lower number is shown
enum : uint8_t {
  R_NONE, R_ESTOP_BTN, R_REMOTE, R_RELAY, R_NOPOWER,
  R_SENS_TEMP, R_SENS_MPU, R_SENS_INA,
  R_PROX, R_CURRENT, R_STALL, R_TEMP, R_TRATE, R_VIB, R_TILT,
  R_COUNT
};

const char SN0[] PROGMEM = "IDLE";
const char SN1[] PROGMEM = "RUN";
const char SN2[] PROGMEM = "WARNING";
const char SN3[] PROGMEM = "ESTOP";
const char SN4[] PROGMEM = "FAULT";
const char* const STATE_NAME[] PROGMEM = { SN0, SN1, SN2, SN3, SN4 };

// LCD text (max 16 characters)
const char RT0[]  PROGMEM = "ALL OK";
const char RT1[]  PROGMEM = "E-STOP BUTTON";
const char RT2[]  PROGMEM = "REMOTE STOP";
const char RT3[]  PROGMEM = "RELAY STUCK ON";
const char RT4[]  PROGMEM = "NO MOTOR POWER";
const char RT5[]  PROGMEM = "TEMP SENSOR ERR";
const char RT6[]  PROGMEM = "MPU6050 ERROR";
const char RT7[]  PROGMEM = "INA219 ERROR";
const char RT8[]  PROGMEM = "PERSON IN ZONE";
const char RT9[]  PROGMEM = "OVERLOAD / JAM";
const char RT10[] PROGMEM = "MOTOR STALLED";
const char RT11[] PROGMEM = "MOTOR OVERHEAT";
const char RT12[] PROGMEM = "TEMP RISING FAST";
const char RT13[] PROGMEM = "HIGH VIBRATION";
const char RT14[] PROGMEM = "MACHINE TILTED";
const char* const REASON_TEXT[] PROGMEM = {
  RT0, RT1, RT2, RT3, RT4, RT5, RT6, RT7, RT8, RT9, RT10, RT11, RT12, RT13, RT14 };

// Short codes for the CSV log (no spaces, easy for software to read)
const char RC0[]  PROGMEM = "OK";
const char RC1[]  PROGMEM = "ESTOP_BTN";
const char RC2[]  PROGMEM = "REMOTE";
const char RC3[]  PROGMEM = "RELAY_STUCK";
const char RC4[]  PROGMEM = "NO_POWER";
const char RC5[]  PROGMEM = "SENS_TEMP";
const char RC6[]  PROGMEM = "SENS_MPU";
const char RC7[]  PROGMEM = "SENS_INA";
const char RC8[]  PROGMEM = "PROX";
const char RC9[]  PROGMEM = "OVERLOAD";
const char RC10[] PROGMEM = "STALL";
const char RC11[] PROGMEM = "OVERHEAT";
const char RC12[] PROGMEM = "TEMP_RATE";
const char RC13[] PROGMEM = "VIBRATION";
const char RC14[] PROGMEM = "TILT";
const char* const REASON_CODE[] PROGMEM = {
  RC0, RC1, RC2, RC3, RC4, RC5, RC6, RC7, RC8, RC9, RC10, RC11, RC12, RC13, RC14 };

// =============================== 6. GLOBALS ================================
// All text output goes through LOG. While a graph is shown in the Serial
// Plotter, LOG is switched to "nowhere", so only graph values reach the plotter.
class NullOut : public Print {
 public:
  size_t write(uint8_t) { return 1; }
};
NullOut nullOut;
Print* logOut = &Serial;
#define LOG (*logOut)

// What the serial port sends:
//   readable text (default) - a labelled report every 2 s, for the Serial Monitor
//   data (d)                - one CSV row every 0.5 s, for the dashboard / Excel
//   graphs (v / t / b)      - numbers only, for the IDE's Serial Plotter
#ifndef OUTPUT_DEFAULT_TEXT
#define OUTPUT_DEFAULT_TEXT 1
#endif
bool     textMode = OUTPUT_DEFAULT_TEXT;
uint8_t  plotMode = 0;          // 0 = off, 1 = vibration, 2 = temperature, 3 = both
uint32_t tPlot = 0;
LiquidCrystal_I2C* lcd = NULL;            // created only if an LCD is found
uint8_t lcdAddr = 0;
bool     lcdBusOk = true;                 // false = I2C bus jammed (loose / unpowered module)
uint32_t lcdRetryMs = 0, lcdResyncMs = 0;
char lcdCache[2][17];                     // what is on the screen right now
char lcdWant[2][17];                      // what we want on the screen

// Same wire, two ways of talking - the self-test finds out which sensor is fitted
DHT dht22(PIN_DHT, DHT22);
DHT dht11(PIN_DHT, DHT11);
DHT* dht = &dht22;
uint8_t dhtModel = 0;          // 22 or 11 once found

uint8_t mpuAddr = 0x68;
const uint8_t INA_ADDR = 0x40;

// Sensor health
bool mpuOk = false, tempOk = false;                   // found at self-test
#if HAS_INA219
bool inaOk = false;                                   // INA219 found
#else
const bool inaOk = false;                             // not fitted: its code is left out
#endif
#if HAS_12V_DIVIDER
bool senseOk = false;                                 // 12 V divider found on A3
#else
const bool senseOk = false;
#endif
bool mpuFail = false, tempFail = false, inaFail = false; // lost while running
uint8_t mpuErrCount = 0, mpuStuckCount = 0, tempBadCount = 0, inaErrCount = 0;

// Measurements
float distCm = DIST_MAX_CM;
float distRaw[3] = { DIST_MAX_CM, DIST_MAX_CM, DIST_MAX_CM };
uint8_t distIdx = 0;

float tempC = NAN;
float humidity = NAN;          // % relative humidity (bonus reading)
float tempRate = 0;            // deg C per minute
bool  tempRateValid = false;
float tBuf[TREND_N];
uint8_t tHead = 0, tCount = 0;

float vibMg = 0, tiltDeg = 0;
float accOff[3] = { 0, 0, 0 };  // last window's mean, subtracted before squaring
float accSum[3], accSq[3];
uint8_t nSamples = 0;
float tiltRef[3];
bool tiltRefSet = false, rezeroTilt = false;

float currentMa = 0, busV = 0, powerW = 0;

// Learned baselines and the limits currently in force
bool  baselineReady = false;
float vibBase = 0, curBase = 0;
float learnVibSum = 0, learnCurSum = 0;
uint8_t learnVibN = 0;
uint16_t learnCurN = 0;
float vibWarn = VIB_ABS_STOP_MG, vibStop = VIB_ABS_STOP_MG;
float curWarn = CUR_ABS_STOP_MA, curStop = CUR_ABS_STOP_MA;

// Decision logic
uint8_t state = ST_IDLE;
uint8_t lvl[R_COUNT];          // 0 = OK, 1 = WARNING, 2 = STOP, for every check
uint8_t worstLevel = 0, worstReason = R_NONE, latchedReason = R_NONE;
uint8_t vibStopCount = 0, curStopCount = 0, powerBadCount = 0, relayBadCount = 0, stallCount = 0;
uint32_t runStartMs = 0, relayChangeMs = 0, lastWarnMs = 0;
bool relayOn = false;
uint8_t pwmNow = 0;
volatile bool estopIrq = false;   // set by the E-stop interrupt, cleared by loop()
                                  // "volatile" = always read it from memory, because it
                                  // can change between any two instructions
bool goRequest = false;

// Speed set from the laptop, and one learned baseline per speed (10 % steps)
uint8_t  speedPct = SPEED_DEFAULT;
uint32_t speedChangeMs = 0;
float    baseVib[11], baseCur[11];
uint16_t baseKnown = 0;          // bit n set = baseline for n x 10 % is learned
uint16_t estopCount = 0;         // E-stops since power-up (shown on the LCD)
int16_t  numIn = -1;             // a number being typed in the Serial Monitor
uint32_t numInMs = 0;

// The data row is sent in pieces, so the loop never waits for the serial port
char     rowBuf[260];
uint8_t  rowLen = 0, rowPos = 0;
void rowPump();                  // (defined in section 17)
void rowFinish();
void printDataHeader();

// Buttons
struct Button {
  uint8_t pin; bool nc; bool stable; bool last; uint32_t changedMs; bool pressedEvent;
};
Button btnGo   = { PIN_BTN_GO,    false,              false, false, 0, false };
Button btnStop = { PIN_BTN_ESTOP, ESTOP_BUTTON_IS_NC, false, false, 0, false };

// Task timers
uint32_t tSampleUs = 0, tPing = 0, tTemp = 0, tIna = 0, tLogic = 0, tLog = 0,
         tLcd = 0, tLcdPage = 0, tMotor = 0, beepUntil = 0;
uint8_t lcdPage = 0;

// ========================= 7. SMALL HELPERS ================================
void pgmText(const char* const* table, uint8_t i, char* out, uint8_t len) {
  strncpy_P(out, (PGM_P)pgm_read_word(&table[i]), len - 1);
  out[len - 1] = 0;
}

void printPgm(const char* const* table, uint8_t i) {
  char buf[20];
  pgmText(table, i, buf, sizeof buf);
  LOG.print(buf);
}

bool isRunning() { return state == ST_RUN || state == ST_WARN; }

// After a start or a speed change the motor needs a moment to settle
bool motorChecksActive(uint32_t now) {
  return isRunning() && (now - runStartMs >= SPINUP_MS) && (now - speedChangeMs >= SPINUP_MS);
}

uint8_t pctToPwm(uint8_t pct) { return PWM_MIN + (uint16_t)(255 - PWM_MIN) * pct / 100; }
uint8_t pwmToPct(uint8_t pwm) {
  if (pwm <= PWM_MIN) return 0;
  return ((uint16_t)(pwm - PWM_MIN) * 100 + (255 - PWM_MIN) / 2) / (255 - PWM_MIN);
}
uint8_t runPwm()  { return pctToPwm(speedPct); }
uint8_t warnPwm() { return pctToPwm(speedPct < WARN_SPEED_PCT ? speedPct : WARN_SPEED_PCT); }

// Speed signal to the L298N ENA (on both D5 and D6, see the pin map)
void motorPwm(uint8_t v) {
  analogWrite(PIN_MOTOR_EN, v);
#ifdef PIN_MOTOR_EN2
  analogWrite(PIN_MOTOR_EN2, v);
#endif
}

// --- Race-free outputs (see section 15b) ---------------------------------
// The E-stop interrupt can switch the motor off between ANY two instructions.
// If the main code checked "is the E-stop flag set?" and then switched the motor
// on, an interrupt landing between the check and the switch-on would be undone.
// So "check the flag + switch on" runs with interrupts paused: one unbreakable step.
// (It takes a few microseconds; a pending interrupt runs right after.)
void setRelay(bool on) {
  noInterrupts();
  if (on && estopIrq) on = false;           // an E-stop just happened: stay off
  relayOn = on;
  digitalWrite(PIN_RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
  interrupts();
  relayChangeMs = millis();
}

void setBuzzer(bool on) {
  digitalWrite(PIN_BUZZER, (on != BUZZER_ACTIVE_LOW) ? HIGH : LOW);
}

void motorStopNow() {
  noInterrupts();          // these writes share a timer register with the interrupt's
  pwmNow = 0;
  motorPwm(0);                    // ENA low = both motor outputs off
  interrupts();
}

void motorDrive(uint8_t pwm) {
  noInterrupts();
  if (!estopIrq) {         // never switch the motor on over a fresh E-stop
    motorPwm(pwm);                     // direction is fixed by IN1/IN2 wiring
  }
  interrupts();
}

void beep(uint16_t ms) { beepUntil = millis() + ms; }

float median3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }   // now a <= b
  if (b > c) b = c;                          // b = min(b, c)
  return (a > b) ? a : b;                    // max(a, min(b, c))
}

// 0 = OK, 1 = WARNING, 2 = STOP. "High" = bigger value is worse.
// Hysteresis: once in WARNING, the value must fall clearly below the limit to clear.
uint8_t levelHigh(float v, float warn, float stop, float hyst, uint8_t prev) {
  if (v >= stop) return 2;
  if (v >= warn) return 1;
  if (prev >= 1 && v > warn - hyst) return 1;
  return 0;
}

uint8_t levelLow(float v, float warn, float stop, float hyst, uint8_t prev) {
  if (v <= stop) return 2;
  if (v <= warn) return 1;
  if (prev >= 1 && v < warn + hyst) return 1;
  return 0;
}

// =========================== 8. I2C LOW LEVEL ==============================
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cWrite16(uint8_t addr, uint8_t reg, uint16_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write((uint8_t)(val >> 8));
  Wire.write((uint8_t)(val & 0xFF));
  return Wire.endTransmission() == 0;
}

bool i2cRead16(uint8_t addr, uint8_t reg, uint16_t &out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)2) != 2) return false;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  out = ((uint16_t)hi << 8) | lo;
  return true;
}

// ============================= 9. MPU6050 ==================================
bool readAccel(int16_t a[3]) {
  Wire.beginTransmission(mpuAddr);
  Wire.write(0x3B);                                    // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;  // repeated start
  if (Wire.requestFrom(mpuAddr, (uint8_t)6) != 6) return false;
  for (uint8_t k = 0; k < 3; k++) {
    uint8_t hi = Wire.read();                          // read in two steps: C++ does not
    uint8_t lo = Wire.read();                          // promise the order inside one line
    a[k] = (int16_t)(((uint16_t)hi << 8) | lo);
  }
  return true;
}

bool initMPU() {
  if (i2cPresent(0x68)) mpuAddr = 0x68;
  else if (i2cPresent(0x69)) mpuAddr = 0x69;
  else return false;
  bool ok = i2cWrite8(mpuAddr, 0x6B, 0x01);  // PWR_MGMT_1: wake up, clock from gyro PLL
  delay(10);
  ok = ok && i2cWrite8(mpuAddr, 0x1A, 0x03); // CONFIG: digital low-pass filter ~44 Hz
  ok = ok && i2cWrite8(mpuAddr, 0x1C, 0x08); // ACCEL_CONFIG: +/-4 g (8192 counts per g)
  delay(20);
  int16_t a[3];
  if (!ok || !readAccel(a)) return false;
  for (uint8_t k = 0; k < 3; k++) { accOff[k] = a[k]; accSum[k] = 0; accSq[k] = 0; }
  nSamples = 0;
  return true;
}

// ============================== 10. INA219 =================================
bool initINA() {
  if (!i2cPresent(INA_ADDR)) return false;
  // CONFIG = 0x39FF: 32 V range, +/-320 mV shunt range, bus 12-bit,
  // shunt averaged over 128 samples (68 ms) -> smooths the motor's PWM ripple
  return i2cWrite16(INA_ADDR, 0x00, 0x39FF);
}

bool readINA() {
  uint16_t sh, bus;
  if (!i2cRead16(INA_ADDR, 0x01, sh)) return false;   // shunt voltage, 10 uV per count
  if (!i2cRead16(INA_ADDR, 0x02, bus)) return false;  // bus voltage, 4 mV per count (bits 15..3)
  float shunt_mV = (int16_t)sh * 0.01;
  currentMa = fabs(shunt_mV / SHUNT_OHMS);             // Ohm's law: I = V / R
  busV = (bus >> 3) * 0.004;
  powerW = busV * currentMa / 1000.0;                  // P = V x I
  return true;
}

// ============================== 11. DHT22 / DHT11 ==========================
// Both sensors use the same 3 wires but talk differently:
//   DHT22 (white): 1 ms wake-up pulse,  0.1 C steps, -40..80 C
//   DHT11 (blue) : 18 ms wake-up pulse, 1 C steps,     0..50 C
// To find out which one is fitted, read it both ways and keep the one that
// makes sense:
//   a DHT11 read as a DHT22 gives humidity over 100 %  (or does not answer)
//   a DHT22 read as a DHT11 gives a temperature below 3 C (impossible indoors)
// Each sensor answers at most once every 2 s, and needs ~2 s after power-up.
// Waits are chopped into 100 ms pieces so the watchdog stays happy.
void waitFed(uint16_t ms) {
  for (uint16_t i = 0; i < ms; i += 100) { delay(100); wdt_reset(); }
}

bool readDhtAs(uint8_t model, float& t, float& h) {
  DHT& s = (model == 11) ? dht11 : dht22;
  t = s.readTemperature(false, true);
  h = s.readHumidity();
  if (isnan(t) || isnan(h) || h > 100.5) return false;
  if (model == 11) return t >= 3 && t <= 60;
  return t > -40 && t < 80;
}

bool initTempSensor() {
  dht22.begin();
  dht11.begin();
  tCount = 0; tHead = 0; tempRateValid = false;
  uint8_t first  = (dhtModel == 11) ? 11 : 22;   // try last time's type first
  uint8_t second = (first == 11) ? 22 : 11;
  uint8_t model = 0;
  for (uint8_t attempt = 0; attempt < 4; attempt++) {
    if (attempt) waitFed(model == 22 ? 1100 : 2100);   // DHT11 is ready again after 1 s
    model = (attempt % 2 == 0) ? first : second;
    float t, h;
    if (readDhtAs(model, t, h)) {
      dhtModel = model;
      dht = (model == 11) ? &dht11 : &dht22;
      tempC = t;
      humidity = h;
      tBuf[0] = t; tHead = 1; tCount = 1;
      tTemp = millis();
      return true;
    }
  }
  return false;
}

// ============================== 12. SELF-TEST ==============================
// Is the 12 V divider fitted? Turn on A3's internal pull-up (~35k to 5V) for a
// moment. With the divider, its 4.7k to GND holds the pin low (~0.6 V).
// With nothing connected, the pull-up wins and the pin reads ~5 V.
bool detect12VSense() {
  pinMode(PIN_12V_SENSE, INPUT_PULLUP);
  delay(5);
  analogRead(PIN_12V_SENSE);                 // first reading settles the ADC
  int raw = analogRead(PIN_12V_SENSE);
  pinMode(PIN_12V_SENSE, INPUT);
  return raw < 600;                          // below ~2.9 V = divider present
}

void logEventStart(const __FlashStringHelper* what) {
  rowFinish();                     // never print into the middle of a data row
  if (textMode) {
    LOG.print(F(">> "));           // readable: ">> SPEED,60"
  } else {
    LOG.print(F("E,"));            // dashboard: "E,12345,SPEED,60"
    LOG.print(millis());
    LOG.print(',');
  }
  LOG.print(what);
}

void selfTest() {
  mpuOk = initMPU();
  tempOk = initTempSensor();
#if HAS_INA219
  inaOk = initINA();
#endif
#if HAS_12V_DIVIDER
  senseOk = !inaOk && USE_12V_SENSE && detect12VSense();
#endif
  mpuFail = !mpuOk;
  tempFail = !tempOk;
  inaFail = false;
  mpuErrCount = mpuStuckCount = tempBadCount = inaErrCount = 0;

  logEventStart(F("SELFTEST"));
  LOG.print(F(",MPU6050=")); LOG.print(mpuOk ? F("OK") : F("MISSING"));
  LOG.print(!tempOk ? F(" DHT=MISSING") : (dhtModel == 11 ? F(" DHT11=OK") : F(" DHT22=OK")));
  LOG.print(F(" INA219="));  LOG.print(inaOk ? F("OK") : F("not fitted"));
  LOG.print(F(" 12V_SENSE=")); LOG.print(senseOk ? F("A3") : F("not fitted"));
  LOG.print(F(" LCD="));     LOG.println(lcd ? F("OK") : F("not found"));
}

// ======================== 13. SENSOR TASKS =================================
// --- Ultrasonic: distance = echo time x speed of sound / 2 (there and back)
void taskUltrasonic(uint32_t now) {
  if (now - tPing < PING_MS) return;
  tPing = now;
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);
  uint32_t us = pulseIn(PIN_US_ECHO, HIGH, ECHO_TIMEOUT_US);
  float cm = (us == 0) ? DIST_MAX_CM : us * 0.0343 / 2.0;   // 343 m/s = 0.0343 cm/us
  if (cm > DIST_MAX_CM) cm = DIST_MAX_CM;
  distRaw[distIdx] = cm;
  distIdx = (distIdx + 1) % 3;
  distCm = median3(distRaw[0], distRaw[1], distRaw[2]);     // one bad echo cannot trip us
}

// --- Temperature trend: least-squares slope of the last TREND_N readings
float trendSlopePerMin() {
  uint8_t n = tCount;
  uint8_t start = (tHead + TREND_N - n) % TREND_N;
  float y0 = tBuf[start];
  float sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (uint8_t i = 0; i < n; i++) {
    float x = i;
    float y = tBuf[(start + i) % TREND_N] - y0;
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  float den = n * sxx - sx * sx;
  if (den <= 0) return 0;
  float perSample = (n * sxy - sx * sy) / den;        // deg C per reading
  return perSample * (60000.0 / TEMP_MS);             // -> deg C per minute
}

void taskTemperature(uint32_t now) {
  if (!tempOk || now - tTemp < TEMP_MS) return;
  tTemp = now;
  float t = dht->readTemperature(false, true); // one fresh reading (DHT22 ~5 ms, DHT11 ~25 ms)
  float h = dht->readHumidity();               // same reading, no second wait
  if (isnan(t) || t < -40 || t > 80) {         // no answer = wire loose or sensor dead
    if (tempBadCount < 255) tempBadCount++;
    if (tempBadCount >= 3) { tempFail = true; tempC = NAN; tCount = 0; tempRateValid = false; }
    return;
  }
  tempBadCount = 0;
  tempFail = false;
  tempC = t;
  if (!isnan(h)) humidity = h;
  tBuf[tHead] = t;
  tHead = (tHead + 1) % TREND_N;
  if (tCount < TREND_N) tCount++;
  // The DHT11 only reports whole degrees, so wait for the full ~30 s of readings
  // before trusting its rise rate (one 1 C step then reads as at most ~3 C/min).
  tempRateValid = (tCount >= (dhtModel == 11 ? TREND_N : 8));
  tempRate = tempRateValid ? trendSlopePerMin() : 0;
}

// --- Baselines and limits
void updateLimits() {
  if (!baselineReady) {                       // while learning: only the absolute limits
    vibWarn = vibStop = VIB_ABS_STOP_MG;
    curWarn = curStop = CUR_ABS_STOP_MA;
    return;
  }
  vibWarn = fmax(VIB_FLOOR_MG, vibBase * VIB_WARN_FACTOR);
  vibStop = fmax(vibWarn * 1.5, vibBase * VIB_STOP_FACTOR);
  if (vibStop > VIB_ABS_STOP_MG) vibStop = VIB_ABS_STOP_MG;
  if (vibWarn > vibStop * 0.8) vibWarn = vibStop * 0.8;

  curWarn = fmax(CUR_FLOOR_MA, curBase * CUR_WARN_FACTOR);
  curStop = fmax(curWarn * 1.3, curBase * CUR_STOP_FACTOR);
  if (curStop > CUR_ABS_STOP_MA) curStop = CUR_ABS_STOP_MA;
  if (curWarn > curStop * 0.8) curWarn = curStop * 0.8;
}

void finishLearning() {
  vibBase = learnVibSum / learnVibN;
  curBase = (learnCurN > 0) ? learnCurSum / learnCurN : 0;
  baselineReady = true;
  uint8_t b = speedPct / 10;                 // remember it for this speed
  baseVib[b] = vibBase;
  baseCur[b] = curBase;
  baseKnown |= (1u << b);
  updateLimits();
  logEventStart(F("BASELINE"));
  LOG.print(F(",speed_pct=")); LOG.print(speedPct);
  LOG.print(F(" vib_base_mg=")); LOG.print(vibBase, 1);
  LOG.print(F(" vib_warn=")); LOG.print(vibWarn, 0);
  LOG.print(F(" vib_stop=")); LOG.print(vibStop, 0);
  if (inaOk) {
    LOG.print(F(" cur_base_ma=")); LOG.print(curBase, 0);
    LOG.print(F(" cur_warn=")); LOG.print(curWarn, 0);
    LOG.print(F(" cur_stop=")); LOG.print(curStop, 0);
  }
  LOG.println();
  beep(60);
}

// --- Called every 0.5 s with a fresh vibration result
void onVibrationWindow(uint32_t now) {
  if (!motorChecksActive(now)) {
    lvl[R_VIB] = lvl[R_STALL] = 0;
    vibStopCount = stallCount = 0;
    return;
  }
  if (!baselineReady && state == ST_RUN && pwmNow == runPwm()) {
    learnVibSum += vibMg;
    learnVibN++;
    if (learnVibN >= LEARN_WINDOWS) finishLearning();
  }
  uint8_t l = levelHigh(vibMg, vibWarn, vibStop, vibWarn * 0.15, lvl[R_VIB]);
  if (l == 2) {
    if (vibStopCount < 255) vibStopCount++;
    if (vibStopCount < VIB_STOP_WINDOWS) l = 1;    // a single knock only warns
  } else {
    vibStopCount = 0;
  }
  lvl[R_VIB] = l;

  // Stall / jam: at a steady speed the motor must keep vibrating like it did when healthy
  if (baselineReady && vibBase >= STALL_MIN_BASE_MG && state == ST_RUN &&
      pwmNow == runPwm() && vibMg < vibBase * STALL_FACTOR) {
    if (stallCount < 255) stallCount++;
  } else {
    stallCount = 0;
  }
  lvl[R_STALL] = (stallCount >= STALL_WINDOWS) ? 2 : 0;
}

// --- Vibration RMS + tilt from one 0.5 s window of samples
void finishWindow(uint32_t now) {
  float mean[3];
  float var = 0;
  for (uint8_t k = 0; k < 3; k++) {
    float m = accSum[k] / nSamples;
    mean[k] = accOff[k] + m;
    var += accSq[k] / nSamples - m * m;       // variance = mean of squares - square of mean
    accOff[k] = mean[k];
    accSum[k] = 0;
    accSq[k] = 0;
  }
  nSamples = 0;

  // A real MPU6050 always has a little noise. Exactly zero variance = frozen sensor.
  if (var < 0.5) {
    if (mpuStuckCount < 255) mpuStuckCount++;
    if (mpuStuckCount >= 3) mpuFail = true;
    var = 0;
  } else {
    mpuStuckCount = 0;
    mpuFail = false;
  }
  vibMg = sqrt(var) * 1000.0 / ACC_LSB_PER_G;   // RMS of all 3 axes, in milli-g

  // Tilt = angle between today's gravity direction and the installed one
  if (rezeroTilt || !tiltRefSet) {
    for (uint8_t k = 0; k < 3; k++) tiltRef[k] = mean[k];
    tiltRefSet = true;
    rezeroTilt = false;
  }
  float dot = 0, ma = 0, mb = 0;
  for (uint8_t k = 0; k < 3; k++) {
    dot += mean[k] * tiltRef[k];
    ma  += mean[k] * mean[k];
    mb  += tiltRef[k] * tiltRef[k];
  }
  if (ma > 0 && mb > 0) {
    float c = dot / (sqrt(ma) * sqrt(mb));
    if (c > 1) c = 1;
    if (c < -1) c = -1;
    tiltDeg = acos(c) * 57.29578;
  }
  onVibrationWindow(now);
}

void taskMPU(uint32_t nowUs, uint32_t now) {
  if (!mpuOk) return;
  // Sensor lost (e.g. jammed I2C bus): retry only 5x per second, so each 25 ms
  // time-out cannot slow the whole loop down
  if (nowUs - tSampleUs < (mpuFail ? 200000UL : (uint32_t)SAMPLE_US)) return;
  tSampleUs = nowUs;
  int16_t a[3];
  if (!readAccel(a)) {
    if (mpuErrCount < 255) mpuErrCount++;
    if (mpuErrCount >= 20) mpuFail = true;     // 0.2 s of failed reads
    return;
  }
  mpuErrCount = 0;
  for (uint8_t k = 0; k < 3; k++) {
    float d = a[k] - accOff[k];                // small numbers keep float maths accurate
    accSum[k] += d;
    accSq[k]  += d * d;
  }
  if (++nSamples >= VIB_WINDOW) finishWindow(now);
}

// --- Motor supply: voltage from the A3 divider (or the optional INA219),
//     two electrical cross-checks, and current checks if an INA219 is fitted
void taskPower(uint32_t now) {
  if (now - tIna < INA_MS) return;
  tIna = now;
  if (inaOk) {
    if (!readINA()) {
      if (inaErrCount < 255) inaErrCount++;
      if (inaErrCount >= 5) inaFail = true;
      return;
    }
    inaErrCount = 0;
    inaFail = false;
  } else if (senseOk) {
    // Voltage divider: Vpin = V12 x 4.7 / (10 + 4.7), so V12 = Vpin x 3.13
    busV = analogRead(PIN_12V_SENSE) * (5.0 / 1023.0) * SENSE_RATIO;
  } else {
    return;                                    // nothing measures the motor supply
  }

  // Check 1: relay is ON, so the motor must have its 12 V
  if (isRunning() && now - relayChangeMs > 500) {
    if (busV < BUS_MIN_V) { if (powerBadCount < 255) powerBadCount++; }
    else powerBadCount = 0;
  } else {
    powerBadCount = 0;
  }
  lvl[R_NOPOWER] = (powerBadCount >= 3) ? 2 : 0;

  // Check 2: relay is OFF, so there must be NO voltage after it (welded contacts?)
  if (!relayOn && now - relayChangeMs > 800) {
    if (busV > BUS_MIN_V) { if (relayBadCount < 255) relayBadCount++; }
    else relayBadCount = 0;
  } else {
    relayBadCount = 0;
  }
  lvl[R_RELAY] = (relayBadCount >= 5) ? 2 : 0;

  // Check 3: overload / jam (only possible with an INA219)
  if (!inaOk) return;
  if (!motorChecksActive(now)) { lvl[R_CURRENT] = 0; curStopCount = 0; return; }
  if (!baselineReady && state == ST_RUN && pwmNow == runPwm()) {
    learnCurSum += currentMa;
    learnCurN++;
  }
  uint8_t l = levelHigh(currentMa, curWarn, curStop, curWarn * 0.1, lvl[R_CURRENT]);
  if (l == 2) {
    if (curStopCount < 255) curStopCount++;
    if (curStopCount < CUR_STOP_READS) l = 1;
  } else {
    curStopCount = 0;
  }
  lvl[R_CURRENT] = l;
}

// ============================ 14. DECISIONS ================================
void evaluate(uint32_t now) {
  lvl[R_ESTOP_BTN] = btnStop.stable ? 2 : 0;
  lvl[R_SENS_TEMP] = tempFail ? 2 : 0;
  lvl[R_SENS_MPU]  = mpuFail ? 2 : 0;
  lvl[R_SENS_INA]  = (inaOk && inaFail) ? 2 : 0;

  lvl[R_PROX] = levelLow(distCm, DIST_WARN_CM, DIST_STOP_CM, DIST_HYST_CM, lvl[R_PROX]);

  if (!tempFail && !isnan(tempC)) {
    lvl[R_TEMP]  = levelHigh(tempC, TEMP_WARN_C, TEMP_STOP_C, TEMP_HYST_C, lvl[R_TEMP]);
    lvl[R_TRATE] = tempRateValid
                   ? levelHigh(tempRate, TRATE_WARN, TRATE_STOP, TRATE_HYST, lvl[R_TRATE]) : 0;
  } else {
    lvl[R_TEMP] = lvl[R_TRATE] = 0;
  }

  lvl[R_TILT] = (!mpuFail && tiltRefSet)
                ? levelHigh(tiltDeg, TILT_WARN_DEG, TILT_STOP_DEG, TILT_HYST_DEG, lvl[R_TILT]) : 0;
  // R_VIB, R_STALL, R_CURRENT, R_NOPOWER and R_RELAY are set by their own sensor tasks.

  worstLevel = 0;
  worstReason = R_NONE;
  for (uint8_t r = 1; r < R_COUNT; r++) {
    if (lvl[r] > worstLevel) { worstLevel = lvl[r]; worstReason = r; }
  }
  if (worstLevel >= 1) lastWarnMs = now;
}

void changeState(uint8_t s, uint8_t reason) {
  state = s;
  logEventStart(textMode ? F("MACHINE IS NOW ") : F("STATE,"));
  printPgm(STATE_NAME, s);
  LOG.print(textMode ? F(" - ") : F(","));
  printPgm(textMode ? REASON_TEXT : REASON_CODE, reason);
  LOG.println();
  lcdPage = 0;
  tLcdPage = millis();
}

void clearMotorChecks() {
  lvl[R_VIB] = lvl[R_CURRENT] = lvl[R_NOPOWER] = lvl[R_STALL] = 0;
  vibStopCount = curStopCount = powerBadCount = stallCount = 0;
}

void enterIdle() {
  motorStopNow();
  setRelay(false);
  clearMotorChecks();
  latchedReason = R_NONE;
  changeState(ST_IDLE, R_NONE);
}

void startLearning() {
  baselineReady = false;
  learnVibSum = learnCurSum = 0;
  learnVibN = 0;
  learnCurN = 0;
  updateLimits();
}

// One baseline per speed: each speed is learned once (the first time it runs
// healthy after power-up) and then remembered.
void selectBaseline() {
  uint8_t b = speedPct / 10;
  if (baseKnown & (1u << b)) {
    vibBase = baseVib[b];
    curBase = baseCur[b];
    baselineReady = true;
    updateLimits();
  } else {
    startLearning();
  }
}

void enterRun() {
  selectBaseline();            // known speed: keep its baseline. New speed: learn it
  clearMotorChecks();
  runStartMs = millis();
  setRelay(true);              // power first, motor after RELAY_SETTLE_MS
  changeState(ST_RUN, R_NONE);
  beep(80);
}

void enterEstop(uint8_t reason) {
  motorStopNow();              // 1. driver off
  setRelay(false);             // 2. power physically cut
  clearMotorChecks();
  latchedReason = reason;      // 3. remember the FIRST cause ("first-out")
  if (estopCount < 9999) estopCount++;
  changeState(ST_ESTOP, reason);
}

void enterFault(uint8_t reason) {
  motorStopNow();
  setRelay(false);
  clearMotorChecks();
  latchedReason = reason;
  changeState(ST_FAULT, reason);
}

void runStateMachine(uint32_t now) {
  bool go = goRequest;
  goRequest = false;

  if (lvl[R_RELAY] == 2 && state != ST_FAULT) { enterFault(R_RELAY); return; }

  switch (state) {
    case ST_IDLE:
      if (lvl[R_ESTOP_BTN] == 2) { enterEstop(R_ESTOP_BTN); break; }
      if (go) {
        if (worstLevel < 2) enterRun();
        else beep(400);                         // start refused: danger present
      }
      break;

    case ST_RUN:
    case ST_WARN:
      if (worstLevel == 2) { enterEstop(worstReason); break; }
      if (worstLevel == 1) {
        if (state != ST_WARN) changeState(ST_WARN, worstReason);
      } else if (state == ST_WARN && now - lastWarnMs >= WARN_DWELL_MS) {
        changeState(ST_RUN, R_NONE);            // clear for 3 s -> back to full speed
      }
      break;

    case ST_ESTOP:
      if (go) {
        if (worstLevel < 2) enterIdle();        // unlocked, but NOT restarted
        else beep(400);
      }
      break;

    case ST_FAULT:
      if (go) {
        selfTest();
        if (mpuOk && tempOk && lvl[R_RELAY] == 0) enterIdle();
        else beep(400);
      }
      break;
  }
}

// ============================== 15. MOTOR ==================================
void taskMotor(uint32_t now) {
  if (now - tMotor < 10) return;
  tMotor = now;
  if (!isRunning()) { motorStopNow(); return; }
  uint8_t target = 0;
  if (now - runStartMs >= RELAY_SETTLE_MS) target = (state == ST_WARN) ? warnPwm() : runPwm();
  if (pwmNow < target)      pwmNow = (target - pwmNow > RAMP_STEP) ? pwmNow + RAMP_STEP : target;
  else if (pwmNow > target) pwmNow = (pwmNow - target > RAMP_STEP) ? pwmNow - RAMP_STEP : target;
  motorDrive(pwmNow);
}

// ===================== 15b. E-STOP INTERRUPT ===============================
// D3 is one of the Uno's two hardware-interrupt pins. The moment the E-stop
// contact closes, the chip pauses whatever it is doing (an LCD update, a sensor
// read...) and runs this function within a few microseconds. It cuts the motor
// right there; loop() then does the bookkeeping (state, LCD, log) on its next pass.
// Without it, the button is only looked at once per loop pass, so a very quick
// tap during a slow pass (~50 ms worst case) could be missed.
// Rules for an interrupt function: keep it tiny, and never use Serial, Wire,
// delay() or millis()-waits inside it - they need interrupts themselves.
void onEstopInterrupt() {
  bool pressed = (digitalRead(PIN_BTN_ESTOP) == LOW) != ESTOP_BUTTON_IS_NC;
  if (!pressed) return;                    // electrical spike, already gone - ignore
  estopIrq = true;
  motorPwm(0);                             // 1. driver off
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);   // 2. power cut
}

// ============================ 16. INPUTS ===================================
void updateButton(Button &b, uint32_t now) {
  bool raw = (digitalRead(b.pin) == LOW);
  if (b.nc) raw = !raw;                       // NC contact: pressed = opens = HIGH
  if (raw != b.last) {
    b.last = raw;
    b.changedMs = now;
  } else if (raw != b.stable && now - b.changedMs >= DEBOUNCE_MS) {
    b.stable = raw;
    if (raw) b.pressedEvent = true;
  }
}

void printHelp() {
  rowFinish();
  LOG.println(F("# Commands: g = GO (start/reset)  x = emergency stop  z = re-zero tilt"));
  LOG.println(F("#           10..100 + Enter = speed %   + / - = speed +-10 %"));
  LOG.println(F("#           s = status   l = re-learn baselines   ? = help"));
  LOG.println(F("# Output: n = readable text (every 2 s)  d = data for the dashboard"));
  LOG.println(F("# Graphs (Tools -> Serial Plotter): v = vibration  t = temperature  b = both"));
}

// --- Serial Plotter graphs ----------------------------------------------------
// The Arduino IDE's Serial Plotter draws every "Name:value" pair on a line as one
// coloured line. Click a name in the legend to hide / show that line.
// m: 1/2/3 = a graph, 0 = readable text, 4 = dashboard data
void setPlotMode(uint8_t m) {
  rowFinish();
  if (m >= 1 && m <= 3) {
    logOut = &nullOut;                       // text off: the plotter only gets numbers
    plotMode = m;
    return;
  }
  plotMode = 0;
  logOut = &Serial;                          // text output back on
  textMode = (m == 0);
  if (textMode) LOG.println(F("# readable text, every 2 s  (d = data for the dashboard)"));
  else printDataHeader();
}

// --- The readable report for the Serial Monitor (every TEXT_MS) ---------------
// Built in memory and sent bit by bit, like the data row, so the loop never waits.
char* rb;
void bTxt(const __FlashStringHelper* t) { strcpy_P(rb, (PGM_P)t); rb += strlen(rb); }
void bNum(float v, uint8_t dec) {
  if (isnan(v)) { *rb++ = '-'; *rb++ = '-'; *rb = 0; return; }
  dtostrf(v, 1, dec, rb); rb += strlen(rb);
}
void bNl() { *rb++ = '\r'; *rb++ = '\n'; }

void textReport() {
  uint8_t reason = (state == ST_ESTOP || state == ST_FAULT) ? latchedReason : worstReason;
  rb = rowBuf;
  bTxt(F("--- ")); pgmText(STATE_NAME, state, rb, 12); rb += strlen(rb);
  bTxt(F(" | speed ")); bNum(pwmToPct(pwmNow), 0);
  bTxt(F("% (set ")); bNum(speedPct, 0);
  bTxt(F("%) | ")); pgmText(REASON_TEXT, reason, rb, 17); rb += strlen(rb); bNl();
  bTxt(F("Temperature : ")); bNum(tempC, 1); bTxt(F(" C (warn ")); bNum(TEMP_WARN_C, 0);
  bTxt(F(" / stop ")); bNum(TEMP_STOP_C, 0); bTxt(F("), rising "));
  if (tempRateValid) bNum(tempRate, 2); else bTxt(F("--"));
  bTxt(F(" C/min")); bNl();
  bTxt(F("Vibration   : ")); bNum(vibMg, 1); bTxt(F(" mg "));
  if (baselineReady) { bTxt(F("(warn ")); bNum(vibWarn, 0); bTxt(F(" / stop ")); bNum(vibStop, 0); bTxt(F(")")); }
  else bTxt(F("(limits learned when running)"));
  bNl();
  bTxt(F("Distance    : "));
  if (distCm >= DIST_MAX_CM) bTxt(F("clear")); else { bNum(distCm, 0); bTxt(F(" cm")); }
  bTxt(F("   Tilt: ")); bNum(tiltDeg, 1); bTxt(F(" deg   Humidity: ")); bNum(humidity, 0); bTxt(F(" %"));
  bNl();
  rowLen = rb - rowBuf;
  rowPos = 0;
  rowPump();
}

void plotPair(const __FlashStringHelper* name, float v, uint8_t dec, bool last) {
  Serial.print(name);
  Serial.print(':');
  Serial.print(isnan(v) ? 0.0 : v, dec);
  if (!last) Serial.print(',');
  else Serial.println();
}

void taskPlot(uint32_t now) {
  if (!plotMode) return;
  if (now - tPlot < (plotMode == 2 ? 1000 : 250)) return;   // temperature changes slowly
  tPlot = now;
  float vw = baselineReady ? vibWarn : 0;    // limits only once they are learned
  float vs = baselineReady ? vibStop : 0;
  if (plotMode == 1) {                       // VIBRATION graph
    plotPair(F("Vibration_mg"), vibMg, 1, false);
    plotPair(F("Warn_mg"), vw, 0, false);
    plotPair(F("Stop_mg"), vs, 0, false);
    plotPair(F("Speed_pct"), pwmToPct(pwmNow), 0, true);
  } else if (plotMode == 2) {                // TEMPERATURE graph
    plotPair(F("Temp_C"), tempC, 1, false);
    plotPair(F("Warn_C"), TEMP_WARN_C, 0, false);
    plotPair(F("Stop_C"), TEMP_STOP_C, 0, false);
    plotPair(F("Rise_C_per_min"), tempRateValid ? tempRate : 0, 2, true);
  } else {                                   // BOTH on one chart (similar ranges)
    plotPair(F("Vibration_mg"), vibMg, 1, false);
    plotPair(F("Vib_stop_mg"), vs, 0, false);
    plotPair(F("Temp_C"), tempC, 1, false);
    plotPair(F("Temp_stop_C"), TEMP_STOP_C, 0, true);
  }
}

// 's': everything at a glance, in plain words (handy for the demo / viva)
void printLimitPair(float w, float st, uint8_t dec) {
  LOG.print(F("  (warn ")); LOG.print(w, dec);
  LOG.print(F(" / stop ")); LOG.print(st, dec); LOG.println(')');
}

void printStatus() {
  rowFinish();
  uint8_t reason = (state == ST_ESTOP || state == ST_FAULT) ? latchedReason : worstReason;
  char buf[20];
  LOG.println(F("# ------------------------- STATUS -------------------------"));
  LOG.print(F("# State      : ")); printPgm(STATE_NAME, state);
  pgmText(REASON_TEXT, reason, buf, sizeof buf);
  LOG.print(F(" - ")); LOG.println(buf);
  LOG.print(F("# Speed      : set ")); LOG.print(speedPct);
  LOG.print(F(" %, now ")); LOG.print(pwmToPct(pwmNow));
  LOG.print(F(" % (PWM ")); LOG.print(pwmNow); LOG.print(F(")"));
  if (isRunning()) {
    uint32_t sec = (millis() - runStartMs) / 1000;
    LOG.print(F(", running for ")); LOG.print(sec / 60);
    LOG.print(F(" min ")); LOG.print(sec % 60); LOG.print(F(" s"));
  }
  LOG.println();
  LOG.print(F("# Distance   : "));
  if (distCm >= DIST_MAX_CM) LOG.print(F("nothing near")); else { LOG.print(distCm, 0); LOG.print(F(" cm")); }
  LOG.print(F("  (warn < ")); LOG.print(DIST_WARN_CM, 0);
  LOG.print(F(" / stop < ")); LOG.print(DIST_STOP_CM, 0); LOG.println(F(" cm)"));
  LOG.print(F("# Temperature: ")); LOG.print(tempC, 1); LOG.print(F(" C"));
  printLimitPair(TEMP_WARN_C, TEMP_STOP_C, 0);
  LOG.print(F("# Rise rate  : "));
  if (tempRateValid) { LOG.print(tempRate, 2); LOG.print(F(" C/min")); }
  else LOG.print(F("measuring (needs ~30 s of readings)"));
  printLimitPair(TRATE_WARN, TRATE_STOP, 0);
  LOG.print(F("# Humidity   : ")); LOG.print(humidity, 0); LOG.println(F(" %"));
  LOG.print(F("# Vibration  : ")); LOG.print(vibMg, 1); LOG.print(F(" mg"));
  if (baselineReady) { LOG.print(F(", healthy ")); LOG.print(vibBase, 1); }
  else LOG.print(F(", baseline not learned yet"));
  printLimitPair(vibWarn, vibStop, 0);
  LOG.print(F("# Tilt       : ")); LOG.print(tiltDeg, 1); LOG.print(F(" deg"));
  printLimitPair(TILT_WARN_DEG, TILT_STOP_DEG, 0);
  if (inaOk) {
    LOG.print(F("# Motor      : ")); LOG.print(currentMa, 0); LOG.print(F(" mA, "));
    LOG.print(busV, 1); LOG.print(F(" V, ")); LOG.print(powerW, 1); LOG.println(F(" W"));
  }
  LOG.print(F("# Learned at : "));
  bool any = false;
  for (uint8_t b = 1; b <= 10; b++) {
    if (baseKnown & (1u << b)) { LOG.print(b * 10); LOG.print(F("% ")); any = true; }
  }
  LOG.println(any ? F("") : F("no speed yet"));
  LOG.print(F("# Sensors    : MPU6050 ")); LOG.print(mpuOk && !mpuFail ? F("OK") : F("ERROR"));
  LOG.print(F(", DHT")); LOG.print(dhtModel ? dhtModel : 22);
  LOG.print(tempOk && !tempFail ? F(" OK") : F(" ERROR"));
  LOG.print(F(", LCD ")); LOG.print(!lcd ? F("not found") : (lcdBusOk ? F("OK") : F("bus jammed")));
  LOG.print(F(", INA219 ")); LOG.println(inaOk ? F("OK") : F("not fitted"));
  LOG.print(F("# E-stops    : ")); LOG.print(estopCount);
  LOG.print(F(" since power-up, up ")); LOG.print(millis() / 60000UL); LOG.println(F(" min"));
  LOG.println(F("# ----------------------------------------------------------"));
}

// Speed in steps of 10 %, so each speed has one learned baseline
void setSpeed(int16_t pct) {
  if (pct < SPEED_MIN_PCT || pct > 100) {
    rowFinish();
    LOG.println(F("# speed must be 10..100 (%). To stop: x, or the E-STOP button"));
    return;
  }
  uint8_t p = (pct + 5) / 10 * 10;
  if (p > 100) p = 100;
  if (p != speedPct) {
    speedPct = p;
    speedChangeMs = millis();                // vibration checks pause while it settles
    selectBaseline();
  }
  logEventStart(F("SPEED"));
  LOG.print(',');
  LOG.println(speedPct);
}

void taskSerialIn() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {                // digits = a new speed in %
      numIn = (numIn < 0 ? 0 : numIn) * 10 + (c - '0');
      if (numIn > 999) numIn = 999;
      numInMs = millis();
      continue;
    }
    if (numIn >= 0) { setSpeed(numIn); numIn = -1; }   // Enter ends the number
    switch (c) {
      case '+':
        setSpeed(speedPct >= 90 ? 100 : speedPct + 10);
        break;
      case '-':
        setSpeed(speedPct <= SPEED_MIN_PCT + 10 ? SPEED_MIN_PCT : speedPct - 10);
        break;
      case 'g': case 'G':
        goRequest = true;
        break;
      case 'x': case 'X':
        if (state != ST_ESTOP && state != ST_FAULT) enterEstop(R_REMOTE);
        break;
      case 'l': case 'L':
        baseKnown = 0;                         // forget every speed's baseline
        startLearning();
        logEventStart(F("RELEARN"));
        LOG.println();
        break;
      case 'z': case 'Z':
        rezeroTilt = true;
        logEventStart(F("TILT_REZERO"));
        LOG.println();
        break;
      case 's': case 'S':
        printStatus();
        break;
      case 'v': case 'V': setPlotMode(1); break;   // graphs for the Serial Plotter
      case 't': case 'T': setPlotMode(2); break;
      case 'b': case 'B': setPlotMode(3); break;
      case 'n': case 'N': setPlotMode(0); break;   // readable text
      case 'd': case 'D': setPlotMode(4); break;   // CSV data for the dashboard
      case '?':
        printHelp();
        break;
    }
  }
  // Serial Monitor set to "No line ending": take the number after a short pause
  if (numIn >= 0 && millis() - numInMs > 500) { setSpeed(numIn); numIn = -1; }
}

// ============================ 17. OUTPUTS ==================================
void taskIndicators(uint32_t now) {
  bool fast = (now / 150) % 2;
  bool slow = (now / 500) % 2;
  bool g = false, y = false, r = false, buzz = false;

  switch (state) {
    case ST_IDLE:
      g = slow;                                  // blinking green = ready
      y = (worstLevel >= 1);
      break;
    case ST_RUN:
      g = baselineReady ? true : fast;           // fast blink while learning
      break;
    case ST_WARN:
      y = true;
      buzz = (now % 1000) < 80;                  // short beep every second
      break;
    case ST_ESTOP:
      if (worstLevel == 2) { r = fast; buzz = fast; }        // danger still there
      else                 { r = true; buzz = (now % 2000) < 60; } // safe: press GO
      break;
    case ST_FAULT:
      r = slow; y = !slow;
      buzz = (now % 3000) < 60;
      break;
  }
  if ((int32_t)(beepUntil - now) > 0) buzz = true;

  digitalWrite(PIN_LED_GREEN,  g ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, y ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,    r ? HIGH : LOW);
  setBuzzer(buzz);
}

// The screen is updated in small pieces. Each character costs ~1.2 ms on the
// I2C bus, so rewriting both lines at once would hold up the loop for ~40 ms.
// lcdLine() only says WHAT we want; lcdFlush() runs every loop pass and writes
// at most a few changed characters (~7 ms), so no pass is ever held up for long.
// A full screen change still completes within a few tens of milliseconds.
const uint8_t LCD_UNITS_PER_PASS = 6;      // 1 unit = 1 character or 1 cursor jump

void lcdLine(uint8_t row, const char* text) {
  uint8_t i = 0;
  for (; i < 16 && text[i]; i++) lcdWant[row][i] = text[i];
  for (; i < 16; i++) lcdWant[row][i] = ' ';   // pad: no lcd.clear(), no flicker
  lcdWant[row][16] = 0;
}

// --- Self-healing screen ---------------------------------------------------
// A loose wire or an unpowered module on A4/A5 can jam the I2C bus. Then every
// message waits 25 ms for its time-out, and a screen update would freeze the loop
// for ~1 s. So we first check the LCD still answers; if not, we leave it alone for
// a second. A wire that flickers can also put the LCD out of step (it receives
// half-bytes), which shows as garbage - so every 10 s, and whenever the bus comes
// back, the LCD is put back in step and the whole screen is redrawn.
void lcdNibble(uint8_t n) {                 // backpack: P0=RS P1=RW P2=EN P3=light P4-7=data
  uint8_t v = (n << 4) | 0x08;
  Wire.beginTransmission(lcdAddr); Wire.write(v | 0x04); Wire.endTransmission();  // EN high
  Wire.beginTransmission(lcdAddr); Wire.write(v);        Wire.endTransmission();  // EN low
}

void lcdResync() {                          // the datasheet's "reset by instruction"
  lcdNibble(0x3); delay(5);
  lcdNibble(0x3); delay(1);
  lcdNibble(0x3); delay(1);
  lcdNibble(0x2); delay(1);                 // now 4-bit mode, in step, from ANY state
  lcd->command(0x28);                       // 4-bit, 2 lines
  lcd->command(0x0C);                       // display on, no cursor
  lcd->command(0x06);                       // write left to right
  lcd->home();                              // undo any accidental shift
  lcd->backlight();
  memset(lcdCache, 0, sizeof lcdCache);     // redraw everything
}

// Returns true if something is still left to write
bool lcdFlush(uint8_t budget) {
  if (!lcd) return false;
  uint32_t now = millis();
  bool due = (now - lcdResyncMs >= 10000);
  bool changed = (memcmp(lcdWant[0], lcdCache[0], 16) != 0) || (memcmp(lcdWant[1], lcdCache[1], 16) != 0);
  if (!changed && !due) return false;
  if (!lcdBusOk && now - lcdRetryMs < 1000) return false;   // jammed: try again in 1 s
  if (changed || due) {
    if (!i2cPresent(lcdAddr)) {
      if (lcdBusOk) { logEventStart(F("I2C_BUS_ERROR")); LOG.println(); }
      lcdBusOk = false;
      lcdRetryMs = now;
      return false;
    }
    if (!lcdBusOk || due) {
      if (!lcdBusOk) { logEventStart(F("I2C_BUS_OK")); LOG.println(); }
      lcdBusOk = true;
      lcdResync();
      lcdResyncMs = now;
    }
  }
  for (uint8_t row = 0; row < 2; row++) {
    uint8_t i = 0;
    while (i < 16) {
      if (lcdWant[row][i] == lcdCache[row][i]) { i++; continue; }
      if (budget < 2) return true;          // out of time for this pass
      lcd->setCursor(i, row);
      budget--;
      while (i < 16 && budget && lcdWant[row][i] != lcdCache[row][i]) {
        lcd->write(lcdWant[row][i]);
        lcdCache[row][i] = lcdWant[row][i];
        i++;
        budget--;
      }
    }
  }
  return false;
}

void valuePage(uint8_t p, char* out) {
  char a[8], b[8];
  if (p == 0) {                                // distance + temperature
    if (distCm >= DIST_MAX_CM) strcpy(a, "--"); else itoa((int)(distCm + 0.5), a, 10);
    if (isnan(tempC)) strcpy(b, "--.-"); else dtostrf(tempC, 1, 1, b);
    snprintf_P(out, 17, PSTR("D:%scm T:%sC"), a, b);
  } else if (p == 1) {                         // vibration + tilt
    snprintf_P(out, 17, PSTR("V:%dmg Tilt:%d%c"), (int)(vibMg + 0.5), (int)(tiltDeg + 0.5), (char)0xDF);
  } else if (p == 2) {                         // power, or humidity + temperature rise rate
    if (inaOk) {
      dtostrf(powerW, 1, 1, b);
      snprintf_P(out, 17, PSTR("I:%dmA P:%sW"), (int)(currentMa + 0.5), b);
    } else {
      if (isnan(humidity)) strcpy(a, "--"); else itoa((int)(humidity + 0.5), a, 10);
      if (!tempRateValid) strcpy(b, "--");
      else { b[0] = tempRate < 0 ? '-' : '+'; dtostrf(fabs(tempRate), 1, 1, b + 1); }
      if (senseOk) {
        char v[8];
        dtostrf(busV, 1, 1, v);
        snprintf_P(out, 17, PSTR("Vm:%sV RH:%s%%"), v, a);
      } else {
        snprintf_P(out, 17, PSTR("RH:%s%% %sC/m"), a, b);
      }
    }
  } else {                                     // speed set point + run time / stop count
    if (isRunning()) {
      uint32_t sec = (millis() - runStartMs) / 1000;
      uint16_t mins = sec / 60;
      if (mins > 99) mins = 99;
      snprintf_P(out, 17, PSTR("Set:%u%% %02u:%02u"), speedPct, mins, (uint8_t)(sec % 60));  // run time
    } else {
      snprintf_P(out, 17, PSTR("Set:%u%% Stop:%u"), speedPct, estopCount > 99 ? 99 : estopCount);
    }
  }
}

void taskLCD(uint32_t now) {
  if (!lcd || now - tLcd < LCD_MS) return;
  tLcd = now;
  if (now - tLcdPage >= LCD_PAGE_MS) { tLcdPage = now; lcdPage++; }

  char l0[17], l1[17];
  uint8_t pct = pwmToPct(pwmNow);              // actual speed, same scale as the set point
  switch (state) {
    case ST_IDLE:
      strcpy_P(l0, worstLevel == 2 ? PSTR("NOT READY") : PSTR("READY: press GO"));
      break;
    case ST_RUN:
      if (!baselineReady) strcpy_P(l0, PSTR("RUN  learning.."));
      else snprintf_P(l0, 17, PSTR("RUN SAFE    %3u%%"), pct);
      break;
    case ST_WARN:
      snprintf_P(l0, 17, PSTR("WARNING     %3u%%"), pct);
      break;
    case ST_ESTOP:
      strcpy_P(l0, worstLevel == 2 ? PSTR("!!! E-STOP !!!") : PSTR("E-STOP: press GO"));
      break;
    default:
      strcpy_P(l0, PSTR("SYSTEM FAULT"));
      break;
  }

  // Bottom line: when something is wrong, every other page shows WHY
  uint8_t reason = R_NONE;
  if (state == ST_ESTOP || state == ST_FAULT) reason = latchedReason;
  else if (worstLevel >= 1) reason = worstReason;

  if (reason != R_NONE) {
    uint8_t p = lcdPage % 8;                  // reason, A, reason, B, reason, C, reason, D
    if (p % 2 == 0) pgmText(REASON_TEXT, reason, l1, sizeof l1);
    else valuePage(p / 2, l1);
  } else {
    valuePage(lcdPage % 4, l1);
  }
  lcdLine(0, l0);
  lcdLine(1, l1);
}

// One CSV row: the dashboard (part 2) and Excel both read this.
// At 9600 baud one character takes ~1 ms, so waiting for a ~100-character row
// would hold up the loop for ~40 ms (the serial port only buffers 64). Instead
// the row is built in memory and handed over only as fast as the port takes it.
char* addNum(char* p, float v, uint8_t dec) {
  dtostrf(v, 1, dec, p);
  p += strlen(p);
  *p++ = ',';
  return p;
}

void rowPump() {                               // send what fits right now, never wait
  while (rowPos < rowLen) {
    int room = Serial.availableForWrite();
    if (room <= 0) return;
    uint8_t n = rowLen - rowPos;
    if (n > room) n = room;
    Serial.write((const uint8_t*)rowBuf + rowPos, n);
    rowPos += n;
  }
}

void rowFinish() {                             // before printing anything else
  while (rowPos < rowLen) rowPump();
}

void taskLog(uint32_t now) {
  rowPump();
  if (plotMode) return;                        // a graph is shown: no CSV rows
  if (now - tLog < (textMode ? TEXT_MS : LOG_MS)) return;
  if (rowPos < rowLen) return;                 // last row still going out: skip one
  tLog = now;
  if (textMode) { textReport(); return; }
  uint8_t reason = (state == ST_ESTOP || state == ST_FAULT) ? latchedReason : worstReason;
  char* p = rowBuf;
  *p++ = 'D'; *p++ = ',';
  ultoa(now, p, 10);                  p += strlen(p); *p++ = ',';
  pgmText(STATE_NAME, state, p, 12);  p += strlen(p); *p++ = ',';
  pgmText(REASON_CODE, reason, p, 14); p += strlen(p); *p++ = ',';
  p = addNum(p, distCm, 1);
  p = addNum(p, tempC, 2);
  p = addNum(p, tempRate, 2);
  p = addNum(p, vibMg, 1);
  p = addNum(p, tiltDeg, 1);
  p = addNum(p, currentMa, 0);
  p = addNum(p, busV, 2);
  p = addNum(p, powerW, 2);
  p = addNum(p, pwmNow, 0);
  p = addNum(p, vibWarn, 0);
  p = addNum(p, vibStop, 0);
  p = addNum(p, curWarn, 0);
  p = addNum(p, curStop, 0);
  p = addNum(p, humidity, 1);
  itoa(speedPct, p, 10);              p += strlen(p); *p++ = ',';
  itoa(pwmToPct(pwmNow), p, 10);      p += strlen(p);
  *p++ = '\r'; *p++ = '\n';
  rowLen = p - rowBuf;
  rowPos = 0;
  rowPump();
}

void printDataHeader() {          // column names + limits, for the dashboard
  LOG.println(F("H,ms,state,reason,dist_cm,temp_c,temp_rate_cpm,vib_mg,tilt_deg,"
                   "current_ma,bus_v,power_w,pwm,vib_warn_mg,vib_stop_mg,cur_warn_ma,cur_stop_ma,humidity_pct,speed_pct,"
                   "speed_now_pct"));
  LOG.print(F("L,dist_warn_cm="));  LOG.print(DIST_WARN_CM, 0);
  LOG.print(F(",dist_stop_cm="));   LOG.print(DIST_STOP_CM, 0);
  LOG.print(F(",temp_warn_c="));    LOG.print(TEMP_WARN_C, 0);
  LOG.print(F(",temp_stop_c="));    LOG.print(TEMP_STOP_C, 0);
  LOG.print(F(",rate_warn_cpm="));  LOG.print(TRATE_WARN, 0);
  LOG.print(F(",rate_stop_cpm="));  LOG.print(TRATE_STOP, 0);
  LOG.print(F(",tilt_warn_deg="));  LOG.print(TILT_WARN_DEG, 0);
  LOG.print(F(",tilt_stop_deg="));  LOG.println(TILT_STOP_DEG, 0);
}

void printHeader() {
  LOG.println(F("# Smart Machine Safety Monitor v2.1"));
  if (!textMode) printDataHeader();
  printHelp();
}

// ============================== 18. START-UP ===============================
void initLCD() {
  uint8_t addr = 0;
  if (i2cPresent(0x27)) addr = 0x27;
  else if (i2cPresent(0x3F)) addr = 0x3F;
  if (!addr) return;                        // the machine still works without a screen
  lcdAddr = addr;
  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  memset(lcdCache, 0, sizeof lcdCache);    // unknown -> everything counts as changed
  lcdResyncMs = millis();
  lcdLine(0, "Machine Safety");
  lcdLine(1, "Monitor v2.1");
  while (lcdFlush(255)) { }                 // start-up: write it all at once
}

void lampTest() {                           // proves every LED and the buzzer work
  digitalWrite(PIN_LED_GREEN, HIGH);  delay(250);
  digitalWrite(PIN_LED_YELLOW, HIGH); delay(250);
  digitalWrite(PIN_LED_RED, HIGH);    delay(250);
  setBuzzer(true); delay(120); setBuzzer(false);
  delay(300);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED, LOW);
}

void setup() {
  uint8_t resetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();

  // 1. Every output to its SAFE state before anything else
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);   // relay OFF
  pinMode(PIN_RELAY, OUTPUT);
  relayOn = false;
  pinMode(PIN_MOTOR_EN, OUTPUT);
#ifdef PIN_MOTOR_EN2
  pinMode(PIN_MOTOR_EN2, OUTPUT);
#endif
  motorStopNow();
  pinMode(PIN_BUZZER, OUTPUT);
  setBuzzer(false);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);
  if (USE_12V_SENSE) pinMode(PIN_12V_SENSE, INPUT);   // set USE_12V_SENSE false to reuse A3
  pinMode(PIN_BTN_GO, INPUT_PULLUP);        // button to GND, no resistor needed
  pinMode(PIN_BTN_ESTOP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_ESTOP), onEstopInterrupt,
                  ESTOP_BUTTON_IS_NC ? RISING : FALLING);   // the moment it is pressed

  // 2. Communication
  Serial.begin(SERIAL_BAUD);
  Wire.begin();
  initLCD();                                // (its init() restarts Wire, so set options after)
  Wire.setClock(100000);
  Wire.setWireTimeout(25000, true);         // a noisy I2C bus can never freeze the loop
                                            // (compile error here? update "Arduino AVR Boards")

  // 3. Lamp test and sensor self-test
  lampTest();
  selfTest();
  printHeader();
  if (resetCause & _BV(WDRF)) {
    logEventStart(F("WATCHDOG_RESET"));
    LOG.println();
  }

  // 4. Start in IDLE (never auto-start), or FAULT if a required sensor is missing
  uint32_t now = millis();
  relayChangeMs = now;
  tPing = tIna = tLogic = tLog = tLcd = tLcdPage = tMotor = now;
  tSampleUs = micros();
  if (!mpuOk)     enterFault(R_SENS_MPU);
  else if (!tempOk) enterFault(R_SENS_TEMP);
  else            enterIdle();

  if (USE_WATCHDOG) wdt_enable(WDTO_2S);
}

// =============================== 19. LOOP ==================================
// Nothing here waits. Each task checks "is it my turn yet?" and returns at once
// if not, so the loop runs thousands of times a second and never misses a button.
void loop() {
  if (USE_WATCHDOG) wdt_reset();            // "I am alive" - stop doing this and the chip resets
  uint32_t now = millis();

  if (estopIrq) {                           // the interrupt already cut the motor
    estopIrq = false;                       // (one byte: reading/clearing is atomic)
    if (state != ST_ESTOP && state != ST_FAULT) enterEstop(R_ESTOP_BTN);
  }
  updateButton(btnGo, now);
  updateButton(btnStop, now);
  if (btnGo.pressedEvent) { btnGo.pressedEvent = false; goRequest = true; }
  if (btnStop.pressedEvent) {               // E-stop reacts immediately, not on the next tick
    btnStop.pressedEvent = false;
    if (state != ST_ESTOP && state != ST_FAULT) enterEstop(R_ESTOP_BTN);
  }
  taskSerialIn();

  taskMPU(micros(), now);
  taskUltrasonic(now);
  taskTemperature(now);
  taskPower(now);

  if (now - tLogic >= LOGIC_MS) {
    tLogic = now;
    evaluate(now);
    runStateMachine(now);
  }

  taskMotor(now);
  taskIndicators(now);
  taskLCD(now);
  lcdFlush(LCD_UNITS_PER_PASS);
  taskLog(now);
  taskPlot(now);
}
