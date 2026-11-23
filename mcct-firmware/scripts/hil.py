"""Drive the firmware's debug console through the Arduino bridge (HIL testing).

Usage: python hil.py <port> <script> [--log file]

The script is a space-separated list of steps:
    wait:<seconds>    keep reading for this long
    key:<chars>       send console keys (u d e E t s S h, see src/debug/cli.h)
    until:<text>      read until this text appears (30 s limit)

Example (bench build, simulated plate):
    python hil.py COM8 "wait:3 key:t key:e wait:120 key:e key:t"

Opening the port resets the Uno, which resets the target, so the capture
starts with the boot banner. Everything received is printed (and logged).
"""
import sys
import time

import serial

BAUD = 115200       # the bridge's USB side; it converts to CFG_CONSOLE_BAUD


def main():
    args = sys.argv[1:]
    log = None
    if "--log" in args:
        i = args.index("--log")
        log = open(args[i + 1], "w", newline="")
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    port, script = args
    s = serial.Serial(port, BAUD, timeout=0.1)
    buf = ""

    def pump(seconds, until=None):
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            data = s.read(512).decode(errors="replace")
            if data:
                sys.stdout.write(data)
                sys.stdout.flush()
                if log:
                    log.write(data)
                    log.flush()             # readable while the run is going
                buf += data
                if until and until in buf:
                    return True
        return False

    pump(2.0)                                   # Uno bootloader + target boot
    for step in script.split():
        kind, _, arg = step.partition(":")
        if kind == "wait":
            pump(float(arg))
        elif kind == "key":
            for ch in arg:
                s.write(ch.encode())
                pump(0.3)
        elif kind == "until":
            if not pump(30, until=arg):
                print(f"\n[hil] timed out waiting for {arg!r}")
                sys.exit(1)
        else:
            sys.exit(f"unknown step {step!r}")
    s.close()


if __name__ == "__main__":
    main()
