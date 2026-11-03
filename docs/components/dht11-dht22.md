# DHT11 / DHT22 temperature and humidity sensor

**Role in this project:** measures the air temperature next to the motor (overheating and how fast it is heating
up) and the humidity (shown and logged only). The firmware works with either model and detects which one is fitted.

## What is inside

A humidity element, an NTC thermistor, and a small microcontroller that measures both, applies the factory
calibration stored in its memory, and sends the result as digital data.

- **Temperature: NTC thermistor.** A metal-oxide semiconductor. Heat excites more charge carriers across its
  bandgap, so resistance falls as temperature rises: roughly R = R₀ e^(B (1/T − 1/T₀)) with T in kelvin. (In a
  metal the opposite happens: more lattice vibration, lower mobility, higher resistance.)
- **Humidity, DHT11: resistive.** Water absorbed by a hygroscopic layer between two electrodes adds conducting
  ions, so resistance falls as humidity rises.
- **Humidity, DHT22: capacitive.** A thin polymer film is the dielectric of a capacitor. Water (relative
  permittivity about 80, against 3 to 5 for the polymer) soaks into it and raises the capacitance.

| | DHT11 | DHT22 (AM2302) |
| --- | --- | --- |
| Temperature range | 0 to 50 °C | −40 to 80 °C |
| Temperature accuracy | ±2 °C | ±0.5 °C |
| Humidity range | 20 to 80 % | 0 to 100 % |
| Humidity accuracy | ±5 % | ±2 to 5 % |
| Resolution | 1 °C, 1 % | 0.1 °C, 0.1 % |
| Fastest reading | Once per second | Once per 2 s |
| Humidity element | Resistive | Capacitive |

## The one-wire protocol

This is the maker's own protocol, not Dallas 1-Wire.

1. The Arduino pulls the line LOW (at least 18 ms for a DHT11, about 1 ms for a DHT22), then releases it.
2. The sensor answers LOW for 80 µs and HIGH for 80 µs.
3. It sends 40 bits. Every bit starts with 50 µs LOW; the HIGH that follows lasts 26 to 28 µs for a 0 and
   70 µs for a 1.
4. The 40 bits are 16 bits of humidity, 16 of temperature and an 8-bit checksum: the low byte of the sum of the
   first four bytes. A reading that fails the checksum is rejected.

Because a 0 and a 1 differ only in timing, the library switches interrupts off for about 5 ms while it times the
bits.

## Connections

| DHT pin | Goes to |
| --- | --- |
| + (VCC) | 5 V rail |
| − (GND) | Common ground |
| DATA | Arduino D4 |

The line is open-drain: both sides only pull it LOW. A 10 kΩ pull-up to 5 V makes it idle HIGH; 3-pin modules have
it built in, a bare 4-pin sensor needs one added.

## How the firmware uses it

- **Model detection.** The two models pack their numbers differently. A DHT11 read as a DHT22 gives humidity over
  100 %; a DHT22 read as a DHT11 gives a temperature below 3 °C. The code tries both on the first frame and keeps
  the one that makes sense.
- A reading every 2.1 s (`TEMP_MS`).
- **Level limits:** warning at 40 °C, stop at 50 °C, 2 °C of hysteresis.
- **Rise rate:** a least-squares straight line through the last 15 readings (about 31 s). Its slope in °C per
  minute warns above 6 and stops above 15, catching fast heating before the level limit. One noisy reading barely
  moves a fitted line, while "last minus first" would jump.
- Repeated failed readings put the machine into FAULT (temperature sensor).

## Limitations

- It measures the air next to the motor, not the windings, and responds in seconds. A DS18B20, thermistor or
  thermocouple fixed to the motor case would read the motor itself.
- A DHT11's 1 °C steps make its rise rate coarse.

## Test it

`firmware/tests/dht_test` detects the model and prints a reading every 2 s.
