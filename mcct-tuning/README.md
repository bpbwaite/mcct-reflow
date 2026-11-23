# mcct-tuning

Offline analysis for the MCCT hot plate: sensor calibration, a thermal
model of the plate, and PID tuning against it. Python 3 with numpy, scipy
and matplotlib (`requirements.txt`, installed in `../tools/venv`). Run from
this folder, e.g. `..\tools\venv\Scripts\python.exe fit_calibration.py`.
Plots go to `out/` (not tracked).

| Script | Input | Does |
|---|---|---|
| `fit_calibration.py` | `data/cal-*.csv` | Fits sensor reading -> surface temperature on the cool-down table; `--write` is disabled (suspect data). |
| `fit_plateaus.py` | `data/<run>.csv` | Fits a plateau calibration run; `--write` updates the firmware calibration. See `RECALIBRATION.md`. |
| `bench/cal_plateaus.py` | | Laptop runner for that calibration (needs the `-Cal` firmware). |
| `bench/run_holds.sh` | | The 2026-11-22 hold run, kept for reference. Unsafe to reuse (ENTER toggles). |
| `plot_cycle.py` | `data/cal-*.csv` | The calibration run over time, raw and fitted, in mV and degC. |
| `plate_model.py` | `data/`, `--runs logs/*.log` | Fits the plate model (heat capacity, heater lag, convection + radiation loss, probe lag) to `out/plate_model.json`. |
| `tune_pid.py` | `out/plate_model.json` | Simulates the firmware's controller on the model and perturbed copies; grid-searches the PID gains; prints model constants for `user_config.h`. |
| `analyze_holds.py` | `logs/*.log` | Scores a closed-loop hold run (rise, overshoot, settling, steady error, duty vs model). |
| `telemetry.py` | | Reads the firmware's console telemetry from a `hil.py` log. |

## Data

**SUSPECT (2026-11-22): during a 240 degC hold the calibration probe jumped to 290 degC, probably by making better contact with the plate, so the calibration, plate model, PID gains and 250/280 degC limits may all be well off on the hot side. Do not heat with this calibration; redo it with a better probe, then refit and retune.**

- `data/cal-warmup.csv`, `data/cal-cooldown.csv`: the 2026-11-22 video
  calibration run, read off the video by the owner (`time_s` from each
  table's start, `display` as shown, `probe_c` from a probe on the plate
  surface). Full power for 3 min 5 s, then 39 min 4 s of cooling.
- `logs/`: `hil.py` logs from the bench (telemetry on, key `t`).

## Bench runs

Sensor readings are only valid with the bridge on the battery laptop (the
desktop's USB ground corrupts them; see `../mcct-firmware/AGENTS.md`).
Runs go in the laptop's tmux session `mcct` so they show on its screen.
