"""Fit the ADC -> temperature calibration polynomial and emit a C header.

Usage: python gen_calibration.py <calibration.csv> <out/calibration_gen.h>

The CSV holds measured pairs of sensor reading and plate surface temperature
(see calibration/adc_to_temperature.csv for the format). The reading is
converted to u, the fraction of VDD the ADC sees (0..1), and a least-squares
polynomial T(u) in tenths of a degree is fitted. The firmware evaluates it
with Horner's rule in 32-bit fixed point (sensor/temperature.c); this script
emulates that integer arithmetic exactly, checks it cannot overflow, and
reports the fit quality. Pure standard library: no numpy needed.
"""
import csv
import math
import re
import sys

Q16 = 65536
INT32_LIMIT = 2 ** 31
SETTING_RE = re.compile(r"^#\s*(\w+)\s*=\s*([-+0-9.eE]+)")


def fail(msg):
    sys.exit(f"gen_calibration: error: {msg}")


def read_csv(path):
    settings = {"vdd_v": 5.0, "degree": 3}
    rows = []
    with open(path, newline="") as f:
        lines = []
        for line in f:
            m = SETTING_RE.match(line.strip())
            if m:
                settings[m.group(1)] = float(m.group(2))
            if line.strip() and not line.lstrip().startswith("#"):
                lines.append(line)
    reader = csv.DictReader(lines)
    cols = [c.strip() for c in (reader.fieldnames or [])]
    reader.fieldnames = cols
    if "temperature_c" not in cols:
        fail("CSV needs a temperature_c column")
    for row in reader:
        t = float(row["temperature_c"])
        if "voltage_v" in cols:
            u = float(row["voltage_v"]) / settings["vdd_v"]
        elif "adc_fraction" in cols:
            u = float(row["adc_fraction"])
        elif "adc_raw" in cols:            # 12-bit mean reading
            u = float(row["adc_raw"]) / 4096.0
        else:
            fail("CSV needs a voltage_v, adc_fraction or adc_raw column")
        if not 0.0 <= u < 1.0:
            fail(f"reading outside 0..VDD: {row}")
        rows.append((u, t * 10.0))
    return settings, rows


def solve(a, b):
    """Gaussian elimination with partial pivoting."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[piv][col]) < 1e-15:
            fail("calibration data cannot determine the polynomial (too few distinct points?)")
        m[col], m[piv] = m[piv], m[col]
        for r in range(col + 1, n):
            f = m[r][col] / m[col][col]
            for c in range(col, n + 1):
                m[r][c] -= f * m[col][c]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        x[r] = (m[r][n] - sum(m[r][c] * x[c] for c in range(r + 1, n))) / m[r][r]
    return x


def fit(rows, degree):
    n = degree + 1
    ata = [[sum(u ** (i + j) for u, _ in rows) for j in range(n)] for i in range(n)]
    atb = [sum(t * u ** i for u, t in rows) for i in range(n)]
    return solve(ata, atb)


def poly(coeffs, u):
    return sum(c * u ** k for k, c in enumerate(coeffs))


def mul_q16(a, u):
    return (a * u) // Q16          # floor, exactly like fx_mul_q16()


def evaluate_fixed(icoeffs, shift, u_q16, check_overflow=False):
    acc = icoeffs[-1]
    for c in reversed(icoeffs[:-1]):
        acc = mul_q16(acc, u_q16) + c
        if check_overflow and not -INT32_LIMIT <= acc < INT32_LIMIT:
            raise OverflowError
    return (acc + (1 << (shift - 1))) >> shift


def write_if_changed(path, text):
    """Rewrite only when the content changes, so make-style rebuilds stay incremental."""
    try:
        with open(path, newline="") as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path, "w", newline="\n") as f:
        f.write(text)


def main():
    if len(sys.argv) != 3:
        fail("usage: gen_calibration.py <csv> <out.h>")
    src, out = sys.argv[1], sys.argv[2]
    settings, rows = read_csv(src)
    degree = int(settings["degree"])
    if not 1 <= degree <= 4:
        fail("degree must be 1..4")
    if len(rows) < degree + 1:
        fail(f"need at least {degree + 1} points for degree {degree}, have {len(rows)}")

    coeffs = fit(rows, degree)
    total = sum(abs(c) for c in coeffs)
    # Largest shift that keeps every Horner partial sum (bounded by the sum of
    # |c_k| because 0 <= u < 1) below 2^30, leaving a factor-2 margin.
    shift = min(30, int(math.floor(30 - math.log2(max(total, 1.0)))))
    if shift < 4:
        fail("coefficients too large for 32-bit fixed point; check the data")
    icoeffs = [int(round(c * (1 << shift))) for c in coeffs]

    worst_fixed = 0.0
    for u_q16 in list(range(0, Q16, 37)) + [Q16 - 1]:
        try:
            got = evaluate_fixed(icoeffs, shift, u_q16, check_overflow=True)
        except OverflowError:
            fail(f"fixed-point overflow at u={u_q16}")
        if -32000 <= poly(coeffs, u_q16 / Q16) <= 32000:
            worst_fixed = max(worst_fixed, abs(got - poly(coeffs, u_q16 / Q16)))
    if worst_fixed > 1.0:
        fail(f"fixed-point evaluation differs from the fit by {worst_fixed / 10:.2f} degC")

    residuals = [abs(poly(coeffs, u) - t) / 10 for u, t in rows]
    us = sorted(u for u, _ in rows)
    samples = [poly(coeffs, us[0] + (us[-1] - us[0]) * i / 200) for i in range(201)]
    steps = [b - a for a, b in zip(samples, samples[1:])]
    monotonic = all(s > 0 for s in steps) or all(s < 0 for s in steps)

    print(f"gen_calibration: {len(rows)} points, degree {degree}, "
          f"max residual {max(residuals):.2f} degC, "
          f"rms {math.sqrt(sum(r * r for r in residuals) / len(residuals)):.2f} degC, "
          f"fixed-point error {worst_fixed / 10:.3f} degC")
    if not monotonic:
        print("gen_calibration: WARNING: fitted curve is not monotonic over the data range")

    # Test vectors: the firmware must reproduce these exactly.
    vectors = [(u, evaluate_fixed(icoeffs, shift, u)) for u in (0, 1000, 13107, 32768, 45000, 65535)]
    vectors = [(u, t) for u, t in vectors if -32000 <= t <= 32000]

    human = " + ".join(f"{c:.6g}*u^{k}" for k, c in enumerate(coeffs))
    header = [
        f"/* Generated by scripts/gen_calibration.py from {src.replace(chr(92), '/')}. Do not edit.\n",
        f" * T[0.1 degC](u) = {human},  u = Vsensor / VDD\n",
        f" * {len(rows)} points, max residual {max(residuals):.2f} degC */\n",
        "#ifndef CALIBRATION_GEN_H\n#define CALIBRATION_GEN_H\n\n",
        f"#define CAL_DEGREE        {degree}\n",
        f"#define CAL_SHIFT         {shift}\n",
        "#define CAL_COEFFICIENTS  { " + ", ".join(f"{c}L" for c in icoeffs) + " }\n",
        f"#define CAL_MONOTONIC     {1 if monotonic else 0}\n",
        "/* { u_q16, expected 0.1 degC } computed with the firmware's integer math */\n",
        "#define CAL_TEST_VECTORS  { " + ", ".join(f"{{ {u}u, {t} }}" for u, t in vectors) + " }\n",
        "\n#endif\n",
    ]
    write_if_changed(out, "".join(header))


if __name__ == "__main__":
    main()
