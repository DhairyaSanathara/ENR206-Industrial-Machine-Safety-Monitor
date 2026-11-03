# Relay module (5 V, single channel)

**Role in this project:** the hard power switch. Its contact sits in the motor's +12 V line, so when the relay is
off there is a physical air gap between the supply and the L298N.

## How a relay works

Current through a coil makes a magnetic field that pulls a spring-loaded armature. The armature moves the common
contact (COM) from the normally closed contact (NC) to the normally open one (NO). When the coil current stops,
the spring pulls it back.

## What is on the module

| Part | Job |
| --- | --- |
| Relay (SRD-05VDC-SL-C type) | 5 V coil of about 70 Ω (about 71 mA, 0.36 W); contacts rated 10 A at 250 V AC or 30 V DC |
| Transistor | Switches the coil current, which a pin could not supply |
| Flyback diode across the coil | Absorbs the voltage spike (V = −L di/dt) when the coil switches off |
| Optocoupler (on many modules) | LED and phototransistor: the control signal crosses as light, isolating the Arduino |
| LED | Shows the coil is energised |

## Connections

| Module pin | Goes to | Why |
| --- | --- | --- |
| VCC | 5 V rail | Coil and module electronics |
| GND | Common ground | Reference |
| IN | Arduino D9 | Control signal, active-low |
| COM | SMPS +12 V | Supply side of the switch |
| NO | L298N +12 V | Motor side: connected only while the coil is on |
| NC | Not used | Would be connected while the relay is off |

## Design decisions

- **Active-low.** IN LOW lets current flow from VCC through the opto's LED into the pin, which turns the relay on.
  A disconnected or HIGH input means off, so a broken wire stops the motor (`RELAY_ACTIVE_LOW = true`).
- **NO contact, energise to run.** An unpowered relay leaves the motor disconnected. If the Arduino resets, loses
  power or its wire breaks, the motor stops. This is the fail-safe direction.
- **Why a relay when the L298N can already stop the motor?** The L298N is a semiconductor: a shorted transistor or
  a software fault could leave it on. The relay is a different technology with a different way of failing, and
  it opens a real gap. Two independent barriers.
- **Sequencing.** On start, the relay closes first and PWM begins 150 ms later (`RELAY_SETTLE_MS`), so the
  contacts never switch motor current, which reduces arcing. On a normal stop the PWM ramps to 0 first and the
  relay opens afterwards. On an emergency stop both happen at once.

## Why DC is rated lower than AC

The contacts are rated 250 V AC but only 30 V DC. AC passes through zero 100 times a second (50 Hz), which puts
out the arc as the contacts open. DC never passes through zero, so the arc lasts longer and burns the contacts.

## Limitations

- A welded contact would not be noticed. The firmware can check for 12 V after the relay through an optional
  10 kΩ / 4.7 kΩ divider on A3 (`HAS_12V_DIVIDER`), compiled out here to save flash.
- In industry this job is done by a contactor or a safety relay with force-guided contacts.

## Test it

`firmware/tests/motor_speed` switches the relay on before driving the motor; you should hear it click and see its
LED.
