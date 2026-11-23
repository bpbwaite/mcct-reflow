"""Fit the sensor calibration from a plateau run (bench/cal_plateaus.py).

Usage: ..\\tools\\venv\\Scripts\\python.exe fit_plateaus.py data/<run>.csv [more.csv] [--write]

Degree (1..3) is chosen by leave-one-out error: the lowest degree within 10 %
of the best. Plateaus visited twice show repeatability (a shifted probe).
--write is refused for too few points, poor repeatability, a non-rising
curve, or a worst residual above --max-resid. Plot: out/plateaus.png
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


def loo_rms(mv, t, degree):
    idx = np.arange(mv.size)
    err = [t[i] - np.polyval(np.polyfit(mv[idx != i], t[idx != i], degree), mv[i]) for i in idx]
    return float(np.sqrt(np.mean(np.square(err))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--note", default="plateau run")
    ap.add_argument("--max-resid", type=float, default=2.0, help="degC")
    args = ap.parse_args()

    rows = [r for path in args.runs for r in csv.DictReader(open(path, newline=""))]
    if len(rows) < 6:
        raise SystemExit(f"only {len(rows)} plateaus: need at least 6")
    mv = np.array([float(r["mv"]) for r in rows])
    t = np.array([float(r["probe_c"]) for r in rows])

    scores = {d: loo_rms(mv, t, d) for d in (1, 2, 3)}
    degree = min(d for d in scores if scores[d] <= 1.1 * min(scores.values()))
    coef = np.polyfit(mv, t, degree)
    resid = t - np.polyval(coef, mv)
    x = np.linspace(mv.min(), mv.max(), 300)
    rising = bool(np.all(np.polyval(np.polyder(coef), x) > 0))

    print("leave-one-out rms: " + ", ".join(f"degree {d} {s:.2f}" for d, s in scores.items()))
    print(f"degree {degree}: {cal.equation(coef).replace(chr(183), '*')}  (V in mV)")
    print(f"residuals: rms {np.sqrt(np.mean(resid ** 2)):.2f}, worst {np.abs(resid).max():.2f} degC")
    slope = np.polyval(np.polyder(coef), mv)
    spread = 0.0
    for i in range(mv.size):
        for j in range(i):
            if abs(mv[i] - mv[j]) < 10:
                gap = abs((t[i] - t[j]) - slope[i] * (mv[i] - mv[j]))
                spread = max(spread, gap)
                print(f"repeat at {mv[i]:.0f} mV: {t[j]:.1f} / {t[i]:.1f} degC ({gap:.1f} apart)")
    print(f"valid for {mv.min():.0f}..{mv.max():.0f} mV = {t.min():.0f}..{t.max():.0f} degC; "
          f"readings beyond that are extrapolated")

    fig, (ax, axr) = plt.subplots(2, 1, figsize=(9, 8), sharex=True, gridspec_kw={"height_ratios": [3, 1]})
    ax.plot(mv, t, "o", label="plateaus")
    ax.plot(x, np.polyval(coef, x), "k-", label=f"fit, degree {degree}: {cal.equation(coef)}")
    ax.set_ylabel("Probe temperature (°C)")
    ax.set_title("MCCT plateau calibration")
    ax.legend(fontsize=8)
    axr.axhline(0, color="k", lw=0.8)
    axr.plot(mv, resid, "o")
    axr.set_ylabel("Probe − fit (°C)")
    axr.set_xlabel("Sensor reading (mV of a nominal 5.0 V)")
    for a in (ax, axr):
        a.grid(alpha=0.3)
    (HERE / "out").mkdir(exist_ok=True)
    fig.tight_layout()
    fig.savefig(HERE / "out" / "plateaus.png", dpi=150)

    if args.write:
        why = []
        if not rising:
            why.append("the curve is not rising throughout")
        if np.abs(resid).max() > args.max_resid:
            why.append(f"worst residual above {args.max_resid} degC")
        if spread > args.max_resid:
            why.append(f"repeats differ by {spread:.1f} degC")
        if why:
            raise SystemExit("not written: " + "; ".join(why))
        cal.write_firmware_csv(mv, t, degree, args.note, source="the plateau table (fit_plateaus.py)")
        print(f"wrote {cal.FIRMWARE_CSV}")


if __name__ == "__main__":
    main()
