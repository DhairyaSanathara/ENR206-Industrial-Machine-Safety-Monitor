# I2C bus and the 16×2 LCD

**Role in this project:** the MPU6050 and the LCD share two wires. The LCD shows the state, the reason for any
stop, and live readings at the machine itself, with no PC needed.

## How I2C works

- **Two wires plus ground:** SDA (data, Arduino A4) and SCL (clock, A5).
- **Roles.** The Arduino is the controller: it drives the clock and starts every transfer. Each device answers
  only when its 7-bit address is called.
- **Open drain.** Devices can only pull a line LOW; pull-up resistors on the modules pull it HIGH. Two devices can
  never short 5 V against 0 V.
- **One transfer:** START (SDA falls while SCL is HIGH) → 7-bit address + read/write bit → the addressed device
  pulls SDA LOW on the 9th clock (ACK) → data bytes, each with an ACK → STOP (SDA rises while SCL is HIGH). Data may
  change only while SCL is LOW.
- **Speed.** We run 100 kHz (standard mode): a byte with its ACK is 9 clocks, 90 µs.
- **Our addresses:** MPU6050 0x68; LCD backpack 0x27 (PCF8574) or 0x3F (PCF8574A). Two devices on one address
  would collide: the MPU6050's AD0 pin and the backpack's A0 to A2 pads change them.
- **Limits.** I2C is meant for short wires on one board (bus capacitance up to 400 pF). Long jumpers round off
  the edges and pick up motor noise.

### Surviving a bus jam

If a device is cut off mid-byte by noise or a loose wire, it can hold SDA LOW forever, and the standard Wire
library would wait forever too. The firmware sets `Wire.setWireTimeout(25000, true)`: a stuck transfer is
abandoned and the I2C hardware reset. `I2C_BUS_ERROR` and `I2C_BUS_OK` events are logged, a sensor that stays
lost causes a FAULT, and the watchdog is the final backstop.

## The LCD

- **The display.** Liquid crystal molecules twist polarised light; a voltage across a segment untwists them, so it
  turns dark between crossed polarisers. Each character is a 5 × 8 dot matrix with an LED backlight.
- **The controller.** An HD44780 (or compatible) keeps the text in display RAM (line 1 starts at address 0x00,
  line 2 at 0x40) and turns character codes into dot patterns from its built-in ROM.
- **The backpack.** A PCF8574 I/O expander turns each I2C byte into 8 parallel pins: P0 RS (command or text),
  P1 RW, P2 E (enable strobe), P3 backlight, P4 to P7 to the LCD's D4 to D7. With only four data lines the LCD
  runs in **4-bit mode**: every byte is sent as two halves (nibbles), each latched by a pulse on E.
- **Contrast.** The potentiometer on the backpack sets the liquid crystal drive voltage. Set wrong, the screen is
  blank or shows solid blocks.
- **Why the I2C version?** A bare LCD needs at least six Arduino pins; the backpack uses the two I2C pins we
  already have.

## Connections

| LCD backpack pin | Goes to |
| --- | --- |
| VCC | 5 V rail |
| GND | Common ground |
| SDA | Arduino A4 |
| SCL | Arduino A5 |

## Self-healing display

If noise loses one nibble, the LCD pairs the wrong halves and every later character is garbage. Every 10 s the
firmware sends the reset sequence 0x3, 0x3, 0x3, 0x2: the three 0x3s force 8-bit mode from any state, then 0x2
returns to 4-bit mode with the halves realigned, and the screen is redrawn. The display is refreshed every
250 ms and its second line changes page every 2 s.

## Test it

- `firmware/tests/lcd_test` finds the LCD's address and shows a counter.
- `firmware/tests/hello_lcd` prints continuously and shows whether the Arduino is restarting.
- `firmware/tests/lcd_letters` slides letters across both lines and repairs the screen itself after a wire
  glitch, printing each dropout to the Serial Monitor.
