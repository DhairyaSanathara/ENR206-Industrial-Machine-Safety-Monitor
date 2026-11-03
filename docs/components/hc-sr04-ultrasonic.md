# HC-SR04 ultrasonic distance sensor

**Role in this project:** measures the distance to the nearest object in front of the machine, so the motor slows
when a person comes within 40 cm and stops within 15 cm.

## How it works

- **Echolocation at 40 kHz**, above the roughly 20 kHz limit of human hearing.
- Two **piezoelectric transducers**. In the transmitter, a voltage flexes the ceramic and makes sound (inverse
  piezoelectric effect). In the receiver, the returning pressure wave flexes it and makes a voltage (direct
  effect).
- **Sequence.** The Arduino sends a 10 µs HIGH pulse on TRIG. The module fires 8 cycles at 40 kHz and raises ECHO.
  When the echo returns, ECHO falls. The HIGH time is the round-trip time.
- **Distance** = time × speed of sound / 2, halved because the sound goes there and back. With 343 m/s (air at
  20 °C) = 0.0343 cm/µs:

```
distance (cm) = echo time (µs) × 0.0343 / 2  ≈  echo time / 58
```

**Why 40 kHz?** Inaudible, and the short wavelength (343 / 40,000 = 8.6 mm) reflects off small objects. Much
higher frequencies are absorbed by air too quickly.

## Connections

| HC-SR04 pin | Goes to |
| --- | --- |
| VCC | 5 V rail |
| GND | Common ground |
| TRIG | Arduino D11 |
| ECHO | Arduino D10 |

## Specifications

| | |
| --- | --- |
| Supply | 5 V, about 15 mA |
| Range | 2 to 400 cm (we use up to 150 cm) |
| Resolution | About 0.3 cm |
| Beam | About 15° wide |
| Time between pings | At least 60 ms |

## How the firmware uses it

- A ping every 70 ms (`PING_MS`), so the echo of the previous ping has died out.
- `pulseIn()` gives up after 10 ms (`ECHO_TIMEOUT_US`, about 171 cm). No echo is reported as 150 cm, "zone clear".
  Without a time-out, `pulseIn()` would wait up to 1 s and freeze the safety loop.
- The **median of the last three pings** is used, so one false echo cannot trip the machine. An average would
  let that one wild value drag the result.
- Warning below 40 cm, stop below 15 cm, with 5 cm of hysteresis: a warning clears only beyond 45 cm.
- Worst-case reaction: two pings must agree (up to about 140 ms) plus up to 100 ms to the next decision, about
  0.25 s in total.

## Limitations

- Soft cloth absorbs ultrasound; a flat surface at an angle reflects it away like a mirror.
- The 15° cone sees only straight ahead, and closer than 2 cm the transmitter is still ringing (blind zone).
- The speed of sound rises with temperature, c ≈ 331.3 + 0.606 T m/s: at 35 °C the reading is about 3 % short.
  The DHT reading could be used to correct it.
- **Fail-to-danger:** if the sensor is unplugged, ECHO stays LOW, `pulseIn()` returns 0 and the zone reads as
  clear. A fix is to aim it at a fixed reference and declare a FAULT if that reference disappears.
- For a real machine, ISO 13855 sets the minimum distance S = K × T + C with a hand speed K = 2,000 mm/s. At
  T = 0.25 s that is already 500 mm, so our 15 cm stop zone is for demonstration only; industry uses light
  curtains or laser scanners.

## Test it

Measure against a ruler at 15, 40 and 100 cm with the main program in readable-text mode (`n`): the Distance line
shows the filtered value.
