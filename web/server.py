#!/usr/bin/env python3
"""
Industrial Machine Safety Monitor - local web server
ENR206 project, Ahmedabad University

What it does
  * talks to the Arduino over USB (9600 baud) - no Arduino IDE needed
  * serves the monitoring website at http://localhost:8000
  * sends every reading to all open browsers in real time
  * forwards Start / speed / E-STOP commands from the website to the Arduino
  * writes every reading of the session to data/session_<date>_<time>.csv

Run it
  pip install pyserial
  python server.py                    finds the Arduino by itself
  python server.py --port COM11       or name the port
  python server.py --demo             no Arduino: a pretend machine (for practice / screenshots)

Who may control the machine
  * the PC running this server (http://localhost:8000): everything
  * phones / laptops on the same Wi-Fi (http://<this PC's IP>:8000): watch the
    machine and press EMERGENCY STOP, but not start it or change the speed
  * --allow-remote-control gives every viewer full control (not recommended)

Close the Arduino IDE's Serial Monitor and Serial Plotter first: only one
program can use the USB port at a time.
"""

import argparse
import heapq
import json
import math
import os
import queue
import random
import re
import socket
import sys
import threading
import time
import webbrowser
from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

try:
    import serial                      # pip install pyserial
    import serial.tools.list_ports
except ImportError:                    # still works with --demo
    serial = None

VERSION = "1.0"
HERE = os.path.dirname(os.path.abspath(__file__))
BAUD = 9600
REPLAY_ROWS = 700                      # ~5.8 min of readings (one every 0.5 s) for new viewers
MAX_EVENTS = 5000
DEFAULT_COLS = ("ms,state,reason,dist_cm,temp_c,temp_rate_cpm,vib_mg,tilt_deg,current_ma,bus_v,"
                "power_w,pwm,vib_warn_mg,vib_stop_mg,cur_warn_ma,cur_stop_ma,humidity_pct,"
                "speed_pct,speed_now_pct").split(",")

# Commands the website may send. Output-mode commands (n, v, t, b) are kept for the
# server itself: they would switch the Arduino away from the data the website reads.
CMD_OK = re.compile(r"^(?:[gxzlsd+\-]|\d{1,3})$")
STOP_ONLY = {"x"}
CMD_TEXT = {"g": "Start / reset", "x": "Emergency stop", "z": "Re-zero tilt", "l": "Re-learn vibration",
            "s": "Status report", "d": "Data mode", "+": "Speed +10 %", "-": "Speed -10 %"}

USB_IDS = {0x2341, 0x2A03, 0x1A86, 0x0403, 0x10C4}   # Arduino, Arduino.org, CH340, FTDI, CP210x


def say(msg):
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def now_ms():
    return int(time.time() * 1000)


