# 12 V DC geared motor

**Role in this project:** the "machine" being protected. Its speed is set from the dashboard; the safety system
watches its vibration, temperature and surroundings.

## How a brushed DC motor works

- **Torque.** Current in the armature windings, inside a permanent magnet's field, feels a force F = B I L
  (Lorentz force). The commutator and brushes reverse the current every half turn so the torque keeps one
  direction.
- **Back-EMF.** A spinning motor is also a generator: E = k ω. In steady state V = I R + k ω, so
  ω = (V − I R) / k. At light load speed is roughly proportional to voltage, and torque T = k I is proportional to
  current.
- **Start-up and stall.** When ω = 0 there is no back-EMF and the current is V / R, the largest the motor can draw.
  That is why starting is soft (0.5 s ramp) and why a jam is dangerous: it overheats the windings and the driver.
- **Gearbox.** Cuts the speed by the gear ratio and multiplies the torque by roughly the same ratio, minus
  friction.

## Connections

OUT1 and OUT2 of the L298N to the motor terminals; swapping them reverses the direction. A **100 nF ceramic
capacitor** across the terminals shorts the high-frequency noise made by the brushes sparking at each
commutation, so it does not disturb the I2C bus or reset the Arduino.

## Speed and RPM

The dashboard shows RPM as an **estimate**, because the motor has no speed sensor:

```
RPM ≈ rated RPM × (PWM / 255) × (10 V / 12 V)
```

The L298N drops about 2 V, so at full duty the motor gets about 10 V of 12 V. Set the rated RPM printed on your
motor in the Shaft speed tile or with `python server.py --rated-rpm 300`. Under load the real speed is lower.
To measure it, add an IR slot sensor or a Hall sensor with a magnet on the shaft and count pulses with an
interrupt: RPM = pulses per second × 60 / pulses per turn.

## What the safety system watches

| Signal | Why it matters for a motor |
| --- | --- |
| Vibration RMS (MPU6050) | Imbalance, a loose mount, worn bearings or gears raise it |
| Vibration collapse at steady speed | The shaft has stopped: a stall or jam |
| Air temperature and its rise rate (DHT) | Overload and jams heat the motor |
| Tilt (MPU6050) | The machine has shifted or been knocked over |
| Distance (HC-SR04) | A person within reach of moving parts |

## Mounting

Fix the MPU6050 to the same base as the motor. Vibration travels through the structure; a sensor on a separate
surface feels almost nothing.
