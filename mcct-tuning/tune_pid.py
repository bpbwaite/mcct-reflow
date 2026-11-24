"""Tune the firmware's controller offline against the fitted plate model.

Usage (from mcct-tuning/):
    ..\\tools\\venv\\Scripts\\python.exe tune_pid.py [--check KP_X10 TI TD]

The controller is a Python copy of the firmware's (src/control/controller.c,
pid.c, thermal_model.c): model feed-forward, the HOLD setpoint ramp with
its headroom and ease-in, and the ISA PID on the sensor reading, in the
firmware's units (duty 0.01 %, temperatures 0.1 degC, gains as stored in
settings). Many gain sets are simulated at once (numpy over candidates).

Plant: out/plate_model.json (plate_model.py), plus perturbed copies (heat
capacity, losses, heater lag and power off by 15-50 %) so the chosen gains
survive a model that is somewhat wrong. Sensor noise is added.

Scenarios: holds from cold (100, 150, 240 degC), a hold stepped up from a
settled 150 to 200 degC, and the Pb and bi reflow profiles. Score: settling
time to +/- 1 degC, with heavy penalties for overshoot (sensor or surface)
and for a noisy heater duty.

Prints the model constants for user_config.h and the best gains, and
plots the nominal responses to out/tuning.png.
"""
import argparse
import itertools
import json
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
FW = HERE.parent / "mcct-firmware"
DT = 0.25                    # CFG_CONTROL_PERIOD_MS
STEPS_PER_S = 4
DUTY_FULL = 10000.0
HEADROOM = 0.85              # CFG_RAMP_HEADROOM_PERCENT
APPROACH_S = 30              # CFG_RAMP_APPROACH_S
MIN_RAMP_CPS = 10
I_CLAMP_DC = 500             # CFG_PID_I_ERROR_CLAMP_C * 10
D_SHIFT = 3                  # CFG_PID_D_FILTER_SHIFT
AMBIENT = 23.0
NOISE_C = 0.15               # sensor noise, degC rms (to be checked on the chip)


def load_model():
    return json.loads((HERE / "out" / "plate_model.json").read_text())


def model_constants(m, lo=100, hi=250):
    """Firmware model constants: LOSS (0.001 %/degC) and CAP (0.1 %*s/degC).

    The firmware's loss is linear in (T - ambient); fit it to the real
    (convection + radiation) loss over the working range.
    """
    T = np.linspace(lo, hi, 50)
    loss_w = m["k"] * (T - AMBIENT) + m["r_e9"] * 1e-9 * ((T + 273.15) ** 4 - (AMBIENT + 273.15) ** 4)
    pct = 100 * loss_w / m["P_heater_W"]
    loss_x1000 = 1000 * np.sum(pct * (T - AMBIENT)) / np.sum((T - AMBIENT) ** 2)
    cap_x10 = 10 * 100 * m["C"] / m["P_heater_W"]
    return int(round(loss_x1000)), int(round(cap_x10))


PLANTS = {                    # name: (C, loss, tau_h, power) multipliers
    "nominal":      (1.0, 1.0, 1.0, 1.0),
    "heavy":        (1.2, 1.0, 1.0, 1.0),
    "light":        (0.85, 1.0, 1.0, 1.0),
    "lossy":        (1.0, 1.25, 1.0, 1.0),
    "insulated":    (1.0, 0.8, 1.0, 1.0),
    "slow heater":  (1.0, 1.0, 3.0, 1.0),     # ~11 s in total: the lag was only inferred
    "low mains":    (1.0, 1.0, 1.0, 0.85),
}


