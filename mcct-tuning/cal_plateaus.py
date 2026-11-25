"""Plateau calibration run (PC side), for the -Cal firmware.

Usage: uv run cal_plateaus.py <port> <out.csv> [--mv 700 800 ...] [--max-probe <degC>]

Steps the plate through sensor-voltage holds. At each one it waits until the
sensor is steady, then asks for the probe reading; wait until the probe is
steady too. At the prompt:
    a number      record it (degC) and go on
    s             skip this plateau
    q             stop the run
It stops by itself when a probe reading reaches --max-probe. A keep-alive
goes out every second; if this program dies, the firmware turns the heater
off after CFG_CAL_HOST_S. On every exit it sends "stop" and reports whether
the chip confirmed it. The power switch is still the real stop.

Results: <out.csv> (mv, probe_c, mv_std, duty_pct), all telemetry: <out>.log
"""
import argparse
import collections
import pathlib
import queue
import re
import statistics
import sys
import threading
import time

RUNNING = 1
STATES = ["idle", "holding", "stopped", "time limit", "mV ceiling", "sensor does not follow", "host lost"]
STEADY_S, STEADY_BAND_MV = 120, 2.0
USER_CONFIG = pathlib.Path(__file__).resolve().parent.parent / "mcct-firmware" / "config" / "user_config.h"


def config(name):
    """An integer option from the firmware's user_config.h."""
    m = re.search(rf"^#define\s+{name}\s+(\d+)", USER_CONFIG.read_text(encoding="utf-8"), re.M)
    if not m:
        sys.exit(f"{name} not found in {USER_CONFIG}")
    return int(m.group(1))


STEP_MV, MIN_MV, MAX_MV = config("CFG_CAL_STEP_MV"), config("CFG_CAL_MIN_MV"), config("CFG_CAL_MAX_MV")
MAX_PROBE_C = config("CFG_TARGET_MAX_C") - 10       # end the run a little below the plate's limit


class Link:
    """Serial I/O on a background thread: parses telemetry, sends keep-alives."""

    def __init__(self, ser, log):
        self.ser, self.log = ser, log
        self.window = collections.deque(maxlen=STEADY_S * 4)    # 250 ms per line
        self.prev = None                    # (set_mv, state) of the line before
        self.last = None                    # (mv, set_mv, duty_pct, state)
        self.last_time = 0.0
        self.alive, self.keepalive = True, False
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.thread.start()

    def _pump(self):
        buf, next_k = b"", 0.0
        while self.alive:
            try:
                buf += self.ser.read(256)
                if self.keepalive and time.time() >= next_k:
                    self.ser.write(b"k")
                    next_k = time.time() + 1.0
            except Exception as e:              # unplugged: the firmware times out
                print(f"\n[link lost: {e}]")
                self.alive = False
            *lines, buf = buf.split(b"\n")
            for line in lines:
                self._line(line.decode(errors="replace").strip())

    def _line(self, text):
        self.log.write(text + "\n")
        self.log.flush()
        if text.startswith("#"):
            print("  chip:", text[1:].strip())
        parts = text.split(",")
        try:
            t, q16, mv10, set_mv, duty, state = (int(p) for p in parts)
        except ValueError:
            return
        # Reject garbled lines: the two readings must agree, the rest be legal.
        if abs(q16 * 50000 / 65536 - mv10) > 3 or not 0 <= state < len(STATES) or not 0 <= duty <= 10000:
            return
        if set_mv % STEP_MV or not MIN_MV <= set_mv <= MAX_MV or (state != RUNNING and duty):
            return
        # A single flipped character can still look legal: believe the target
        # and state only when two lines in a row agree.
        prev, self.prev = self.prev, (set_mv, state)
        if prev != self.prev:
            return
        self.last, self.last_time = (mv10 / 10, set_mv, duty / 100, state), time.time()
        self.window.append((mv10 / 10, duty / 100))

    def send(self, key):
        self.ser.write(key.encode())
        time.sleep(0.4)

    def fresh(self):
        return self.alive and time.time() - self.last_time < 3 and self.last is not None

    def steady(self):
        v = [m for m, _ in self.window]
        return len(v) == self.window.maxlen and max(v) - min(v) <= 2 * STEADY_BAND_MV


