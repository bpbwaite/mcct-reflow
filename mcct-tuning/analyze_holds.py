"""Score a closed-loop hold run logged by hil.py and plot it.

Usage (from mcct-tuning/):
    ..\\tools\\venv\\Scripts\\python.exe analyze_holds.py logs/holds-....log [--targets 100 150 200 240]

Each target is a stage: from the moment the setpoint starts moving towards
it until the next stage. Per stage: rise time (to within 1 degC), overshoot
of the sensor, settling time (stays within +/- 1 degC), steady-state mean
error and noise over the stage's last 5 minutes, and the steady duty next
to what the plate model predicts. Plot: out/<log name>.png.
"""
import argparse
import json
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import telemetry

HERE = pathlib.Path(__file__).resolve().parent


def model_hold_pct(T, ambient=23.0):
    m = json.loads((HERE / "out" / "plate_model.json").read_text())
    loss = m["k"] * (T - ambient) + m["r_e9"] * 1e-9 * ((T + 273.15) ** 4 - (ambient + 273.15) ** 4)
    return 100 * loss / m["P_heater_W"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--targets", nargs="+", type=float, default=[100, 150, 200, 240])
    args = ap.parse_args()

    d = telemetry.read(args.log)
    t, pv, sp, duty = d["t"], d["pv_c"], d["sp_c"], d["duty_pct"]
    holding = d["mode"] == 1

    # Stage boundaries: the first sample where the setpoint heads for each target.
    starts = []
    for i, target in enumerate(args.targets):
        prev = args.targets[i - 1] if i else -1e9
        cand = np.nonzero(holding & (sp > prev + 0.05) & (t > (starts[-1] if starts else -1)))[0] if i else \
            np.nonzero(holding)[0]
        if not cand.size:
            break
        starts.append(t[cand[0]])
    ends = starts[1:] + [t[holding][-1] if holding.any() else t[-1]]

    print(f"{'target':>6} {'rise':>7} {'overshoot':>9} {'settle':>7} {'ss err':>7} {'noise':>6} "
          f"{'duty':>6} {'model':>6}")
    for target, a, b in zip(args.targets, starts, ends):
        m = (t >= a) & (t < b)
        tt, p = t[m] - a, pv[m]
        reached = np.nonzero(p >= target - 1)[0]
        rise = tt[reached[0]] if reached.size else np.nan
        over = p.max() - target
        out = np.abs(p - target) > 1.0
        settle = tt[np.nonzero(out)[0][-1]] if out.any() else 0.0
        last = m & (t >= b - 300)
        err = pv[last].mean() - target
        noise = pv[last].std()
        dmean = duty[last].mean()
        print(f"{target:6.0f} {rise / 60:6.1f}m {over:+8.1f}C {settle / 60:6.1f}m {err:+6.2f}C "
              f"{noise:5.2f}C {dmean:5.1f}% {model_hold_pct(target):5.1f}%")

    fig, (ax, axd) = plt.subplots(2, 1, figsize=(12, 7), sharex=True, gridspec_kw={"height_ratios": [3, 1.2]})
    x = t / 60
    ax.plot(x, sp, color="0.5", lw=1, label="setpoint")
    ax.plot(x, pv, color="tab:blue", lw=1, label="sensor (firmware pv)")
    for target in args.targets:
        ax.axhline(target, color="0.85", lw=0.8, zorder=0)
    ax.set_ylabel("Temperature (°C)")
    ax.set_title(f"Closed-loop holds on the plate: {pathlib.Path(args.log).name}")
    ax.grid(alpha=0.3)
    ax.legend(loc="lower right", fontsize=8)
    axd.plot(x, duty, color="tab:red", lw=0.6, label="heater duty")
    axd.set_ylabel("Duty (%)")
    axd.set_xlabel("Time (min)")
    axd.set_ylim(0, 100)
    axd.grid(alpha=0.3)

    out = HERE / "out" / (pathlib.Path(args.log).stem + ".png")
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    print(f"plot: {out}")


if __name__ == "__main__":
    main()
