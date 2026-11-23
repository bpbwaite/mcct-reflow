"""Plot the calibration run as one heat-and-cool cycle over time.

Usage (from mcct-tuning/):
    ..\\tools\\venv\\Scripts\\python.exe plot_cycle.py

Inputs  data/cal-warmup.csv then data/cal-cooldown.csv, stitched in that
        order, time 0 = heater off. A time_s column (seconds, each table from
        0) gives the real time axis. Without it, rows are assumed evenly
        spaced over the measured durations (--cooldown-s 39 min 4 s,
        --warmup-s 3 min 5 s). Rough: rows were logged on change, so they
        bunch up below 1 V where the display shows 1 mV steps.
Output  out/cycle.png

Two panels, each with raw and fitted traces:
    mV   raw sensor reading, and the probe temperature mapped to the mV the
         sensor would show (inverse of the calibration fit)
    degC raw probe reading, and the sensor reading through the fit
The gap between the two traces in a panel is the thermal lag plus probe
noise; both panels show the same gap in different units.
"""
import argparse
import csv
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import fit_calibration as cal

HERE = pathlib.Path(__file__).resolve().parent


def read_rows(path):
    with open(path, newline="") as f:
        rows = [r for r in f if r.strip() and not r.lstrip().startswith("#")]
    rows = list(csv.DictReader(rows))
    mv = np.array([cal.display_to_mv(r["display"]) for r in rows])
    temp = np.array([float(r["probe_c"]) for r in rows])
    t = np.array([float(r["time_s"]) for r in rows]) if rows and "time_s" in rows[0] else None
    return mv, temp, t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cooldown-s", type=float, default=39 * 60 + 4,
                    help="cool-down table duration (default: measured 39 min 4 s)")
    ap.add_argument("--warmup-s", type=float, default=3 * 60 + 5,
                    help="warm-up table duration (default: measured 3 min 5 s; "
                         "0 = estimate from the cool-down row rate)")
    args = ap.parse_args()

    warm = read_rows(HERE / "data" / "cal-warmup.csv")
    cool = read_rows(HERE / "data" / "cal-cooldown.csv")

    in_fit = cool[1] <= cal.FIT_MAX_C
    degree, coef, _ = cal.fit(cool[0][in_fit], cool[1][in_fit], 0.1)

    mv = np.r_[warm[0], cool[0]]
    probe = np.r_[warm[1], cool[1]]
    n_warm, n_cool = warm[0].size, cool[0].size
    if warm[2] is not None and cool[2] is not None:
        # Each table's clock starts at 0; the cool-down follows the last warm-up row.
        t = np.r_[warm[2] - warm[2][-1], cool[2]]
        xlabel = "Time from heater off (min), video timestamps"
    else:
        cool_dt = args.cooldown_s / (n_cool - 1)
        warm_s = args.warmup_s or cool_dt * n_warm
        t = np.r_[np.linspace(-warm_s, 0, n_warm, endpoint=False),
                  np.linspace(0, args.cooldown_s, n_cool)]
        how = "measured" if args.warmup_s else "estimated from the cool-down row rate"
        xlabel = (f"Time from heater off (min)\nrows assumed evenly spaced: cool-down "
                  f"{args.cooldown_s / 60:.1f} min measured, warm-up {warm_s / 60:.1f} min {how}")
        print(f"cool-down {n_cool} rows over {args.cooldown_s:.0f} s ({cool_dt:.2f} s/row); "
              f"warm-up {n_warm} rows over {warm_s:.0f} s ({how})")
    x = t / 60.0
    heat_end = 0.0

    sensor_c = np.polyval(coef, mv)
    # Inverse of the fit: the fit is monotonic over the data, so interpolate.
    grid = np.linspace(mv.min() - 100, mv.max() + 100, 4000)
    probe_mv = np.interp(probe, np.polyval(coef, grid), grid)

    fig, (ax_mv, ax_c) = plt.subplots(2, 1, figsize=(11, 8), sharex=True)
    for ax in (ax_mv, ax_c):
        ax.axvspan(x[0], heat_end, color="tab:red", alpha=0.07, label="heater on (full)")
        ax.grid(alpha=0.3)

    ax_mv.plot(x, mv, "-", color="tab:blue", lw=1.3, label="sensor, raw (display mV)")
    ax_mv.plot(x, probe_mv, "-", color="tab:orange", lw=1.0, alpha=0.85,
               label="probe, fitted (probe °C through the inverse fit)")
    ax_mv.set_ylabel("Sensor reading (mV of nominal 5.0 V)")
    ax_mv.set_title("MCCT calibration run: heat-up and cool-down")
    ax_mv.legend(loc="upper right", fontsize=8)

    ax_c.plot(x, probe, "-", color="tab:orange", lw=1.3, label="probe, raw (°C)")
    ax_c.plot(x, sensor_c, "-", color="tab:blue", lw=1.0, alpha=0.85,
              label=f"sensor, fitted (degree {degree} fit, °C)")
    ax_c.axhline(cal.OPERATING_MAX_C, color="0.4", ls="--", lw=0.8,
                 label=f"{cal.OPERATING_MAX_C} °C limit")
    ax_c.set_ylabel("Temperature (°C)")
    ax_c.set_xlabel(xlabel)
    ax_c.legend(loc="upper right", fontsize=8)

    out = HERE / "out"
    out.mkdir(exist_ok=True)
    fig.tight_layout()
    fig.savefig(out / "cycle.png", dpi=150)
    print(f"plot: {out / 'cycle.png'}")

    # Rates the thermal model will need (sensor, through the fit).
    for name, sl in (("warm-up", slice(0, n_warm)), ("cool-down", slice(n_warm, None))):
        tt, cc = t[sl], sensor_c[sl]
        for lo, hi in ((50, 100), (100, 150), (150, 200), (200, 250)):
            m = (cc >= lo) & (cc < hi)
            if m.sum() > 5:
                print(f"  {name} {lo}-{hi} degC: {np.polyfit(tt[m], cc[m], 1)[0] * 60:+.1f} degC/min")


if __name__ == "__main__":
    main()
