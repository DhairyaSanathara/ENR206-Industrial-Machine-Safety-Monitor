# Serial protocol

USB serial at **9600 baud**, 8 data bits, no parity, 1 stop bit (8N1): 10 bits per byte, 960 bytes per second.
Commands may end with any line ending.

## Output modes

| Command | Mode | What is printed |
| --- | --- | --- |
| `n` (default after reset) | Readable text | A labelled report every 2 s, for the Serial Monitor |
| `d` | Data | CSV lines for the website and Excel, a row every 0.5 s |
| `v`, `t`, `b` | Serial Plotter | Vibration, temperature, or both, as plotter graphs |

The Arduino restarts when the port is opened and starts in text mode, so the website's server sends `d` until
data rows arrive.

### Readable text example

```
--- RUN | speed 60% (set 60%) | ALL OK
Temperature : 27.4 C (warn 40 / stop 50), rising 0.30 C/min
Vibration   : 35.5 mg (warn 71 / stop 123)
Distance    : 120 cm   Tilt: 0.4 deg   Humidity: 52 %
```

## Data mode lines

Every line starts with a letter that says what it is. Lines starting with `#` are notes for people.

### `H`: column names

```
H,ms,state,reason,dist_cm,temp_c,temp_rate_cpm,vib_mg,tilt_deg,current_ma,bus_v,power_w,pwm,vib_warn_mg,vib_stop_mg,cur_warn_ma,cur_stop_ma,humidity_pct,speed_pct,speed_now_pct
```

The website reads this line, so a column added later does not break it.

### `L`: fixed limits

```
L,dist_warn_cm=40,dist_stop_cm=15,temp_warn_c=40,temp_stop_c=50,rate_warn_cpm=6,rate_stop_cpm=15,tilt_warn_deg=10,tilt_stop_deg=25
```

### `D`: one row of readings, every 0.5 s

```
D,9205,RUN,OK,120.0,30.00,0.00,36.1,0.1,0,0.00,0.00,255,71,123,150,195,45.0,100,100
```

| Column | Unit | Meaning |
| --- | --- | --- |
| `ms` | ms | Time since the Arduino started |
| `state` | | IDLE, RUN, WARNING, ESTOP or FAULT |
| `reason` | | Reason code (below) |
| `dist_cm` | cm | Filtered distance; 150 = nothing in range |
| `temp_c` | °C | Air temperature at the motor |
| `temp_rate_cpm` | °C/min | Rise rate (least-squares slope over about 30 s) |
| `vib_mg` | mg | Vibration RMS over the last 0.5 s |
| `tilt_deg` | ° | Tilt from the installed position |
| `current_ma`, `bus_v`, `power_w` | mA, V, W | Optional INA219 (0 when not fitted) |
| `pwm` | 0 to 255 | Duty cycle on ENA right now |
| `vib_warn_mg`, `vib_stop_mg` | mg | Learned limits at this speed (800 = not learned yet) |
| `cur_warn_ma`, `cur_stop_ma` | mA | Optional current limits |
| `humidity_pct` | % RH | Relative humidity |
| `speed_pct` | % | Speed set point |
| `speed_now_pct` | % | Speed the motor is actually being driven at (lower in WARNING, ramps) |

### `E`: events

```
E,<ms>,<type>,<details>
```

| Type | Example | Meaning |
| --- | --- | --- |
| `SELFTEST` | `E,1205,SELFTEST,MPU6050=OK DHT22=OK INA219=not fitted 12V_SENSE=not fitted LCD=OK` | Power-up sensor check |
| `STATE` | `E,12222,STATE,WARNING,PROX` | New state and its reason |
| `SPEED` | `E,7205,SPEED,80` | New speed set point in % |
| `BASELINE` | `E,8782,BASELINE,speed_pct=100 vib_base_mg=35.3 vib_warn=71 vib_stop=123` | Normal vibration learned at a speed |
| `RELEARN` | `E,40210,RELEARN` | Baselines cleared by `l` |
| `TILT_REZERO` | `E,41000,TILT_REZERO` | Current position stored as level by `z` |
| `WATCHDOG_RESET` | `E,1300,WATCHDOG_RESET` | The previous run was ended by the watchdog |
| `I2C_BUS_ERROR`, `I2C_BUS_OK` | `E,52011,I2C_BUS_ERROR` | The I2C wires stopped and started responding |

In text mode the same events print as `>> ...` lines, for example `>> SELFTEST,MPU6050=OK ...`.

### Reason codes

| Code | LCD text | Meaning |
| --- | --- | --- |
| `OK` | ALL OK | Nothing wrong |
| `ESTOP_BTN` | E-STOP BUTTON | Emergency stop button pressed |
| `REMOTE` | REMOTE STOP | `x` received from the PC or website |
| `RELAY_STUCK` | RELAY STUCK ON | 12 V present after the relay opened (optional sense divider) |
| `NO_POWER` | NO MOTOR POWER | No 12 V at the motor while running (optional sensor) |
| `SENS_TEMP` | TEMP SENSOR ERR | DHT not answering |
| `SENS_MPU` | MPU6050 ERROR | MPU6050 missing or frozen |
| `SENS_INA` | INA219 ERROR | Optional current sensor not answering |
| `PROX` | PERSON IN ZONE | Someone too close |
| `OVERLOAD` | OVERLOAD / JAM | Motor current too high (optional sensor) |
| `STALL` | MOTOR STALLED | Vibration collapsed at steady speed |
| `OVERHEAT` | MOTOR OVERHEAT | Temperature limit |
| `TEMP_RATE` | TEMP RISING FAST | Rise-rate limit |
| `VIBRATION` | HIGH VIBRATION | Vibration limit |
| `TILT` | MACHINE TILTED | Tilt limit |

## Commands

| Command | Action |
| --- | --- |
| `g` | GO: start, or reset after a stop |
| `x` | Emergency stop |
| `10` … `100` | Speed set point in % (rounded to steps of 10) |
| `+`, `-` | Speed ±10 % |
| `s` | Status report (`#` lines) |
| `z` | Re-zero tilt |
| `l` | Re-learn vibration baselines |
| `n`, `d`, `v`, `t`, `b` | Output mode (above) |
| `?` | Help |

The website's server only forwards `g x z l s d + -` and numbers; output-mode commands are kept for the server
itself.
