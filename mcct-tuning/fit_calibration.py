"""Fit the sensor calibration from the video tables and plot it.

Usage (from mcct-tuning/):
    ..\\tools\\venv\\Scripts\\python.exe fit_calibration.py [--write]

Inputs  data/cal-cooldown.csv, data/cal-warmup.csv
        columns: display,probe_c   (display exactly as shown: "357" = mV,
        "2.56" = V). Lines starting with # are comments.
Outputs out/calibration.png      graph + residuals
        --write: ../mcct-firmware/calibration/adc_to_temperature.csv

The display shows the window-averaged ADC reading scaled by a nominal
5000 mV (CFG_VDD_MV), not by the real supply. The ADC is ratiometric, so
display / 5000 is exactly the fraction of VDD the firmware works with, and
the calibration is written in those nominal volts with vdd_v = 5.0. The real
rail voltage only matters for reading the numbers as true volts.

The fit uses the cool-down table only: cooling is slow, so the thermocouple
and the probe are nearly in step. The warm-up table is plotted against the
same curve; its offset shows the thermal lag between them, not a sensor error.
The polynomial degree is the smallest (1..3) that the next degree up
improves by less than --min-gain degC rms: the probe reading wanders a few
degC at high temperature, so chasing it with more terms only fits noise. The
firmware's generator refits the same points at that degree, so the chip and
this graph agree.
"""
import argparse
import csv
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
FIRMWARE_CSV = HERE.parent / "mcct-firmware" / "calibration" / "adc_to_temperature.csv"
NOMINAL_VDD_MV = 5000.0
OPERATING_MAX_C = 250       # CFG_TARGET_MAX_C
FIT_MAX_C = 290             # fit a little past the 280 degC fault limit


def display_to_mv(text):
    text = text.strip()
    return float(text) * 1000.0 if "." in text else float(text)


def read_table(path):
    mv, temp = [], []
    with open(path, newline="") as f:
        rows = [r for r in f if r.strip() and not r.lstrip().startswith("#")]
    for row in csv.DictReader(rows):
        mv.append(display_to_mv(row["display"]))
        temp.append(float(row["probe_c"]))
    return np.array(mv), np.array(temp)


def fit(mv, temp, min_gain):
    fits = []
    for degree in (1, 2, 3):
        coef = np.polyfit(mv, temp, degree)
        resid = temp - np.polyval(coef, mv)
        fits.append((degree, coef, resid, np.sqrt(np.mean(resid ** 2))))
    for this, nxt in zip(fits, fits[1:]):
        if this[3] - nxt[3] < min_gain:
            return this[:3]
    return fits[-1][:3]


def equation(coef):
    terms = []
    for power, c in zip(range(len(coef) - 1, -1, -1), coef):
        if power == 0:
            terms.append(f"{c:+.2f}")
        elif power == 1:
            terms.append(f"{c:+.4f}·V")
        else:
            terms.append(f"{c:+.3e}·V^{power}")
    return "T = " + " ".join(terms).lstrip("+")