class Sim:
    """Plant + firmware controller for N gain candidates in parallel."""

    def __init__(self, m, plant, kp, ti, td, loss_x1000, cap_x10, seed=1):
        n = kp.size
        cm, lm, tm, pm = PLANTS[plant]
        self.C = m["C"] * cm
        self.k = m["k"] * lm
        self.r = m["r_e9"] * 1e-9 * lm
        self.tau_h = m["tau_h"] * tm
        self.tau_2 = m["tau_2"] * tm
        self.P = m["P_heater_W"] * pm
        self.tau_p = m["tau_probe"]
        self.kp, self.ti, self.td = kp.astype(float), ti.astype(float), td.astype(float)
        self.loss, self.cap = float(loss_x1000), float(cap_x10)
        self.rng = np.random.default_rng(seed)
        self.n = n
        self.reset_plant(AMBIENT)

    # ---- plant
    def reset_plant(self, t0):
        self.ts = np.full(self.n, t0)
        self.probe = self.ts.copy()
        self.q = np.full(self.n, self.k * (t0 - AMBIENT))      # steady heat flow
        self.h = self.q.copy()

    def plant_step(self, duty):
        ta4 = (AMBIENT + 273.15) ** 4
        loss = self.k * (self.ts - AMBIENT) + self.r * ((self.ts + 273.15) ** 4 - ta4)
        self.ts = self.ts + DT * (self.q - loss) / self.C
        self.q = self.q + DT * (self.h - self.q) / self.tau_2
        self.h = self.h + DT * (self.P * duty / DUTY_FULL - self.h) / self.tau_h
        self.probe = self.probe + DT * (self.ts - self.probe) / self.tau_p

    def pv_dc(self):
        noisy = self.ts + self.rng.normal(0, NOISE_C, self.n)
        return np.floor(noisy * 10)

    # ---- firmware model (thermal_model.c), temperatures in 0.1 degC
    def hold_duty(self, t_dc):
        return self.loss * (t_dc - AMBIENT * 10) / 100

    def duty_to_rate(self, duty):
        return np.clip(np.where(duty <= 0, 0, duty * 10 / self.cap), 0, 3000)

    # ---- controller.c
    def start(self, mode):
        """ctrl_hold / ctrl_track from off: PID reset, setpoint at the plate."""
        self.mode = mode
        pv = self.pv_dc()
        self.sp_cc = pv * 10
        self.rate = np.zeros(self.n)
        self.integral = np.zeros(self.n)
        self.e_filt_q3 = np.zeros(self.n)

    def ramp_setpoint(self, target_dc):
        sp = np.floor(self.sp_cc / 10)
        up = self.duty_to_rate(DUTY_FULL * HEADROOM - self.hold_duty(sp))
        down = self.duty_to_rate(self.hold_duty(sp) * HEADROOM)
        up = np.floor(np.maximum(up, MIN_RAMP_CPS) / STEPS_PER_S)
        down = np.floor(np.maximum(down, MIN_RAMP_CPS) / STEPS_PER_S)
        step = target_dc * 10 - self.sp_cc
        ease = np.floor(np.abs(step) / (APPROACH_S * STEPS_PER_S)) + 1
        up, down = np.minimum(up, ease), np.minimum(down, ease)
        step = np.clip(step, -down, up)
        self.sp_cc = self.sp_cc + step
        self.rate = step * STEPS_PER_S

    def pid(self, sp, pv, ff):
        e = np.clip(sp - pv, -3000, 3000)
        p = self.kp * e
        de = np.floor((e * 8 - self.e_filt_q3) / 2 ** D_SHIFT)
        self.e_filt_q3 = self.e_filt_q3 + de
        de = np.clip(de, -160, 160)
        d = np.floor(self.kp * self.td / 16) * de * 2 * STEPS_PER_S
        di = np.where(self.ti > 0, self.kp * np.clip(e, -I_CLAMP_DC, I_CLAMP_DC) * 64 / np.maximum(self.ti, 1), 0)
        u = ff + p + self.integral / 256 + d
        di = np.where(((u >= DUTY_FULL) & (di > 0)) | ((u <= 0) & (di < 0)), 0, di)
        self.integral = np.clip(self.integral + di, -DUTY_FULL * 256, DUTY_FULL * 256)
        u = ff + p + self.integral / 256 + d
        return np.clip(u, 0, DUTY_FULL)

    def step(self, target_dc=None, track=None):
        """One control period. HOLD: ramp towards target_dc. TRACK: (sp_dc, rate_cps)."""
        pv = self.pv_dc()
        if track is None:
            self.ramp_setpoint(target_dc)
        else:
            self.sp_cc = np.full(self.n, track[0] * 10.0)
            self.rate = np.full(self.n, float(track[1]))
        sp = np.floor(self.sp_cc / 10)
        ff = np.clip(self.hold_duty(sp) + self.cap * self.rate / 10, 0, DUTY_FULL)
        duty = self.pid(sp, pv, ff)
        self.plant_step(duty)
        return pv, sp, duty


