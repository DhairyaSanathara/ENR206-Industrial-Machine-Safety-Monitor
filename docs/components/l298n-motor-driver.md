# L298N motor driver and PWM speed control

**Role in this project:** sets the motor's speed. The Arduino sends a PWM signal to ENA; the L298N switches the
12 V supply across the motor at that duty cycle.

## How an H-bridge works

Each channel of the L298 has four bipolar transistor switches arranged in an H, with the motor as the crossbar.
Turning on one top switch and the opposite bottom switch drives current through the motor one way; the other
diagonal pair reverses it.

| ENA | IN1 | IN2 | Motor |
| --- | --- | --- | --- |
| HIGH | HIGH | LOW | Forward |
| HIGH | LOW | HIGH | Reverse |
| HIGH | Same as IN2 | Same as IN1 | Brake: both terminals tied together, back-EMF stops it fast |
| LOW | Any | Any | Coast: outputs off, it spins down freely |

Each input drives one half of the bridge with complementary switches, so both transistors of one leg are never
on together (no shoot-through), even if IN1 and IN2 are both HIGH.

## What is on the module

- ST **L298** dual full-bridge IC: up to 46 V, 2 A per channel continuous (3 A peak), logic HIGH above 2.3 V.
- Heat sink, flyback diodes, a **78M05** regulator for the chip's 5 V logic supply, screw terminals.
- **12V-EN jumper**: with it on, the 78M05 makes the logic supply from the 12 V input (allowed up to 12 V). The
  module's 5V terminal is then an output: never feed another 5 V into it.
- **ENA / ENB jumper caps**: with a cap on, the channel is always enabled at full speed. We remove the ENA cap so
  the Arduino can drive ENA with PWM.

## Connections

| L298N pin | Goes to | Why |
| --- | --- | --- |
| +12 V | Relay NO | Motor supply, present only while the relay is on |
| GND | SMPS COM (common ground) | Shared reference with the Arduino |
| 5V terminal | Nothing | It is an output while the 12V-EN jumper is on |
| ENA (cap removed) | 10 kΩ resistor → Arduino D5 (signal also on D6) | PWM speed |
| IN1 | 5 V rail | Fixed direction |
| IN2 | Ground rail | Fixed direction |
| OUT1, OUT2 | Motor terminals (100 nF ceramic capacitor across them) | Swap them to reverse the direction |

The machine only turns one way, so IN1 and IN2 are tied to the rails and the Arduino needs only one signal for
the motor. The firmware sends the same PWM on D5 and D6 because one of those pins was suspected damaged on our
board; ENA works on either.

## The 10 kΩ resistor on ENA

It protects the Arduino pin if the wire lands on the wrong terminal (during the build it was once on the
module's 5V pin). ENA draws at most 100 µA, so the resistor drops at most 1 V: 4 V still reads as HIGH (above
2.3 V).

## PWM speed control

- At about 977 Hz (Timer0 on D5) the bridge switches the full supply on and off. The motor's inductance smooths
  the current and its inertia smooths the speed, so it follows the average:
  V_avg ≈ duty × (12 − 2) V.
- Our mapping: `PWM = PWM_MIN + (255 − PWM_MIN) × speed% / 100` with `PWM_MIN = 70`, so 10 % = PWM 88 and
  100 % = 255. Below about 27 % duty the geared motor cannot beat static friction and only hums, so the scale
  starts there.
- **Soft start.** PWM changes by 5 every 10 ms (`RAMP_STEP`), so 0 to full takes about 0.5 s. This limits inrush
  current and mechanical shock.
- **Why PWM and not a resistor?** A switch that is fully on or fully off wastes almost nothing (P = V × I is small
  at both ends); a series resistor would burn the dropped voltage as heat.
- **The whine.** About 1 kHz is audible, so the motor sings. A PWM above 20 kHz would be silent, but the slow
  bipolar transistors in the L298 would lose more power on every switch.

## Voltage drop and heat

Bipolar transistors keep a saturation voltage when on, and two are in the current path, so about 2 V is lost at
low current (more near 2 A). The motor sees about 10 V of the 12 V, and the L298N turns P = 2 V × I into heat:
1 W at 0.5 A. A MOSFET driver (TB6612FNG, BTS7960) loses only I² × R_DS(on).

## Test it

- `firmware/tests/pwm_finder` drives the same changing speed on D5, D6, D10 and D11; move the ENA wire from pin to
  pin to find one that controls the motor.
- `firmware/tests/motor_speed` sets the speed from the Serial Monitor (0 to 100 %, `+`, `-`, `x`).
