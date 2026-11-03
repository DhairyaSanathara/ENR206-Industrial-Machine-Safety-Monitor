# Calculations

The numbers behind every setting, worked with the values used in the firmware.

## Distance (HC-SR04)

| Question | Working | Result |
| --- | --- | --- |
| Speed of sound in µs | 343 m/s = 34,300 cm/s = 0.0343 cm/µs | 1 cm of distance = 58 µs of echo |
| Echo of 2,332 µs | 2,332 × 0.0343 / 2 | 40.0 cm, the warning edge |
| Echo at the 15 cm stop limit | 2 × 15 / 0.0343 | About 875 µs |
| Farthest distance before the 10 ms time-out | 10,000 × 0.0343 / 2 | 171.5 cm |
| Error at 35 °C if 343 m/s is assumed | c = 331.3 + 0.606 × 35 = 352.5 m/s; (352.5 − 343) / 343 | 2.8 %: a real 40 cm reads 38.9 cm |
| Worst-case detection time | 2 pings × 70 ms + 100 ms decision period | About 0.25 s |

## Motor speed

| Question | Working | Result |
| --- | --- | --- |
| PWM at 60 % | 70 + 185 × 0.60 | 181 of 255, duty 71 % |
| Average motor voltage at 60 % | 0.71 × (12 − 2) V | About 7.1 V |
| Estimated RPM at 60 %, motor rated 300 RPM | 300 × (181 / 255) × (10 / 12) | About 177 RPM |
| Speed in WARNING | 70 + 185 × 0.45 = 153; 300 × (153 / 255) × (10 / 12) | Duty 60 %, about 150 RPM |
| PWM frequency on D5 | 16,000,000 / (64 × 256) | 977 Hz, period 1.02 ms |
| Soft-start time | 255 / 5 steps × 10 ms | About 0.5 s |
| Heat in the L298N at 0.5 A | 2 V × 0.5 A | 1 W |

## Electronics

| Question | Working | Result |
| --- | --- | --- |
| LED current with 220 Ω | (5 − 2) / 220 | 13.6 mA; resistor heat 0.04 W |
| Photon energy of red light at 630 nm | E (eV) = 1240 / λ (nm) | 1.97 eV, close to the LED's 2 V |
| Relay coil current | 5 V / 70 Ω | About 71 mA, 0.36 W: needs a transistor |
| Voltage at ENA through 10 kΩ | Worst case 100 µA × 10 kΩ = 1 V dropped | At least 4 V, above the 2.3 V HIGH threshold |
| ADC step | 5 V / 1024 | 4.9 mV |
| Optional 12 V sense divider on A3 | 12 × 4.7 / (10 + 4.7) = 3.84 V; 3.84 / 5 × 1023 | About 785 counts |
| Heat in the Arduino's regulator at 100 mA | (12 − 5) × 0.1 | 0.7 W |

## Vibration and tilt (MPU6050 at ±4 g)

| Question | Working | Result |
| --- | --- | --- |
| Counts per g | 32,768 / 4 | 8,192; one count = 0.12 mg |
| Summed variance of 84,000 counts² | √84,000 = 290 counts; 290 / 8,192 × 1,000 | About 35 mg RMS |
| Gravity was (0, 0, 8192), now (1422, 0, 8068) | cos θ = 8068 / √(1422² + 8068²) = 8068 / 8192 | θ ≈ 10°, the tilt warning |
| Highest vibration frequency seen | Nyquist: 100 samples/s / 2 | 50 Hz |
| Output shaft frequency at 177 RPM | 177 / 60 | 2.95 Hz |
| Time to learn a new speed | 2 s spin-up + 8 × 0.5 s | About 6 s |

## Temperature

| Question | Working | Result |
| --- | --- | --- |
| Temperature climbs 0.2 °C per reading | 0.2 × 60 / 2.1 | 5.7 °C/min, just under the 6 °C/min warning |
| Length of the rise-rate window | 15 readings × 2.1 s | About 31 s |

## Communication and timing

| Question | Working | Result |
| --- | --- | --- |
| Bytes per second at 9600 baud | 9600 / 10 bits per byte | 960 bytes/s |
| Time to send one 87-byte data row | 87 / 960 | About 91 ms (two rows a second use under a fifth of the link) |
| One I2C byte at 100 kHz | 9 clocks × 10 µs | 90 µs; one MPU6050 reading is under 1 ms |
| Watchdog time-out | 256 K cycles of a 128 kHz oscillator | About 2 s |
| Time before millis() rolls over | 2³² ms | 49.7 days (handled by unsigned subtraction) |