# =============================================================================== hub
class Hub:
    """Keeps the session's data and passes every line to every open browser."""

    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.clients = set()
        self.rows = deque()             # (t, line, run_dt, warn_dt) of the last REPLAY_ROWS readings
        self.events = deque(maxlen=MAX_EVENTS)   # (t, line) of every event this session
        self.header = None
        self.limits = None
        self.cols = list(DEFAULT_COLS)
        self.prev_row_t = None
        self.evicted = {"rows": 0, "run_s": 0.0, "warn_s": 0.0}
        self.peaks = {"temp_c": None, "vib_mg": None, "dist_cm": None}
        self.status = {"connected": False, "port": None, "message": "Starting"}
        self.started = now_ms()
        self.last_row_mono = 0.0
        self.writer = None              # function(cmd) that sends to the Arduino
        self.csv = None
        self.csv_path = None
        self.ev_file = None
        self._open_logs()

    # ------------------------------------------------------------- session files
    def _open_logs(self):
        if self.args.no_log:
            return
        folder = os.path.join(HERE, "data")
        os.makedirs(folder, exist_ok=True)
        stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self.csv_path = os.path.join(folder, "session_%s.csv" % stamp)
        self.csv = open(self.csv_path, "w", encoding="utf-8", newline="")
        self.csv.write("time,epoch_ms," + ",".join(self.cols) + "\n")
        self.ev_file = open(os.path.join(folder, "session_%s_events.txt" % stamp), "w", encoding="utf-8")

    def _log_row(self, t, values):
        if self.csv:
            stamp = datetime.fromtimestamp(t / 1000).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
            self.csv.write("%s,%d,%s\n" % (stamp, t, ",".join(values)))
            self.csv.flush()

    def _log_event(self, t, text):
        if self.ev_file:
            self.ev_file.write(datetime.fromtimestamp(t / 1000).strftime("%Y-%m-%d %H:%M:%S  ") + text + "\n")
            self.ev_file.flush()

    # ------------------------------------------------------------- broadcasting
    def _broadcast(self, msg):
        dead = []
        for q in self.clients:
            try:
                q.put_nowait(msg)
            except queue.Full:          # a browser that stopped reading
                dead.append(q)
        for q in dead:
            self.clients.discard(q)

    def publish(self, line):
        """One line from the Arduino (or the demo machine)."""
        line = line.strip()
        if not line:
            return
        t = now_ms()
        msg = "data: " + json.dumps({"t": t, "l": line}, separators=(",", ":")) + "\n\n"
        with self.lock:
            kind = line[:2]
            if kind == "D,":
                self._store_row(t, line)
            elif kind == "E," or line.startswith((">> SELFTEST", ">> WATCHDOG_RESET")):
                # (the board starts in readable-text mode, so its power-up self-test
                #  arrives as ">> SELFTEST,..." before the server switches it to data)
                self.events.append((t, line))
                self._log_event(t, line)
            elif kind == "H,":
                self.header = (t, line)
                cols = line.split(",")[1:]
                if len(cols) > 5:
                    self.cols = cols
            elif kind == "L,":
                self.limits = (t, line)
            self._broadcast(msg)

    def _store_row(self, t, line):
        values = line.split(",")[1:]
        if len(values) < 10:
            return
        self.last_row_mono = time.monotonic()
        row = dict(zip(self.cols, values))
        state = row.get("state", "")
        dt = 0.0
        if self.prev_row_t is not None:
            dt = min(t - self.prev_row_t, 2000) / 1000.0
        self.prev_row_t = t
        run_dt = dt if state in ("RUN", "WARNING") else 0.0
        warn_dt = dt if state == "WARNING" else 0.0
        self.rows.append((t, line, run_dt, warn_dt))
        while len(self.rows) > REPLAY_ROWS:
            _, _, r, w = self.rows.popleft()
            self.evicted["rows"] += 1
            self.evicted["run_s"] += r
            self.evicted["warn_s"] += w
        self._peak("temp_c", row.get("temp_c"), max)
        self._peak("vib_mg", row.get("vib_mg"), max)
        try:
            if float(row.get("dist_cm", "nan")) < 149:
                self._peak("dist_cm", row.get("dist_cm"), min)
        except ValueError:
            pass
        self._log_row(t, values)

    def _peak(self, key, text, pick):
        try:
            v = float(text)
        except (TypeError, ValueError):
            return
        if math.isnan(v):
            return
        cur = self.peaks[key]
        self.peaks[key] = v if cur is None else pick(cur, v)

    def set_status(self, connected, port, message):
        with self.lock:
            changed = (connected, port, message) != (self.status["connected"], self.status["port"], self.status["message"])
            self.status = {"connected": connected, "port": port, "message": message}
            if changed:
                self._broadcast("event: status\ndata: " + json.dumps(self.status) + "\n\n")
        if changed:
            say(message)

    def note_command(self, cmd, who):
        info = {"t": now_ms(), "cmd": cmd, "who": who}
        with self.lock:
            self._broadcast("event: cmd\ndata: " + json.dumps(info) + "\n\n")
        self._log_event(info["t"], "COMMAND %s from %s" % (cmd, who))

    # ------------------------------------------------------------- viewers
    def subscribe(self, can_control):
        q = queue.Queue(maxsize=4000)
        with self.lock:
            hello = {
                "version": VERSION, "mode": "demo" if self.args.demo else "serial",
                "can_control": can_control, "rated_rpm": self.args.rated_rpm,
                "started": self.started, "status": self.status, "evicted": self.evicted,
                "peaks": self.peaks, "viewers": len(self.clients) + 1,
            }
            parts = ["retry: 2000\n\n", "event: hello\ndata: " + json.dumps(hello) + "\n\n"]
            fixed = sorted(x for x in (self.header, self.limits) if x)
            replay = heapq.merge(fixed, [(t, l) for t, l, _, _ in self.rows], self.events, key=lambda x: x[0])
            for t, line in replay:
                parts.append("data: " + json.dumps({"t": t, "l": line, "r": 1}, separators=(",", ":")) + "\n\n")
            parts.append("event: ready\ndata: {}\n\n")
            self.clients.add(q)
            self._broadcast("event: viewers\ndata: %d\n\n" % len(self.clients))
        return q, "".join(parts)

    def unsubscribe(self, q):
        with self.lock:
            self.clients.discard(q)
            self._broadcast("event: viewers\ndata: %d\n\n" % len(self.clients))

    def send(self, cmd):
        w = self.writer
        if not w:
            return False
        try:
            w(cmd)
            return True
        except Exception as e:          # port vanished mid-write
            say("Could not send %r: %s" % (cmd, e))
            return False


