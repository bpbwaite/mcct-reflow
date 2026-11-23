"""Plateau calibration run (laptop side), for the -Cal firmware.

Usage: python cal_plateaus.py <port> <out.csv> [--mv 700 800 ...] [--max-probe 240]

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
import queue
import statistics
import sys
import threading
import time

RUNNING = 1
STATES = ["idle", "holding", "stopped", "time limit", "mV ceiling", "sensor does not follow", "host lost"]
STEADY_S, STEADY_BAND_MV = 120, 2.0
STEP_MV, MIN_MV = 100, 700


class Link:
    """Serial I/O on a background thread: parses telemetry, sends keep-alives."""

    def __init__(self, ser, log):
        self.ser, self.log = ser, log
        self.window = collections.deque(maxlen=STEADY_S * 4)    # 250 ms per line
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
        try:
            link.send("x")
        except Exception:
            break
        if link.fresh() and link.last[3] != RUNNING:
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
            return
        link.send("+" if now < target else "-")
        time.sleep(0.4)
    raise RuntimeError(f"target stuck at {link.last[1]} mV")


def check_running(link):
    if not link.fresh():
        raise RuntimeError("no telemetry")
    if link.last[3] != RUNNING:
        raise RuntimeError(f"chip stopped the hold: {STATES[link.last[3]]}")


def run(link, targets, max_probe, out, ask):
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
            check_running(link)
            if time.time() - shown > 15:
                shown = time.time()
                print(f"   {link.last[0]:7.1f} mV  duty {link.last[2]:4.1f} %")
            time.sleep(0.2)
        answers = ask("Sensor steady. Probe steady too? Type its degC (s skip, q quit): ")
        while True:
            check_running(link)
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
    ap.add_argument("--mv", nargs="+", type=int, default=list(range(700, 1801, 100)))
    ap.add_argument("--max-probe", type=float, default=240.0)
    args = ap.parse_args()
    if any(m % STEP_MV or m < MIN_MV for m in args.mv):
        sys.exit(f"targets must be multiples of {STEP_MV} from {MIN_MV}")
    import serial
    with open(args.out, "x") as out, open(args.out + ".log", "x") as log:      # never overwrite data
        out.write("mv,probe_c,mv_std,duty_pct\n")
        link = Link(serial.Serial(args.port, 115200, timeout=0.1), log)
        try:
            time.sleep(4)                   # Uno bootloader + chip boot
            run(link, args.mv, args.max_probe, out, ask_stdin)
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