LEAD_S = 0                    # reflow ramp feed-forward ends this early (heater lag)


def reflow_track(path):
    """Setpoint/rate per control step for a profile (reflow.c, open-loop timing:
    settle phases are taken as reached, which the score then checks). The
    ramp's feed-forward rate drops to 0 LEAD_S before the ramp ends, so the
    heat still in the element carries the plate the last stretch."""
    prof = json.loads(path.read_text())
    sp, out = AMBIENT * 10, []
    lead = int(LEAD_S * STEPS_PER_S)
    for w in prof["waypoints"]:
        target, n = w["temp_c"] * 10, int(w["ramp_s"] * STEPS_PER_S)
        rate = (target - sp) * 10 / w["ramp_s"] if w["ramp_s"] else 0
        out += [(sp + (target - sp) * i / n, rate if i + lead < n or rate < 0 else 0) for i in range(1, n)]
        out += [(target, 0)] * int((w["hold_s"] + 45) * STEPS_PER_S)
        sp = target
    return out, prof["label"], prof["waypoints"]


def run_scenarios(m, plant, kp, ti, td, loss, cap, keep=False):
    """Return per-candidate cost and (optionally) traces of candidate 0."""
    n = kp.size
    cost = np.zeros(n)
    over = np.zeros(n)
    traces = []

    def hold(sim, target_c, minutes, label):
        nonlocal cost, over
        steps = int(minutes * 60 * STEPS_PER_S)
        pvs = np.empty((steps, n))
        probes = np.empty((steps, n))
        duties = np.empty((steps, n))
        sps = np.empty((steps, n))
        for i in range(steps):
            pvs[i], sps[i], duties[i] = sim.step(target_dc=target_c * 10)
            probes[i] = sim.probe
        true_over = np.max(probes, axis=0) - target_c
        pv_over = np.max(pvs, axis=0) / 10 - target_c
        o = np.maximum(true_over, pv_over)
        out_band = np.abs(pvs / 10 - target_c) > 1.0
        last_out = np.where(out_band.any(axis=0), steps - np.argmax(out_band[::-1], axis=0), 0)
        settle_s = last_out / STEPS_PER_S
        tail = duties[-int(60 * STEPS_PER_S):]
        chatter = np.std(np.diff(tail, axis=0), axis=0) / 100      # % per step
        cost += settle_s + 400 * np.maximum(o - 0.5, 0) ** 2 + 30 * np.maximum(chatter - 2, 0)
        over = np.maximum(over, o)
        if keep:
            traces.append((label, pvs[:, 0] / 10, probes[:, 0], sps[:, 0] / 10, duties[:, 0] / 100))
        return settle_s

    for target, minutes in ((100, 15), (150, 18), (240, 22)):
        sim = Sim(m, plant, kp, ti, td, loss, cap)
        sim.start("hold")
        hold(sim, target, minutes, f"hold {target} from cold")

    sim = Sim(m, plant, kp, ti, td, loss, cap)
    sim.start("hold")
    for i in range(int(18 * 60 * STEPS_PER_S)):
        sim.step(target_dc=1500)
    hold(sim, 200, 12, "150 -> 200")

    for prof in ("1-sn63pb37.json", "3-sn42bi58.json"):
        track, label, wps = reflow_track(FW / "profiles" / prof)
        sim = Sim(m, plant, kp, ti, td, loss, cap)
        sim.start("track")
        peak_t = max(w["temp_c"] for w in wps)
        pvs, probes, sps, duties = [], [], [], []
        for sp_dc, rate in track:
            pv, sp, duty = sim.step(track=(sp_dc, rate))
            pvs.append(pv / 10); probes.append(sim.probe.copy()); sps.append(sp / 10); duties.append(duty / 100)
        pvs, probes = np.array(pvs), np.array(probes)
        heating = np.array([r > 0 for _, r in track])
        lag = np.max(np.where(heating[:, None], np.array(sps) - pvs, 0), axis=0)
        o = np.maximum(np.max(probes, axis=0), np.max(pvs, axis=0)) - peak_t
        cost += 3 * np.maximum(lag - 5, 0) ** 2 + 400 * np.maximum(o - 1.0, 0) ** 2
        over = np.maximum(over, o)
        if keep:
            traces.append((f"reflow {label}", pvs[:, 0], probes[:, 0], np.array(sps)[:, 0], np.array(duties)[:, 0]))
    return cost, over, traces


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lead", type=float, default=11, help="reflow feed-forward lead, s")
    ap.add_argument("--kp", nargs="+", type=int, default=[5, 10, 15, 20, 30, 45, 60, 90],
                    help="Kp grid, 0.1 %%/degC")
    ap.add_argument("--ti", nargs="+", type=int, default=[0, 60, 120, 200, 300, 500], help="Ti grid, s")
    ap.add_argument("--td", nargs="+", type=int, default=[0, 4, 8, 12, 20], help="Td grid, s")
    ap.add_argument("--check", nargs=3, type=int, metavar=("KP_X10", "TI", "TD"),
                    help="only simulate these gains")
    args = ap.parse_args()

    global LEAD_S
    LEAD_S = args.lead
    m = load_model()
    loss, cap = model_constants(m)
    print(f"model constants: CFG_MODEL_LOSS_X1000_DEFAULT {loss}, CFG_MODEL_CAP_X10_DEFAULT {cap}")

    if args.check:
        grid = [tuple(args.check)]
    else:
        grid = list(itertools.product(args.kp, args.ti, args.td))
    kp, ti, td = (np.array(c) for c in zip(*grid))

    total = np.zeros(kp.size)
    worst_over = np.full(kp.size, -99.0)
    for plant in PLANTS:
        cost, over, _ = run_scenarios(m, plant, kp, ti, td, loss, cap)
        total += cost * (2 if plant == "nominal" else 1)
        worst_over = np.maximum(worst_over, over)
    order = np.argsort(total)
    print("best gains (Kp x10, Ti s, Td s): cost, worst overshoot over all plants")
    for i in order[:8]:
        print(f"  {kp[i]:4d} {ti[i]:4d} {td[i]:3d}   cost {total[i]:8.0f}   overshoot {worst_over[i]:+.1f} degC")

    best = order[0]
    bk, bi, bd = (np.array([v[best]]) for v in (kp, ti, td))
    fig, axes = plt.subplots(len(PLANTS), 1, figsize=(12, 3.0 * len(PLANTS)))
    for ax, plant in zip(axes, PLANTS):
        _, over, traces = run_scenarios(m, plant, bk, bi, bd, loss, cap, keep=True)
        t0 = 0.0
        for label, pv, probe, sp, duty in traces:
            x = t0 + np.arange(pv.size) * DT / 60
            ax.plot(x, sp, color="0.5", lw=0.8)
            ax.plot(x, pv, color="tab:blue", lw=1)
            ax.plot(x, probe, color="tab:orange", lw=0.8, alpha=0.8)
            ax.text(t0 + 0.3, 255, label, fontsize=7, va="top")
            t0 = x[-1] + 2
        ax.set_ylim(0, 265)
        ax.set_ylabel("°C")
        ax.set_title(f"plant: {plant}   (worst overshoot {over[0]:+.1f} °C)", fontsize=9)
        ax.grid(alpha=0.3)
    axes[0].plot([], [], color="0.5", label="setpoint")
    axes[0].plot([], [], color="tab:blue", label="sensor")
    axes[0].plot([], [], color="tab:orange", label="surface (probe model)")
    axes[0].legend(loc="lower right", fontsize=8)
    axes[-1].set_xlabel("Scenario time (min; scenarios placed side by side)")
    fig.suptitle(f"Simulated firmware control, Kp {bk[0] / 10:g} %/°C, Ti {bi[0]} s, Td {bd[0]} s, "
                 f"LOSS {loss}, CAP {cap}")
    fig.tight_layout()
    fig.savefig(HERE / "out" / "tuning.png", dpi=130)
    print(f"plot: {HERE / 'out' / 'tuning.png'}")


if __name__ == "__main__":
    main()
