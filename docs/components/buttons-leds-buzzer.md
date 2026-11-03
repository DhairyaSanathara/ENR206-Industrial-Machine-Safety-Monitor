# Buttons, emergency stop, LEDs and buzzer

**Role in this project:** the machine's local controls and signals. They work with no PC attached: the operator
can always start, stop and see the state at the machine.

## Push buttons: GO (D2) and E-STOP (D3)

- **Wiring.** One leg to the pin, the other to ground. The pin is set to `INPUT_PULLUP`, which switches on an
  internal resistor of about 20 to 50 kΩ to 5 V: released reads HIGH, pressed reads LOW (active-low). No external
  resistor is needed.
- **Why a pull-up?** An unconnected input floats: its very high impedance lets stray charge and noise decide the
  level, so it reads random values.
- **Bounce.** Metal contacts bounce for a few milliseconds, giving several edges per press. GO must be steady for
  25 ms before it counts.
- **E-STOP on an interrupt.** D3 is external interrupt INT1, triggered on the falling edge. The ISR re-reads the
  pin to ignore a spike, sets a `volatile` flag, sets PWM to 0 and drops the relay, all within microseconds.
  Bounce does no harm: stopping twice is still stopped.
- **Latching.** Releasing E-STOP does not restart anything. Press GO once to reset (only if every cause has
  cleared) and again to start. ISO 13850 requires that resetting an emergency stop does not restart the machine.

### Limitation

A real emergency stop is a red mushroom button on a yellow background with a normally closed (NC) contact that is
forced open when pressed, so a broken wire also stops the machine. It is wired into the power circuit through a
safety relay, so it works even if the software does not. Ours is a normally open push button read by the
microcontroller. The firmware supports a real NC button (`ESTOP_BUTTON_IS_NC = true`), and wiring its contact in
series with the relay module's supply would make the stop independent of the software.

## LEDs: green (A0), yellow (A1), red (A2)

- **Principle.** A p-n junction: forward current makes electrons and holes recombine, and each recombination
  releases a photon with energy close to the bandgap, E = h c / λ. Red light at about 630 nm is about 1.97 eV.
- **Forward voltage follows the bandgap:** red about 2 V, green and blue (InGaN) about 3 V.
- **Why a 220 Ω resistor?** LED current rises exponentially with voltage, so the voltage alone cannot set it.
  (5 − 2) / 220 ≈ 14 mA is safe for the LED and the pin.
- **Polarity.** Long leg = anode (+) to the resistor; short leg and flat edge = cathode (−) to ground.
- **Why A0 to A2?** Analog pins double as digital outputs (port C), which keeps the D pins free.

| State | Green | Yellow | Red | Buzzer |
| --- | --- | --- | --- | --- |
| IDLE | Slow blink | On if a danger is present | | |
| RUN | On (fast blink while learning) | | | |
| WARNING | | On | | Short beep every second |
| ESTOP, danger still present | | | Fast flash | Fast alarm |
| ESTOP, safe to reset | | | On | Chirp every 2 s |
| FAULT | | Alternating with red | Alternating with yellow | Chirp every 3 s |

At power-up a lamp test lights all three LEDs and sounds the buzzer, proving them before the machine can run.

## Active buzzer (D8)

- **Active vs passive.** An active buzzer has its own oscillator, so steady DC gives a fixed tone (about 2 to
  4 kHz). A passive one is only a transducer and needs a square wave from `tone()`.
- **Inside:** a piezo disc that flexes with voltage (inverse piezoelectric effect) or a small coil pulling a metal
  diaphragm.
- **Limitation.** It draws about 25 to 30 mA straight from D8: inside the 40 mA absolute maximum but above the
  20 mA recommendation. A small transistor driver would be better.
