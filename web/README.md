# Industrial Machine Safety Monitor – website

ENR206 project, Ahmedabad University. Guided by Prof. Aditya Tiwari.
Team: Dhairya Sanathara, Kaushal Chitlange, Mahi Mistry, Kavan Vyas.

A one-page website that shows every reading from the Arduino live and controls the
machine from the lab PC. It replaces the Arduino IDE: you don't need the Serial
Monitor or the Plotter.

## Files

| File | What it is |
|---|---|
| `index.html` | The website |
| `server.py` | Small Python server that talks to the Arduino over USB (9600 baud) and serves the website |
| `start_monitor.bat` | Double-click to start everything (Windows) |
| `start_demo.bat` | Same, with a demo machine instead of the Arduino (for practice and screenshots) |
| `au_logo.png` | **Add this yourself**: the official Ahmedabad University logo (PNG, SVG or JPG named `au_logo.*`) |
| `data/` | Created automatically. One CSV of every reading per session, plus an events file |

The Arduino firmware (`machine_safety_v2.ino`) needs no changes.

## One-time setup

1. Install Python 3 from python.org. During setup, tick **Add python.exe to PATH**.
2. Copy the university logo into this folder as `au_logo.png`.

`start_monitor.bat` installs the only extra package (`pyserial`) by itself. To do it
by hand: `python -m pip install pyserial`.

## Every time

1. Plug in the Arduino and switch on the SMPS.
2. **Close the Arduino IDE's Serial Monitor and Serial Plotter.** Only one program can
   use the USB port at a time.
3. Double-click `start_monitor.bat`. The browser opens at http://localhost:8000.

The server finds the Arduino by itself. If it picks the wrong port, name it:

    python server.py --port COM11

The black window shows the address for phones, for example
`http://192.168.1.23:8000`. Leave that window open; closing it stops the website.

## What the website does

- Start, Reset, emergency stop (the red button, or the **Esc** key), speed set point
  10–100 %, re-zero tilt, re-learn vibration.
- Live values: speed, shaft RPM, temperature and its rise rate, vibration, distance to
  the nearest person, tilt, humidity, each with its warning and stop limits.
- Three live graphs: vibration (with the learned warning and stop lines), temperature
  and shaft speed (actual and set point). 1, 2 or 5 minutes.
- Protection checks: which of the nine safety checks is normal, warning or tripped.
- Session counters: starts, emergency stops, warnings, faults, speed changes, running
  time, peak temperature and vibration, closest approach, readings logged.
- System health: data link, sensor self-test, LCD and I2C bus, watchdog restarts.
- Event log, the raw serial lines, a command box and a CSV download.

## Who can control the machine

| Where the page is opened | What it can do |
|---|---|
| The lab PC (http://localhost:8000) | Everything |
| Any phone or laptop on the same Wi-Fi | Watch everything, and press the emergency stop |

Stopping is allowed from anywhere because it can only make the machine safer.
`python server.py --allow-remote-control` gives every viewer full control (not
recommended). `--local-only` hides the website from other devices.

If Windows asks whether Python may use the network, choose **Allow** for private
networks, otherwise phones can't open the page.

### Watching from outside the lab

Phones need the same Wi-Fi as the lab PC. To share a link that works from anywhere,
run a tunnel next to the server, for example Cloudflare's free quick tunnel:

    cloudflared tunnel --url http://localhost:8000

Anyone with that link can watch and press the emergency stop, but can't start the
motor. Close the tunnel when you're done.

## RPM

The motor has no speed sensor, so RPM is **estimated** from the drive:

    RPM ≈ rated RPM × (PWM ÷ 255) × (10 V ÷ 12 V)

The L298N drops about 2 V, so the motor gets about 10 V of the 12 V at full duty.
Click "rated 300 RPM" in the Shaft speed tile and type the RPM printed on your
motor, or start the server with `--rated-rpm 200`. For a measured value, add an IR
or Hall-effect sensor that counts shaft turns.

## Without the server

Open `index.html` directly in Chrome or Edge on the PC with the Arduino:

- **Connect Arduino over USB** connects the page straight to the board (Web Serial).
- **Run demo machine** shows pretend data in the same format.

Only that one browser sees the data this way. Use `server.py` for phones and the
session log.

## Problems

| You see | Do this |
|---|---|
| `COM11 is busy` | Close the Serial Monitor / Plotter in the Arduino IDE, or another open copy of the server |
| `Looking for the Arduino` | Check the USB cable, then start with `--port COM11` (the port shown in the Arduino IDE) |
| `No readings` on the page | The Arduino lost power or USB. The server reconnects by itself |
| `Port 8000 is already in use` | The server is already running, or start with `--http-port 8080` |
| Page opens on the PC but not on a phone | Same Wi-Fi? Allow Python through the Windows firewall |
| No logo | Put `au_logo.png` next to `index.html` and refresh |