def stop(link):
    link.keepalive = False
    for _ in range(5):
        sent = time.time()
        try:
            link.send("x")
            time.sleep(0.8)                 # two agreeing lines
        except Exception:
            break
        if link.fresh() and link.last_time > sent and link.last[3] != RUNNING:
            print(f"Chip confirms heater off ({STATES[link.last[3]]}).")
            return True
    print("*** HEATER OFF NOT CONFIRMED: use the power switch ***")
    return False


def set_target(link, target):
    """Step the target, checking the chip's own value after each key."""
    for _ in range(60):
        if not link.fresh():
            raise RuntimeError("no telemetry")
        now = link.last[1]
        if now == target:
            time.sleep(1.2)                 # a key may still be on its way
            if link.fresh() and link.last[1] == target:
                return
            continue
        link.send("+" if now < target else "-")
        time.sleep(1.2)                     # two agreeing lines take up to 0.75 s
    raise RuntimeError(f"target stuck at {link.last[1]} mV")


def check_running(link, target, max_mv):
    if not link.fresh():
        raise RuntimeError("no telemetry")
    if link.last[3] != RUNNING:
        raise RuntimeError(f"chip stopped the hold: {STATES[link.last[3]]}")
    if link.last[1] != target:
        raise RuntimeError(f"target is {link.last[1]} mV, expected {target}")
    if link.last[0] >= max_mv:
        raise RuntimeError(f"reading {link.last[0]:.1f} mV reached --max-mv")


def run(link, targets, max_probe, out, ask, max_mv=MAX_MV + STEP_MV):
    """ask(prompt) starts a prompt and returns a queue its answer arrives on."""
    started = False
    for target in targets:
        set_target(link, target)
        if not started:
            link.keepalive = True
            link.send("k")
            link.send("g")
            started = True
            time.sleep(1)
        link.window.clear()
        print(f"\n== {target} mV: waiting for the sensor to settle")
        shown = 0
        while not link.steady():
            check_running(link, target, max_mv)
            if time.time() - shown > 15:
                shown = time.time()
                print(f"   {link.last[0]:7.1f} mV  duty {link.last[2]:4.1f} %")
            time.sleep(0.2)
        answers = ask("Sensor steady. Probe steady too? Type its degC (s skip, q quit): ")
        while True:
            check_running(link, target, max_mv)
            try:
                text = answers.get(timeout=0.2)
                break
            except queue.Empty:
                continue
        if text == "q":
            return
        if text == "s":
            continue
        probe = float(text)
        v = [m for m, _ in link.window]
        d = [u for _, u in link.window]
        out.write(f"{statistics.mean(v):.2f},{probe:.1f},{statistics.pstdev(v):.2f},{statistics.mean(d):.2f}\n")
        out.flush()
        if probe >= max_probe:
            print(f"Probe reached {max_probe} degC: ending the run.")
            return


def ask_stdin(prompt):
    q = queue.Queue()

    def reader():
        while True:
            try:
                text = input(prompt).strip().lower()
            except EOFError:
                text = "q"
            try:
                ok = text in ("s", "q") or 0 < float(text) < 400
            except ValueError:
                ok = False
            if ok:
                q.put(text)
                return

    threading.Thread(target=reader, daemon=True).start()
    return q


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--mv", nargs="+", type=int, default=list(range(MIN_MV, MAX_MV + 1, STEP_MV)))
    ap.add_argument("--max-probe", type=float, default=MAX_PROBE_C)
    ap.add_argument("--max-mv", type=float, default=MAX_MV + STEP_MV, help="stop the run at this reading")
    args = ap.parse_args()
    if any(m % STEP_MV or not MIN_MV <= m <= MAX_MV for m in args.mv):
        sys.exit(f"targets must be multiples of {STEP_MV} from {MIN_MV} to {MAX_MV}")
    import serial
    pathlib.Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "x") as out, open(args.out + ".log", "x") as log:      # never overwrite data
        out.write("mv,probe_c,mv_std,duty_pct\n")
        link = Link(serial.Serial(args.port, 115200, timeout=0.1), log)
        try:
            time.sleep(4)                   # Uno bootloader + chip boot
            run(link, args.mv, args.max_probe, out, ask_stdin, args.max_mv)
        except KeyboardInterrupt:
            print("\ninterrupted")
        except Exception as e:
            print(f"\nrun ended: {e}")
        finally:
            stop(link)
            link.alive = False
            link.thread.join(2)


if __name__ == "__main__":
    main()