# ======================================================================== the Arduino
def find_arduino():
    ports = list(serial.tools.list_ports.comports())
    known = [p for p in ports if p.vid in USB_IDS or re.search(r"arduino|ch340|usb.serial|usb-serial", (p.description or ""), re.I)]
    if known:
        return known[0].device
    usable = [p for p in ports if not re.search(r"bluetooth", (p.description or "") + (p.hwid or ""), re.I)
              and not p.device.startswith("/dev/ttyS")]
    return usable[0].device if len(usable) == 1 else None


def serial_worker(hub, args, stop):
    while not stop.is_set():
        port = args.port or find_arduino()
        if not port:
            hub.set_status(False, None, "Looking for the Arduino - plug in its USB cable (or start with --port COM11)")
            stop.wait(2)
            continue
        try:
            ser = serial.Serial(port, BAUD, timeout=0.2, write_timeout=1)
        except (serial.SerialException, OSError) as e:
            text = str(e)
            if re.search(r"denied|busy|in use|PermissionError|Errno 16", text, re.I):
                hub.set_status(False, port, "%s is busy - close the Serial Monitor / Plotter in the Arduino IDE" % port)
            else:
                hub.set_status(False, port, "Cannot open %s yet (%s) - retrying" % (port, text.split(":")[0]))
            stop.wait(2)
            continue

        wlock = threading.Lock()

        def write(cmd, ser=ser):
            with wlock:
                ser.write((cmd + "\n").encode("ascii"))

        hub.writer = write
        opened = time.monotonic()
        last_ask = 0.0
        hub.last_row_mono = 0.0
        hub.set_status(True, port, "Connected to the Arduino on %s at %d baud" % (port, BAUD))
        buf = b""
        try:
            while not stop.is_set():
                chunk = ser.read(256)
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        raw, buf = buf.split(b"\n", 1)
                        hub.publish(raw.decode("utf-8", "replace"))
                    if len(buf) > 4000:
                        buf = b""
                # The Arduino starts in readable-text mode (for the Serial Monitor) and
                # restarts when the port opens. Ask for data rows until they flow.
                t = time.monotonic()
                if t - opened > 2.5 and t - hub.last_row_mono > 4 and t - last_ask > 2.5:
                    write("d")
                    last_ask = t
        except (serial.SerialException, OSError) as e:
            hub.set_status(False, port, "Lost the Arduino on %s (%s) - reconnecting" % (port, str(e).split(":")[0]))
        finally:
            hub.writer = None
            try:
                if ser.is_open:
                    try:
                        if stop.is_set():
                            ser.write(b"n\n")      # leave it readable for the Serial Monitor
                            ser.flush()
                    except Exception:
                        pass
                    ser.close()
            except Exception:
                pass
        stop.wait(1)


