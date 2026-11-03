# Arduino Uno R3 (ATmega328P)

**Role in this project:** the controller. It reads every sensor, runs the state machine every 100 ms, drives the
relay, the motor driver, the LEDs, the buzzer and the LCD, and talks to the PC over USB.

## What is on the board

- **ATmega328P**: an 8-bit AVR microcontroller at 16 MHz. Most instructions take one clock cycle.
- **ATmega16U2**: a second small chip that turns USB into a serial port (UART) for the main chip.
- **5 V regulator** (fed from VIN) and a **3.3 V regulator**.
- **Reset circuit**: the USB chip's DTR line pulses RESET through a 100 nF capacitor whenever the serial port is
  opened. That is why the board restarts every time the Serial Monitor or our server connects.
- **Bootloader** (0.5 KB of flash): lets the IDE upload code over USB without a programmer.

| Feature | Value | Where it matters for us |
| --- | --- | --- |
| Flash (program) | 32 KB, 32,256 bytes usable | Our program uses 31,880 bytes |
| SRAM (variables) | 2 KB | We use 1,299 bytes; fixed text stays in flash with `F()` and `PROGMEM` |
| EEPROM | 1 KB | Not used yet; could store the learned baselines |
| Digital pins | D0 to D13, PWM on 3, 5, 6, 9, 10, 11 | Motor speed on D5 |
| Analog inputs | A0 to A5, 10-bit ADC | Used here as digital outputs (LEDs) and I2C |
| Pin current | 20 mA recommended, 40 mA absolute maximum, 200 mA for the chip | Why the relay and motor need drivers |
| External interrupts | D2 (INT0), D3 (INT1) | Emergency stop on D3 |
| Timers | Timer0, Timer2 (8-bit), Timer1 (16-bit) | PWM and `millis()` |
| Buses | UART (D0, D1), I2C (A4, A5), SPI (D10 to D13) | USB link, MPU6050 and LCD |
| Watchdog | Separate 128 kHz oscillator | Restarts a frozen program after 2 s |
| Logic levels at 5 V | HIGH above 3.0 V, LOW below 1.5 V | Every sensor output must clear these |

## Digital, analog and PWM

- A **digital input** reads only LOW or HIGH. Buttons, ECHO and DHT data are digital.
- An **analog input** uses a 10-bit successive-approximation ADC: 0 to 5 V becomes 0 to 1023, one step is
  5 / 1024 ≈ 4.9 mV. None of our sensors needs it: they send digital data (DHT, MPU6050) or a timed pulse
  (HC-SR04), which shrugs off small noise on the wires.
- `analogWrite()` is not analog. It is **PWM**: the pin switches fully on and off at a fixed frequency, and the
  fraction of time it is on (duty cycle) sets the average.

| PWM pins | Timer | Mode | Frequency |
| --- | --- | --- | --- |
| D5, D6 (our motor) | Timer0 | Fast PWM | 16 MHz / (64 × 256) ≈ 977 Hz |
| D9, D10 | Timer1 | Phase-correct | 16 MHz / (64 × 510) ≈ 490 Hz |
| D3, D11 | Timer2 | Phase-correct | ≈ 490 Hz |

Timer0 also drives `millis()`, so its settings must never be changed. The timer makes PWM by itself: it counts
0 to 255, sets the pin HIGH at 0 and LOW when the count matches the compare register; `analogWrite()` only
writes that register.

## Interrupts

An interrupt makes the CPU finish its current instruction, save its place, run a short function (the ISR) and
carry on. D3 fires on the falling edge, the instant the E-STOP button pulls it from 5 V to 0 V. The ISR cuts
PWM and drops the relay itself, so the stop never waits for the main loop. Latency is a few microseconds, except
for about 5 ms every 2.1 s while the DHT library blocks interrupts to time its 40 data bits.

## Watchdog

The watchdog is an independent timer with its own oscillator. The program resets it (`wdt_reset()`) on every
pass of `loop()`. If the code hangs for 2 s, the watchdog restarts the chip: every pin becomes an input, the relay
drops out, the motor stops, and the machine waits for GO. A `WATCHDOG_RESET` event is logged after the restart.

## Memory

The ATmega328P is a Harvard machine: program memory (flash) and data memory (SRAM) are separate. Text such as
`"PERSON IN ZONE"` would normally be copied into the 2 KB of SRAM at start-up; `F()` and `PROGMEM` keep it in
flash. With flash 99 % full, the optional INA219 current sensor and 12 V sense code are compiled out
(`HAS_INA219`, `HAS_12V_DIVIDER`).

## Safe start-up

At reset every pin is a high-impedance input until `setup()` runs, and the relay module's input is pulled up, so
the relay stays off. `setup()` writes the OFF level before making the pin an output, so the relay never flickers
on, then runs a lamp test and a sensor self-test. The motor never starts by itself.

## How it is powered here

SMPS +12 V goes to **VIN**, through the on-board regulator. The Arduino's 5V pin is not used: the sensors take
their 5 V from the SMPS rail, which keeps about 120 mA (and 0.8 W of regulator heat) off the board. With VIN above
about 6.6 V, the Uno switches its USB supply off, so the laptop and the SMPS can be connected at the same time.

## Pins used

See the [pin map in the README](../../README.md#wiring-at-a-glance). Not used: D0 and D1 (USB serial), D7, D12,
D13 and A3 (kept for an optional 12 V sense divider).
