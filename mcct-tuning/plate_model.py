"""Thermal model of the hot plate, fitted to logged runs.

    tau_h dH/dt = P*u - H                          heater element
    tau_2 dQ/dt = H - Q                            heat reaching the sensor
    C dTs/dt    = Q - k (Ts - Ta) - r ((Ts+273)^4 - (Ta+273)^4)
    surface: first-order lag of Ts with time constant tau_probe (fixed, see below)

u is the heater duty (0..1) and P the full heater power (350 W nominal).
Q is the heat flow into the plate. Two lags in series (heater element, and
the path to the sensor) give the few seconds of dead time seen after a step.
The lags are inferred from logged plateau steps, not from a lag experiment. Ts is what the firmware sees (the calibrated
sensor reading), so the controller is tuned on u -> Ts.

Usage (from mcct-tuning/):
    ..\\tools\\venv\\Scripts\\python.exe plate_model.py [--runs log ...] [--check log ...]

Fits the 2026-11-22 video run (full-power warm-up, then cool-down) and the
2026-11-23 plateau logs (sensor mV and duty). Only their sensor mV and heater
power are used, on the firmware's current calibration: the video's probe had
poor contact. Samples above FIT_MAX_C are ignored (the calibration is
extrapolated there); the cool-down starts below it, with no heat left in the
element. tau_probe needs a trustworthy probe trace, so it stays at the first
fit's value; it hardly matters to the tuning. --runs adds hil.py logs
(telemetry on) to the fit, --check only compares them. Output:
out/plate_model.json, out/plate_model.png.
"""
import argparse
import csv
import json
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import least_squares

import fit_calibration as cal

HERE = pathlib.Path(__file__).resolve().parent
P_HEATER_W = 350.0
PARAMS = ["tau_h", "tau_2", "C", "k", "r_e9"]
TAU_PROBE_S = 2.19
FIT_MAX_C = 260.0
AMBIENT_C = 23.0
UNITS = ["s", "s", "J/K", "W/K", "1e-9 W/K^4"]
GUESS = [4.0, 2.0, 150.0, 0.2, 0.5]
BOUNDS = ([0.5, 0.5, 20, 0, 0], [120, 120, 1000, 3, 20])


class Run:
    """A logged run: time (s), duty (0..1), sensor degC, ambient degC."""

    def __init__(self, name, t, u, ts, ambient=None):
        self.name, self.t, self.u, self.ts = name, t, u, ts
        self.ambient = ambient if ambient is not None else float(ts[0])
        self.fit = ts <= FIT_MAX_C


def video_runs(coef):
    """The calibration video: warm-up at full power, and cool-down at zero."""
    def table(name):
        with open(HERE / "data" / name, newline="") as f:
            rows = list(csv.DictReader(r for r in f if r.strip() and not r.lstrip().startswith("#")))
        t = np.array([float(r["time_s"]) for r in rows])
        return t, np.polyval(coef, [cal.display_to_mv(r["display"]) for r in rows])

    tw, warm = table("cal-warmup.csv")
    tc, cool = table("cal-cooldown.csv")
    i = int(np.argmax(cool <= FIT_MAX_C))
    return [Run("video warm-up", tw, np.ones(tw.size), warm, ambient=AMBIENT_C),
            Run("video cool-down", tc[i:], np.zeros(tc.size - i), cool[i:], ambient=AMBIENT_C)]


def plateau_run(path, coef):
    """Telemetry of the -Cal firmware (t_ms,q16,mv_x10,set_mv,duty,state)."""
    text = path.read_text(errors="replace")
    text = text[text.rindex("t_ms,q16"):]           # skip lines from before the reset
    rows, last = [], 0
    for line in text.splitlines():
        try:
            t, q16, mv10, _, duty, state = (int(p) for p in line.split(","))
        except ValueError:
            continue
        # Drop garbled lines: readings must agree, time must move on plausibly.
        if abs(q16 * 50000 / 65536 - mv10) > 3 or not 0 < t - last <= 5000 or t % 250:
            continue
        if not 0 <= duty <= 4000 or (state != 1 and duty):
            continue
        last = t
        rows.append((t / 1000, mv10 / 10, duty / 10000))
    t, mv, u = np.array(rows).T
    # A garbled duty can still be legal: take the median of three.
    u[1:-1] = np.median([u[:-2], u[1:-1], u[2:]], axis=0)
    return Run(path.name.split(".")[0], t, u, np.polyval(coef, mv), ambient=AMBIENT_C)


def log_run(path, coef):
    """A closed-loop or step run logged by hil.py (firmware telemetry).

    Uses the firmware's own mV reading through the same calibration fit, so
    it is on the same scale as the other runs.
    """
    import telemetry
    d = telemetry.read(path)
    return Run(pathlib.Path(path).stem, d["t"], d["duty_pct"] / 100.0, np.polyval(coef, d["mv"]),
               ambient=float(np.polyval(coef, d["mv"][0])) if d["pv_c"][0] < 40 else 23.0)


