"""Thermal model of the hot plate, fitted to logged runs.

    tau_h dQ/dt = P*u - Q                          heat reaching the plate
    C dTs/dt    = Q - k (Ts - Ta) - r ((Ts+273)^4 - (Ta+273)^4)
    probe: first-order lag of Ts with time constant tau_probe

u is the heater duty (0..1) and P the full heater power (350 W nominal).
Q is the heat flow into the plate: the heater element's own heat capacity
makes it trail the duty by tau_h (a two-node element/plate model fitted to
the video run collapsed to exactly this: a tiny element with weak coupling,
fixed only by their ratio). Ts is what the firmware sees (the calibrated
sensor reading), so the controller is tuned on u -> Ts; the probe lag says
how far the surface trails the sensor while the temperature moves.

Usage (from mcct-tuning/):
    ..\\tools\\venv\\Scripts\\python.exe plate_model.py [--runs run1.csv ...]

With no --runs it fits the video calibration run (warm-up at full power,
then cool-down at zero), which identifies the model but excites only two
duty levels. Log files from the step experiment (columns t_s, duty, mv) are
added with --runs (hil.py logs with telemetry on). The fitted parameters go to out/plate_model.json and the
fit plot to out/plate_model.png.
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
PARAMS = ["tau_h", "C", "k", "r_e9", "tau_probe"]
UNITS = ["s", "J/K", "W/K", "1e-9 W/K^4", "s"]
GUESS = [10.0, 180.0, 0.2, 0.5, 5.0]
BOUNDS = ([0.5, 20, 0, 0, 0.3], [120, 1000, 3, 20, 120])


class Run:
    """A logged run: time (s), duty (0..1), sensor degC, optional probe degC."""

    def __init__(self, name, t, u, ts, probe=None, ambient=None):
        self.name, self.t, self.u, self.ts, self.probe = name, t, u, ts, probe
        self.ambient = ambient if ambient is not None else float(ts[0])


def video_run(coef):
    """The calibration video: warm-up at full power, then cool-down at zero."""
    def table(name):
        with open(HERE / "data" / name, newline="") as f:
            rows = list(csv.DictReader(r for r in f if r.strip() and not r.lstrip().startswith("#")))
        t = np.array([float(r["time_s"]) for r in rows])
        mv = np.array([cal.display_to_mv(r["display"]) for r in rows])
        return t, mv, np.array([float(r["probe_c"]) for r in rows])

    tw, mvw, pw = table("cal-warmup.csv")
    tc, mvc, pc = table("cal-cooldown.csv")
    t = np.r_[tw, tc + tw[-1]]
    u = np.r_[np.ones(tw.size), np.zeros(tc.size)]
    return Run("video", t, u, np.polyval(coef, np.r_[mvw, mvc]), np.r_[pw, pc], ambient=23.0)


def log_run(path, coef):
    """A closed-loop or step run logged by hil.py (firmware telemetry).

    Uses the firmware's own mV reading through the same calibration fit, so
    it is on the same scale as the video run.
    """
    import telemetry
    d = telemetry.read(path)
    return Run(pathlib.Path(path).stem, d["t"], d["duty_pct"] / 100.0, np.polyval(coef, d["mv"]),
               ambient=float(np.polyval(coef, d["mv"][0])) if d["pv_c"][0] < 40 else 23.0)


def simulate(p, run, t=None, u=None, dt=0.25):
    """Euler-integrate the model on a fixed grid; return Ts and probe at the run's times."""
    tau_h, C, k, r_e9, tau = p
    r = r_e9 * 1e-9
    t = run.t if t is None else t
    u = run.u if u is None else u
    grid = np.arange(t[0], t[-1] + dt, dt)
    # Duty is piecewise constant: the value logged at a row holds until the next row.
    ug = u[np.clip(np.searchsorted(t, grid, side="right") - 1, 0, u.size - 1)]
    ta = run.ambient
    ta4 = (ta + 273.15) ** 4
    q = 0.0
    ts = pr = run.ts[0]
    out_ts = np.empty(grid.size)
    out_pr = np.empty(grid.size)
    for i, ui in enumerate(ug):
        out_ts[i], out_pr[i] = ts, pr
        ts += dt * (q - k * (ts - ta) - r * ((ts + 273.15) ** 4 - ta4)) / C
        q += dt * (P_HEATER_W * ui - q) / tau_h
        pr += dt * (ts - pr) / tau
    return np.interp(t, grid, out_ts), np.interp(t, grid, out_pr)


def residuals(p, runs):
    res = []
    for run in runs:
        ts, pr = simulate(p, run)
        res.append(ts - run.ts)
        if run.probe is not None:
            res.append(0.5 * (pr - run.probe))      # the probe is noisier: half weight
    return np.concatenate(res)


def fit_model(runs):
    sol = least_squares(residuals, GUESS, bounds=BOUNDS, args=(runs,), x_scale="jac")
    return sol.x


def loss_w(p, t, ta=23.0):
    _, _, k, r_e9, _ = p
    return k * (t - ta) + r_e9 * 1e-9 * ((t + 273.15) ** 4 - (ta + 273.15) ** 4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", nargs="*", default=[], help="step-experiment logs to add")
    ap.add_argument("--no-video", action="store_true", help="fit only the --runs logs")
    args = ap.parse_args()

    cool_mv, cool_t = cal.read_table(HERE / "data" / "cal-cooldown.csv")
    in_fit = cool_t <= cal.FIT_MAX_C
    _, coef, _ = cal.fit(cool_mv[in_fit], cool_t[in_fit], 0.1)

    runs = [] if args.no_video else [video_run(coef)]
    runs += [log_run(p, coef) for p in args.runs]
    p = fit_model(runs)

    print("fitted model:")
    for name, unit, v in zip(PARAMS, UNITS, p):
        print(f"  {name:9s} {v:9.3f} {unit}")
    for T in (50, 100, 150, 200, 250):
        L = loss_w(p, T)
        print(f"  hold {T:3d} degC: {L:5.1f} W = {100 * L / P_HEATER_W:4.1f} % duty, "
              f"plate tau {p[1] / (L / (T - 23)):5.0f} s")
    for run in runs:
        ts, pr = simulate(p, run)
        msg = f"  {run.name}: sensor rms {np.sqrt(np.mean((ts - run.ts) ** 2)):.2f} degC"
        if run.probe is not None:
            msg += f", probe rms {np.sqrt(np.mean((pr - run.probe) ** 2)):.2f} degC"
        print(msg)

    out = HERE / "out"
    out.mkdir(exist_ok=True)
    (out / "plate_model.json").write_text(json.dumps(
        {"P_heater_W": P_HEATER_W, **dict(zip(PARAMS, map(float, p))),
         "runs": [r.name for r in runs]}, indent=2))

    fig, axes = plt.subplots(len(runs), 1, figsize=(11, 4.2 * len(runs)), squeeze=False)
    for ax, run in zip(axes[:, 0], runs):
        ts, pr = simulate(p, run)
        x = run.t / 60
        ax.plot(x, run.ts, color="tab:blue", lw=1.2, label="sensor, measured (calibrated °C)")
        ax.plot(x, ts, color="black", lw=1, ls="--", label="sensor, model")
        if run.probe is not None:
            ax.plot(x, run.probe, color="tab:orange", lw=1, alpha=0.8, label="probe, measured")
            ax.plot(x, pr, color="tab:red", lw=1, ls=":", label="probe, model")
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
