# SMPS and power distribution

**Role in this project:** one dual-output switch-mode power supply turns 230 V AC mains into 12 V (motor and the
Arduino's VIN) and 5 V (sensors, LCD, relay coil). All negatives meet at one common ground.

## How an SMPS works

1. **Rectify.** An input filter and bridge rectifier turn 230 V AC into about 325 V DC (peak = 230 × √2),
   smoothed by a large capacitor.
2. **Chop.** A MOSFET switches that DC on and off at tens of kHz into a small ferrite transformer (a flyback
   design at this size).
3. **Step down and smooth.** Secondary windings give the low voltages; fast diodes rectify them; inductors and
   capacitors filter the ripple.
4. **Regulate.** Each output is compared with a reference (often a TL431) and the error crosses back over an
   optocoupler. The controller adjusts the duty cycle so 12 V and 5 V stay steady as the load changes.

**Why switching?** A transformer's core can shrink as frequency rises (V = 4.44 f N A B), so at tens of kHz it is
tiny compared with a 50 Hz one. The switches are either fully on or fully off, so efficiency is typically 80 to
90 %; a linear regulator would burn the extra voltage as heat.

**Why is the low-voltage side safe to touch?** The transformer and the optocoupler give galvanic isolation: no
conductor joins mains to the outputs. The metal case is connected to mains earth so a fault blows the fuse
instead of electrifying the case.

## Rails

| Rail | Feeds | Why |
| --- | --- | --- |
| +12 V | Relay COM (then the L298N and the motor), Arduino VIN | The motor is rated 12 V; VIN accepts 7 to 12 V |
| +5 V | Breadboard + rail: HC-SR04, DHT, MPU6050, LCD, relay module VCC, L298N IN1 | Keeps sensor current off the Arduino's small regulator |
| COM (0 V) | Breadboard − rail, Arduino GND, L298N GND | One shared reference for every signal |

## Why a common ground

A voltage only means something relative to a reference. The L298N reads ENA against its own GND; if that GND is
not joined to the Arduino's, the PWM "5 V" has no defined meaning at the driver and the motor behaves erratically
or not at all.

## Why VIN and not the Arduino's 5V pin

VIN passes through the board's regulator and protection. Feeding 5 V into the 5V pin bypasses both, and with
the laptop plugged in two 5 V sources would fight. With VIN above about 6.6 V the Uno disconnects USB power by
itself, so USB and the SMPS can be connected together.

The sensors do not use the Arduino's 5V pin either: at about 120 mA, the regulator would have to burn
(12 − 5) × 0.12 ≈ 0.84 W extra.

## Power budget (typical datasheet values)

| Load | Supply | Current |
| --- | --- | --- |
| Relay coil (5 V, about 70 Ω) | 5 V rail | About 70 mA while on |
| LCD with backlight | 5 V rail | About 20 to 40 mA |
| HC-SR04 | 5 V rail | About 15 mA |
| MPU6050 module | 5 V rail | About 4 to 5 mA |
| DHT11 / DHT22 | 5 V rail | About 1 to 2.5 mA while measuring |
| Arduino board | 12 V through VIN | About 50 mA |
| One LED (from a pin) | Arduino regulator | (5 − 2) / 220 ≈ 14 mA |
| Active buzzer (from D8) | Arduino regulator | About 25 to 30 mA |
| Motor | 12 V through relay and L298N | Load-dependent; the stall current on the motor label is the worst case |

The 12 V output must supply the motor's stall current plus about 0.2 A for everything else. Check the amp rating
of each output on your SMPS label.

## Safety when wiring

- Wire everything with the SMPS unplugged.
- Mains terminals (L, N, earth) must be covered; use a 3-pin plug with earth on the metal case.
- Never connect the 12 V output to any Arduino pin other than VIN.
- Earlier in the build the motor was mistakenly fed from the 5 V output; its current spikes reset the electronics.
  The motor belongs on 12 V only.