def simulate(p, run, t=None, u=None, dt=0.25):
    """Euler-integrate the model on a fixed grid; return Ts at the run's times."""
    tau_h, tau_2, C, k, r_e9 = p
    r = r_e9 * 1e-9
    t = run.t if t is None else t
    u = run.u if u is None else u
    grid = np.arange(t[0], t[-1] + dt, dt)
    # Duty is piecewise constant: the value logged at a row holds until the next row.
    ug = u[np.clip(np.searchsorted(t, grid, side="right") - 1, 0, u.size - 1)]
    ta = run.ambient
    ta4 = (ta + 273.15) ** 4
    h = q = 0.0
    ts = run.ts[0]
    out_ts = np.empty(grid.size)
    for i, ui in enumerate(ug):
        out_ts[i] = ts
        ts += dt * (q - k * (ts - ta) - r * ((ts + 273.15) ** 4 - ta4)) / C
        q += dt * (h - q) / tau_2
        h += dt * (P_HEATER_W * ui - h) / tau_h
    return np.interp(t, grid, out_ts)


def residuals(p, runs):
    res = []
    for run in runs:
        # one residual per 2 s, so densely logged runs don't drown the others
        at = np.arange(run.t[0], run.t[-1], 2.0)
        keep = np.interp(at, run.t, run.ts) <= FIT_MAX_C
        err = np.interp(at, run.t, simulate(p, run) - run.ts)
        res.append(err[keep])
        # Change over 10 s: losses differ a little from day to day, and that
        # level error must not be traded against the lags.
        res.append(3 * (err[5:] - err[:-5])[keep[5:] & keep[:-5]])
    return np.concatenate(res)


def fit_model(runs):
    sol = least_squares(residuals, GUESS, bounds=BOUNDS, args=(runs,), x_scale="jac")
    return sol.x


def loss_w(p, t, ta=AMBIENT_C):
    _, _, _, k, r_e9 = p
    return k * (t - ta) + r_e9 * 1e-9 * ((t + 273.15) ** 4 - (ta + 273.15) ** 4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", nargs="*", default=[], help="hil.py logs to add to the fit")
    ap.add_argument("--check", nargs="*", default=[], help="hil.py logs to compare only")
    args = ap.parse_args()

    coef = cal.firmware_coef()
    runs = video_runs(coef)
    runs += [plateau_run(HERE / "data" / f"plateaus-{n}.csv.log", coef) for n in ("1", "1b", "2")]
    runs += [log_run(q, coef) for q in args.runs]
    p = fit_model(runs)

    fitted = [r.name for r in runs]
    runs += [log_run(q, coef) for q in args.check]

    print("fitted model:")
    for name, unit, v in zip(PARAMS, UNITS, p):
        print(f"  {name:9s} {v:9.3f} {unit}")
    for T in (50, 100, 150, 200, 250):
        L = loss_w(p, T)
        print(f"  hold {T:3d} degC: {L:5.1f} W = {100 * L / P_HEATER_W:4.1f} % duty, "
              f"plate tau {p[2] / (L / (T - 23)):5.0f} s")
    for run in runs:
        err = (simulate(p, run) - run.ts)[run.fit]
        print(f"  {run.name}: sensor rms {np.sqrt(np.mean(err ** 2)):.2f}, worst {np.abs(err).max():.1f} degC"
              + ("" if run.name in fitted else " (check only)"))

    out = HERE / "out"
    out.mkdir(exist_ok=True)
    (out / "plate_model.json").write_text(json.dumps(
        {"P_heater_W": P_HEATER_W, **dict(zip(PARAMS, map(float, p))),
         "tau_probe": TAU_PROBE_S, "runs": fitted}, indent=2))

    fig, axes = plt.subplots(len(runs), 1, figsize=(11, 4.2 * len(runs)), squeeze=False)
    for ax, run in zip(axes[:, 0], runs):
        ts = simulate(p, run)
        x = run.t / 60
        ax.plot(x, run.ts, color="tab:blue", lw=1.2, label="sensor, measured (calibrated °C)")
        ax.plot(x, ts, color="black", lw=1, ls="--", label="sensor, model")
        ax2 = ax.twinx()
        ax2.fill_between(x, 100 * run.u, step="post", color="tab:red", alpha=0.08)
        ax2.set_ylim(0, 105)
        ax2.set_ylabel("Heater duty (%)", color="tab:red")
        ax.set_xlabel("Time (min)")
        ax.set_ylabel("Temperature (°C)")
        ax.set_title(f"Plate model vs run '{run.name}'")
        ax.grid(alpha=0.3)
        ax.legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    fig.savefig(out / "plate_model.png", dpi=150)
    print(f"plot: {out / 'plate_model.png'}")


if __name__ == "__main__":
    main()
