# Limitations and future work

This is a teaching prototype that demonstrates the logic of machine safeguarding. It is not a certified safety
system.

## Design choices and their costs

| We chose | Instead of | Why | What it costs |
| --- | --- | --- | --- |
| Arduino Uno | ESP32, STM32 | 5 V logic matches the sensors and the L298N; simple, predictable timing; available in the lab | 2 KB RAM, flash 99 % full; no Wi-Fi, so the PC is the gateway |
| HC-SR04 | PIR, IR distance sensor, time-of-flight laser, light curtain | A real distance in light or dark, cheap | 15° cone, one direction, misses soft or angled surfaces |
| DHT11 / DHT22 | DS18B20 on the motor case, thermistor, thermocouple | Digital output, adds humidity, available | Measures air, not the motor; slow |
| MPU6050 | SW-420 vibration switch, piezo disc | A number for vibration (RMS) plus tilt from one chip | 50 Hz bandwidth at our sample rate; acceleration, not ISO velocity |
| L298N plus relay | MOSFET drivers (TB6612FNG, BTS7960) | Robust, cheap, in the lab; the relay adds a physical break | About 2 V lost as heat |
| Learned vibration limits | Fixed limits | Fit every speed and machine | Must be learned on a healthy machine |
| 9600 baud | 115200 baud | Course requirement | 960 bytes/s; enough for 2 rows a second |

## Known limitations

1. **Not safety-rated.** Real machines use safety relays and dual-channel emergency-stop circuits designed to
   ISO 13849-1 performance levels or IEC 62061.
2. **Emergency stop.** A normally open push button read by the microcontroller. A real one is a positive-opening
   NC mushroom button wired into the power circuit (ISO 13850).
3. **RPM is estimated** from the PWM duty and the motor's rated RPM, not measured.
4. **No current sensing.** The INA219 code is compiled out to fit the flash, so overload is detected indirectly:
   a jam by vibration collapse, heating by temperature.
5. **Temperature** is the air next to the motor, read every 2 s.
6. **One ultrasonic sensor** covers one direction. If it is unplugged it reads "zone clear": a fail-to-danger
   mode.
7. **Safety distance.** ISO 13855 (S = K × T + C, hand speed 2,000 mm/s) would ask for about 500 mm at our 0.25 s
   reaction time; the 15 cm stop zone is for demonstration.
8. **Vibration bandwidth** is 50 Hz, and the unit is acceleration rather than velocity (ISO 10816 / 20816).
9. **A welded relay** is not detected unless the optional 12 V sense divider on A3 is fitted.
10. **The buzzer** runs straight from a pin, above the recommended 20 mA.
11. **Breadboard wiring** caused intermittent faults during the build; a soldered board with screw terminals would
    remove them.

## Future work

- Measured RPM with an IR or Hall-effect sensor on the shaft, counted on an interrupt.
- Current sensing (INA219 or ACS712) for direct overload and stall detection, on a board with more flash.
- A DS18B20 or thermistor fixed to the motor case.
- Speed-of-sound correction using the DHT's own temperature.
- A reference target for the ultrasonic sensor, so a dead sensor is detected.
- Faster vibration sampling (1 kHz with the MPU6050's FIFO) and an FFT to find imbalance and bearing frequencies,
  reported in mm/s.
- A hardwired NC emergency stop through a safety relay.
- Learned baselines saved in EEPROM.
- ESP32 with MQTT for remote monitoring without a PC.
- A gas or smoke sensor (for example MQ-2) for fire and leak detection.
- A small UPS or supercapacitor so the controller can log and shut down cleanly in a power cut.