# ===================================================================== demo machine
class DemoMachine:
    """A pretend machine that prints exactly what the real firmware prints in data mode."""

    def __init__(self, hub):
        self.hub = hub
        self.lock = threading.Lock()
        self.ms = 1200
        self.state, self.reason = "IDLE", "OK"
        self.speed, self.pwm = 60, 0
        self.temp, self.tilt, self.dist = 27.4, 0.4, 150.0
        self.learned = {}
        self.learn_t = 0
        self.clear_t = 0
        self.temps = deque(maxlen=60)
        self.started_at = time.monotonic()
        self.estopped_at = None

    def out(self, line):
        self.hub.publish(line)

    def set_state(self, s, r):
        self.state, self.reason = s, r
        self.out("E,%d,STATE,%s,%s" % (self.ms, s, r))

    def boot(self):
        self.out("E,%d,SELFTEST,MPU6050=OK DHT22=OK INA219=not fitted 12V_SENSE=not fitted LCD=OK" % self.ms)
        self.out("# Smart Machine Safety Monitor v2.1 (demo)")
        self.out("H," + ",".join(DEFAULT_COLS))
        self.out("L,dist_warn_cm=40,dist_stop_cm=15,temp_warn_c=40,temp_stop_c=50,rate_warn_cpm=6,"
                 "rate_stop_cpm=15,tilt_warn_deg=10,tilt_stop_deg=25")
        self.set_state("IDLE", "OK")

    @staticmethod
    def pct_to_pwm(p):
        return 70 + round(185 * p / 100)

    def now_pct(self):
        return round((self.pwm - 70) * 100 / 185) if self.pwm > 70 else 0

    def command(self, c):
        # like the real board, answer a moment after the command arrives
        threading.Timer(0.08, self._command, (c,)).start()

    def _command(self, c):
        with self.lock:
            c = c.lower()
            if c.isdigit():
                p = max(10, min(100, int(round(int(c) / 10.0)) * 10))
                self.speed = p
                self.out("E,%d,SPEED,%d" % (self.ms, p))
            elif c == "g":
                if self.state == "IDLE":
                    self.set_state("RUN", "OK")
                elif self.state in ("ESTOP", "FAULT"):
                    self.set_state("IDLE", "OK")
            elif c == "x":
                if self.state in ("RUN", "WARNING", "IDLE"):
                    self.set_state("ESTOP", "REMOTE")
                    self.estopped_at = time.monotonic()
            elif c in "+-":
                p = self.speed + (10 if c == "+" else -10)
                self.speed = max(10, min(100, p))
                self.out("E,%d,SPEED,%d" % (self.ms, self.speed))
            elif c == "l":
                self.learned.clear()
                self.out("E,%d,RELEARN" % self.ms)
            elif c == "z":
                self.tilt = 0.0
                self.out("E,%d,TILT_REZERO" % self.ms)
            elif c == "s":
                self.out("# State      : %s - %s" % (self.state, self.reason))
                self.out("# Speed      : set %d %%, now %d %% (PWM %d)" % (self.speed, self.now_pct(), self.pwm))

    def tick(self):
        with self.lock:
            self.ms += 500
            running = self.state in ("RUN", "WARNING")
            target = self.pct_to_pwm(min(self.speed, 45) if self.state == "WARNING" else self.speed) if running else 0
            step = min(abs(target - self.pwm), 120)
            self.pwm += step if target > self.pwm else -step
            # someone walks up to the machine every 50 s and stands there for 4 s
            cycle = (self.ms / 1000.0) % 50
            self.dist = 26 + random.random() * 6 if 36 < cycle < 40 else 150.0
            if running:
                if self.dist < 40 and self.state == "RUN":
                    self.set_state("WARNING", "PROX")
                    self.clear_t = 0
                if self.dist >= 45 and self.state == "WARNING":
                    self.clear_t += 500
                    if self.clear_t >= 3000:
                        self.set_state("RUN", "OK")
            pct = self.now_pct()
            base = 9 + 28 * pct / 100
            vib = (base if self.pwm > 0 else 3.5) + (random.random() - 0.5) * 4 + (6 * math.sin(self.ms / 1700) if self.pwm else 0) * 0.3
            settle = 27 + 9 * pct / 100 if running else 27.0     # warms toward this, ~5 min time constant
            self.temp += (settle - self.temp) * 0.0017 + (random.random() - 0.5) * 0.03
            self.temps.append(self.temp)
            rate = (self.temps[-1] - self.temps[0]) / (len(self.temps) * 0.5 / 60) if len(self.temps) > 20 else 0.0
            warn = stop = 800.0
            key = self.speed if self.state == "RUN" else None
            if running and self.learned.get(self.speed):
                b = self.learned[self.speed]
                warn = max(40.0, b * 2)
                stop = max(warn * 1.5, b * 3.5)
            if key and key not in self.learned and self.pwm == target:
                self.learn_t += 500
                if self.learn_t >= 6000:
                    self.learned[key] = base
                    self.learn_t = 0
                    w = max(40.0, base * 2)
                    self.out("E,%d,BASELINE,speed_pct=%d vib_base_mg=%.1f vib_warn=%.0f vib_stop=%.0f"
                             % (self.ms, key, base, w, max(w * 1.5, base * 3.5)))
            else:
                if not key:
                    self.learn_t = 0
            reason = "OK" if self.state in ("IDLE", "RUN") else self.reason
            self.out("D,%d,%s,%s,%.1f,%.2f,%.2f,%.1f,%.1f,0,0.00,0.00,%d,%.0f,%.0f,1500,1500,%.1f,%d,%d"
                     % (self.ms, self.state, reason, self.dist, self.temp, rate, vib, self.tilt, self.pwm,
                        warn, stop, 51.0 + math.sin(self.ms / 60000) * 2, self.speed, pct))
            # the demo restarts itself after an emergency stop so it never stays dead
            if self.state == "ESTOP" and self.estopped_at and time.monotonic() - self.estopped_at > 20:
                self.estopped_at = None
                self.set_state("IDLE", "OK")

    def run(self, stop):
        self.hub.set_status(True, "demo", "Demo machine running (no Arduino needed)")
        self.hub.writer = self.command
        self.boot()
        script = {6: "g", 70: "100", 150: "60"}
        n = 0
        while not stop.wait(0.5):
            n += 1
            if n // 2 in script and n % 2 == 0:
                self.command(script.pop(n // 2))
            self.tick()
            if self.state == "IDLE" and n % 60 == 0 and n > 20:
                self.command("g")


# ========================================================================= web server
def local_ips():
    ips = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))          # no packet is sent
        ips.append(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
            if not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except OSError:
        pass
    return ips


LOGO_FILES = ("au_logo.png", "au_logo.svg", "au_logo.jpg", "au_logo.jpeg", "au_logo.webp")
TYPES = {".png": "image/png", ".svg": "image/svg+xml", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
         ".webp": "image/webp", ".html": "text/html; charset=utf-8", ".ico": "image/x-icon"}


def make_handler(hub, args):
    class Handler(BaseHTTPRequestHandler):
        server_version = "MachineSafetyMonitor/" + VERSION
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *a):            # keep the console quiet
            if args.verbose:
                say("%s %s" % (self.client_address[0], fmt % a))

        def is_lab_pc(self):
            ip = self.client_address[0]
            forwarded = any(h in self.headers for h in ("X-Forwarded-For", "Forwarded", "CF-Connecting-IP", "X-Real-IP"))
            return ip in ("127.0.0.1", "::1", "::ffff:127.0.0.1") and not forwarded

        def can_control(self):
            return args.allow_remote_control or self.is_lab_pc()

        def reply(self, code, body, ctype="application/json", extra=None):
            data = body if isinstance(body, bytes) else body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(data)

        def send_file(self, name):
            path = os.path.join(HERE, name)
            if not os.path.isfile(path):
                return self.reply(404, "not found", "text/plain")
            with open(path, "rb") as f:
                body = f.read()
            self.reply(200, body, TYPES.get(os.path.splitext(name)[1].lower(), "application/octet-stream"))

        def do_HEAD(self):
            if urlparse(self.path).path == "/api/stream":
                return self.reply(405, "use GET", "text/plain")
            self.do_GET()

        def do_GET(self):
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                return self.send_file("index.html")
            if path in ("/logo",) + tuple("/" + n for n in LOGO_FILES):
                for name in LOGO_FILES:
                    if os.path.isfile(os.path.join(HERE, name)):
                        return self.send_file(name)
                return self.reply(404, "no logo file", "text/plain")
            if path == "/api/info":
                return self.reply(200, json.dumps({
                    "version": VERSION, "mode": "demo" if args.demo else "serial", "can_control": self.can_control(),
                    "status": hub.status, "rated_rpm": args.rated_rpm, "viewers": len(hub.clients),
                    "lan": ["http://%s:%d" % (ip, args.http_port) for ip in local_ips()]}))
            if path == "/api/stream":
                return self.stream()
            if path == "/api/csv":
                if not hub.csv_path or not os.path.isfile(hub.csv_path):
                    return self.reply(404, "no session file (started with --no-log)", "text/plain")
                with open(hub.csv_path, "rb") as f:
                    body = f.read()
                return self.reply(200, body, "text/csv; charset=utf-8",
                                  {"Content-Disposition": 'attachment; filename="%s"' % os.path.basename(hub.csv_path)})
            if path == "/favicon.ico":
                return self.reply(204, b"", "image/x-icon")
            return self.reply(404, "not found", "text/plain")

        def do_POST(self):
            if urlparse(self.path).path != "/api/cmd":
                return self.reply(404, json.dumps({"ok": False, "error": "not found"}))
            try:
                n = min(int(self.headers.get("Content-Length", "0")), 1000)
                cmd = str(json.loads(self.rfile.read(n) or b"{}").get("cmd", "")).strip().lower()
            except (ValueError, AttributeError):
                return self.reply(400, json.dumps({"ok": False, "error": "bad request"}))
            if not CMD_OK.match(cmd):
                return self.reply(400, json.dumps({"ok": False, "error": "unknown command"}))
            if not self.can_control() and cmd not in STOP_ONLY:
                return self.reply(403, json.dumps({"ok": False, "error": "view only - control the machine from the lab PC"}))
            who = "lab PC" if self.is_lab_pc() else "viewer " + self.client_address[0]
            if not hub.send(cmd):
                return self.reply(503, json.dumps({"ok": False, "error": "the Arduino is not connected"}))
            hub.note_command(cmd, who)
            if args.verbose or cmd == "x":
                say("Command %r from %s" % (cmd, who))
            return self.reply(200, json.dumps({"ok": True}))

        def stream(self):
            q, first = hub.subscribe(self.can_control())
            try:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Accel-Buffering", "no")
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True
                self.wfile.write(first.encode("utf-8"))
                self.wfile.flush()
                while True:
                    try:
                        msg = q.get(timeout=15)
                        batch = [msg]
                        while len(batch) < 200:
                            try:
                                batch.append(q.get_nowait())
                            except queue.Empty:
                                break
                        out = "".join(batch)
                    except queue.Empty:
                        out = ": keep-alive\n\n"
                    self.wfile.write(out.encode("utf-8"))
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
                pass
            finally:
                hub.unsubscribe(q)

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Industrial Machine Safety Monitor - local web server")
    ap.add_argument("--port", help="Arduino serial port, e.g. COM11 or /dev/ttyACM0 (default: find it)")
    ap.add_argument("--http-port", type=int, default=8000, help="website port (default 8000)")
    ap.add_argument("--demo", action="store_true", help="no Arduino: run a pretend machine")
    ap.add_argument("--rated-rpm", type=int, default=300, help="RPM printed on the motor (for the RPM estimate)")
    ap.add_argument("--allow-remote-control", action="store_true", help="let every viewer start the machine and change speed")
    ap.add_argument("--local-only", action="store_true", help="only this PC can open the website")
    ap.add_argument("--no-browser", action="store_true", help="do not open the browser")
    ap.add_argument("--no-log", action="store_true", help="do not write data/session_*.csv")
    ap.add_argument("--verbose", action="store_true", help="print every web request")
    args = ap.parse_args()

    if not args.demo and serial is None:
        print("pyserial is missing. Install it with:\n    python -m pip install pyserial\n"
              "or try the website without the Arduino:\n    python server.py --demo")
        sys.exit(1)
    if not os.path.isfile(os.path.join(HERE, "index.html")):
        print("index.html must be in the same folder as server.py (%s)" % HERE)
        sys.exit(1)

    hub = Hub(args)
    stop = threading.Event()
    if args.demo:
        worker = threading.Thread(target=DemoMachine(hub).run, args=(stop,), daemon=True)
    else:
        worker = threading.Thread(target=serial_worker, args=(hub, args, stop), daemon=True)

    host = "127.0.0.1" if args.local_only else "0.0.0.0"
    try:
        httpd = ThreadingHTTPServer((host, args.http_port), make_handler(hub, args))
    except OSError as e:
        print("Port %d is already in use (%s). Is the server already running? Try --http-port 8080" % (args.http_port, e))
        sys.exit(1)
    httpd.daemon_threads = True

    url = "http://localhost:%d" % args.http_port
    print()
    print("  Industrial Machine Safety Monitor - web server %s" % VERSION)
    print("  ------------------------------------------------------------")
    print("  On this PC (full control):   %s" % url)
    if not args.local_only:
        for ip in local_ips():
            print("  Phones on the same Wi-Fi:    http://%s:%d   (watch + emergency stop)" % (ip, args.http_port))
    print("  Arduino:                     %s" % ("demo machine" if args.demo else (args.port or "auto-detect") + " at 9600 baud"))
    if hub.csv_path:
        print("  Session data saved to:       %s" % os.path.relpath(hub.csv_path, os.getcwd()))
    print("  Stop the server with Ctrl+C")
    print()

    worker.start()
    if not args.no_browser:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        say("Stopping...")
        stop.set()
        worker.join(timeout=3)
        httpd.server_close()
        for f in (hub.csv, hub.ev_file):
            if f:
                f.close()


if __name__ == "__main__":
    main()
