# Firmware

| Folder | What it is |
| --- | --- |
| `machine_safety_v2/` | The main program for the Arduino Uno |
| `tests/` | One small sketch per part, for bring-up and debugging (see [docs/testing.md](../docs/testing.md)) |

## Upload the main program

1. Arduino IDE → *Tools → Manage Libraries*, install:
   - **LiquidCrystal I2C** by Frank de Brabander
   - **DHT sensor library** by Adafruit (accept **Adafruit Unified Sensor**)
2. Open `machine_safety_v2/machine_safety_v2.ino`, board *Arduino Uno*, upload.
3. Serial Monitor at **9600 baud**, any line ending. Type `?` for the command list.

The MPU6050 is read straight from its registers, so it needs no library. If compiling fails at
`Wire.setWireTimeout`, update *Arduino AVR Boards* in the Boards Manager.

## Settings

Pins, limits and hardware options are constants at the top of the sketch (sections 1 to 4). The ones that most
often need changing are listed in [docs/wiring.md](../docs/wiring.md#settings-that-must-match-your-hardware).

Program size: 31,880 of 32,256 bytes of flash, 1,299 of 2,048 bytes of RAM. Turning on `HAS_INA219` or
`HAS_12V_DIVIDER` adds the optional current sensor or 12 V sense; only one of them fits at a time.

## Test sketches

| Sketch | Checks |
| --- | --- |
| `serial_test` | USB serial link |
| `power_check` | 5 V supply drops (loose wires) |
| `lcd_test` | LCD address, contrast, counter |
| `hello_lcd` | Continuous LCD and serial output; shows resets |
| `lcd_letters` | Moving letters with automatic LCD repair |
| `dht_test` | DHT11 / DHT22 detection and readings |
| `mpu_test` | MPU6050 acceleration, rotation, tilt, vibration |
| `pwm_finder` | Which PWM pin controls the motor speed |
| `motor_speed` | Relay plus L298N speed control from the Serial Monitor |

How the program works is explained in [docs/firmware.md](../docs/firmware.md).