def write_firmware_csv(mv, temp, degree, note, source="the cool-down table (fit_calibration.py)"):
    lines = [
        "# MCCT sensor calibration: pin 3 reading vs plate surface temperature.",
        "#",
        f"# Generated in mcct-tuning from {source}:",
        f"# {note}. voltage_v is the firmware's",
        "# display value: the ADC fraction of VDD times a nominal 5.000 V, so",
        "# vdd_v stays 5.0 whatever the real rail measures.",
        "#",
        "# vdd_v = 5.0",
        f"# degree = {degree}",
        "voltage_v,temperature_c",
    ]
    lines += [f"{m / 1000:.4f},{t:.1f}" for m, t in sorted(zip(mv, temp))]
    FIRMWARE_CSV.write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="write the firmware calibration CSV")
    ap.add_argument("--min-gain", type=float, default=0.1,
                    help="rms degC a higher degree must save to be used")
    ap.add_argument("--note", default="probe on the plate surface", help="provenance comment")
    args = ap.parse_args()
    if args.write:
        raise SystemExit("--write is disabled: these tables are suspect (probe contact, see "
                         "RECALIBRATION.md); use fit_plateaus.py on a new run")

    cool_path = HERE / "data" / "cal-cooldown.csv"
    warm_path = HERE / "data" / "cal-warmup.csv"
    empty = (np.array([]), np.array([]))
    cool_mv, cool_t = read_table(cool_path) if cool_path.exists() else empty
    warm_mv, warm_t = read_table(warm_path) if warm_path.exists() else empty
    provisional = cool_mv.size == 0
    if provisional:
        # No cool-down table yet: fit the warm-up so the plot can be checked.
        # The curve then carries the warm-up lag, so it is not written out.
        if args.write:
            raise SystemExit("--write needs the cool-down table")
        print("NOTE: no cool-down data; fitting the warm-up table (provisional, includes lag)")
        cool_mv, cool_t, warm_mv, warm_t = warm_mv, warm_t, *empty
    fit_label = "warm-up (fitted, provisional)" if provisional else "cool-down (fitted)"

    in_fit = cool_t <= FIT_MAX_C
    degree, coef, resid = fit(cool_mv[in_fit], cool_t[in_fit], args.min_gain)
    in_range = cool_t[in_fit] <= OPERATING_MAX_C
    slope = np.polyval(np.polyder(coef), np.median(cool_mv[in_fit]))

    print(f"{fit_label.split(' ')[0]}: {in_fit.sum()} points fitted, degree {degree}")
    print(f"  {equation(coef).replace('·', '*')}   (V in mV)")
    print(f"  max |residual| {np.max(np.abs(resid)):.2f} degC "
          f"(<= {OPERATING_MAX_C} degC: {np.max(np.abs(resid[in_range])):.2f}), "
          f"rms {np.sqrt(np.mean(resid ** 2)):.2f}")
    print(f"  sensitivity ~{1 / slope:.2f} mV/degC; display step 10 mV above 1 V = "
          f"{10 * slope:.1f} degC")
    if warm_mv.size:
        lag = np.polyval(coef, warm_mv) - warm_t
        print(f"warm-up: thermocouple ahead of the probe by {np.median(lag):.1f} degC median, "
              f"{lag.max():.1f} max")

    fig, (ax, axr) = plt.subplots(2, 1, figsize=(9, 8), sharex=True,
                                  gridspec_kw={"height_ratios": [3, 1.3]})
    x = np.linspace(min(cool_mv.min(), *(warm_mv if warm_mv.size else [np.inf])), cool_mv.max(), 300)
    ax.axhspan(OPERATING_MAX_C, max(cool_t.max(), *(warm_t if warm_t.size else [0])) + 10,
               color="0.92", zorder=0, label=f"above {OPERATING_MAX_C} °C limit")
    ax.plot(cool_mv, cool_t, "o", ms=4, color="tab:blue", label=fit_label)
    if warm_mv.size:
        ax.plot(warm_mv, warm_t, "^", ms=4, color="tab:orange", label="warm-up (lag, not fitted)")
    ax.plot(x, np.polyval(coef, x), "-", color="black", lw=1.5,
            label=f"fit, degree {degree}: {equation(coef)}")
    ax.set_ylabel("Probe temperature (°C)")
    ax.set_title("MCCT sensor calibration: pin 3 reading vs plate surface")
    ax.grid(alpha=0.3)
    ax.legend(loc="upper left", fontsize=8)

    rms = np.sqrt(np.mean(resid ** 2))
    axr.axhspan(-rms, rms, color="tab:green", alpha=0.12, label=f"±rms ({rms:.1f} °C)")
    axr.axhline(0, color="black", lw=0.8)
    axr.plot(cool_mv[in_fit], resid, "o", ms=4, color="tab:blue", label=fit_label.split(" (")[0] + " residual")
    if warm_mv.size:
        axr.plot(warm_mv, warm_t - np.polyval(coef, warm_mv), "^", ms=4, color="tab:orange",
                 label="warm-up − fit (lag)")
    axr.set_xlabel(f"Sensor reading, display value (mV of a nominal {NOMINAL_VDD_MV / 1000:.1f} V)")
    axr.set_ylabel("Probe − fit (°C)")
    axr.grid(alpha=0.3)
    axr.legend(loc="lower left", fontsize=8)

    out = HERE / "out"
    out.mkdir(exist_ok=True)
    fig.tight_layout()
    fig.savefig(out / "calibration.png", dpi=150)
    print(f"plot: {out / 'calibration.png'}")

    if args.write:
        write_firmware_csv(cool_mv[in_fit], cool_t[in_fit], degree, args.note)
        print(f"wrote {FIRMWARE_CSV}")


if __name__ == "__main__":
    main()
