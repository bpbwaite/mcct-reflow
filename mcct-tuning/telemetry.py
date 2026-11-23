"""Read the firmware's console telemetry from a hil.py log.

Telemetry lines (src/debug/cli.h): ms, mv, pv, sp, duty, heater, mode, p, i, d
    ms     16-bit millisecond clock (wraps every 65.5 s: unwrapped here)
    mv     sensor pin, mV of the nominal 5.000 V
    pv sp  0.1 degC
    duty p i d   0.01 %
Lines garbled by the serial bridge (wrong field count, non-numbers) are
skipped.
"""
import numpy as np

FIELDS = ["ms", "mv", "pv", "sp", "duty", "heater", "mode", "p", "i", "d"]


def read(path):
    rows = []
    with open(path, errors="replace") as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) != len(FIELDS):
                continue
            try:
                rows.append([int(v) for v in parts])
            except ValueError:
                continue
    a = np.array(rows, dtype=float)
    ms = a[:, 0]
    step = np.diff(ms)
    step[step < -30000] += 65536                  # 16-bit wrap
    t = np.r_[0, np.cumsum(step)] / 1000.0
    # Drop samples whose clock jumps oddly (a garbled line that still parsed).
    ok = np.r_[True, (step > 0) & (step < 2000)]
    d = {name: a[ok, i] for i, name in enumerate(FIELDS)}
    d["t"] = t[ok]
    d["pv_c"] = d["pv"] / 10
    d["sp_c"] = d["sp"] / 10
    d["duty_pct"] = d["duty"] / 100
    return d
