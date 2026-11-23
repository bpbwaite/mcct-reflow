# Assumptions to review

Decisions made without information, roughly in order of how much they matter.
Each says where to change it.

## Hardware

1. **Package pins map to these MS51FB9AE ports** (TSSOP-20 datasheet):
   1 P0.5, 2 P0.6, 3 P0.7, 5 P3.0, 6 P1.7, 10 P1.5, 11 P1.4, 13 P1.2, 14 P1.1,
   15 P1.0, 16 P0.0, 17 P0.1, 19 P0.3, 20 P0.4. All in `src/board/pins.h`.
2. **The display is driven one segment at a time, not one digit at a time.**
   The resistors are on the anodes, so lighting one segment across up to three
   digits gives every LED its own resistor and uniform brightness ("1" as
   bright as "8"). Cost: a segment cathode sinks up to 3 LED currents at once
   (1/9 duty). If the anode resistors are small (e.g. < 220 R), check that
   current against the MS51's I/O limits or switch to digit scanning in
   `src/display/display.c`.
3. **UP/DOWN pull their segment line to ground when pressed, possibly without
   a series resistor.** Segment cathodes therefore run in quasi-bidirectional
   mode (strong low, weak high): a pressed button can't fight a push-pull high.
   Side effect: while UP (or DOWN) is held, segment F (or B) may glow faintly
   on digits that are lit. With a 2k series resistor on both buttons (like
   ENTER), push-pull cathodes would remove that; ask and I'll change it.
4. **Heater output is active high, push-pull, and starts low**: the pins are
   high-impedance from reset until `board_init()` drives P0.3 low. An external
   pull-down on the optocoupler input would cover the ~1 ms before that.
5. **Mains is 60 Hz** (`CFG_MAINS_HZ`) and the optotriac is zero-crossing.
6. **The ADC reference is VDD (5 V)** and the sensor network's output is
   ratiometric to it. Calibrating in place absorbs the actual VDD.

## Sensor and calibration

**SUSPECT (2026-11-22): during a 240 degC hold the calibration probe jumped to 290 degC, probably by making better contact with the plate, so the calibration, plate model, PID gains and 250/280 degC limits may all be well off on the hot side. Do not heat with this calibration; redo it with a better probe, then refit and retune.**

7. **Calibration from the 2026-11-22 video run** (`calibration/adc_to_temperature.csv`,
   made by `mcct-tuning/fit_calibration.py`): the owner filmed the display and a
   probe on the plate surface through a full-power heat-up to ~300 degC and a
   39-minute cool-down, and read both into tables. Only the cool-down is
   fitted: it is slow enough that the sensor and the surface agree, while
   during heat-up the sensor runs ahead of the surface (2 degC typically, up
   to 20 degC at full power near 300 degC). Readings are ratiometric, so the
   CSV keeps a nominal `vdd_v = 5.0` (the rail measures 5.04 V).
8. **Sensor fault only near the top rail** (99.5 % of VDD); see 16b for why
   there is no low-rail fault.
9. **Polynomial degree 2.** A cubic fits no better (rms 2.29 vs 2.30 degC);
   the remaining scatter is the probe's (its reading jumps up to 10 degC at a
   steady sensor reading above 250 degC, likely contact), not the sensor's.

## Control

10. **PID gains are standard (ISA) form, tuned offline: Kp 3.0 %/degC,
    Ti 800 s, Td 12 s.** `mcct-tuning/tune_pid.py` runs a copy of the
    firmware's controller against the plate model fitted to the calibration
    run (`plate_model.py`: 184 J/K, heater lag 11.4 s, convection + radiation
    loss) and against copies 15-50 % off (heat capacity, loss, heater lag,
    mains power). Worst case: 1.4 degC overshoot on holds, 2.6 degC at a
    reflow peak. The slow integral is deliberate: the feed-forward does
    most of the work, and a faster I overshoots on the slow heater. The
    spec's P=29 I=49 D=12 (read as ISA) overshot 10 degC on the model.
    The auto-tuner's Ziegler-Nichols rule gives far hotter gains (Kp 11,
    Ti 16 on the simulated plate); it still holds +/-1 degC there, but these
    defaults are the tested ones.
11. **The derivative acts on the error, not on the measurement.** Every setpoint
    is a ramp here, so there is no derivative kick, and it avoids D fighting P
    during warm-up (which wound up the integral and overshot 2.5 degC on the
    simulated plate; now 0.1-0.5 degC).
12. **Thermal model defaults LOSS 0.090 %/degC, CAP 52.6 %·s/degC** from
    the fitted plate (184 J/K; 21 W to hold 100 degC, 79 W for 250 degC;
    LOSS is the best straight line over 100..250 degC). The auto-tuner
    measures both (92 / 534 on the simulated plate, which now copies the
    measured one).
12a. **Reflow ramps drop their slope feed-forward 11 s (one heater lag)
    before the ramp ends** (`CFG_MODEL_LAG_S`). The element keeps delivering
    heat for that long after the duty drops; without the lead the model
    overshot reflow waypoints by up to 7 degC, with it 2.7 degC worst case.
13. **Constant-temperature mode ramps the setpoint** at 85 % of the model's
    best rate and eases into the target with a 30 s exponential
    (`CFG_RAMP_HEADROOM_PERCENT`, `CFG_RAMP_APPROACH_S`). This trades a slower
    final approach for no overshoot.
14. **Heater PDM quantum = 5 mains cycles (83 ms)**, decided per quantum by a
    sigma-delta, so the average power is exact and no pulse is shorter.
15. **Auto-tune: relay method around the chosen temperature, 6 cycles (first 2
    discarded), classic Ziegler-Nichols rule** (`CFG_TUNE_*`). It also learns
    LOSS (always) and CAP (when started at least 40 degC below the target).
    Default tune temperature 200 degC.

## Safety

16. **"Timeout" in the fatal list = heating timeout**: at >= 80 % power the
    plate must gain 3 degC every 60 s. Plus a stuck-heater check (heater off
    for 90 s yet +20 degC within 60 s), a sensor at the top rail,
    over-temperature (280 degC, see 16a), lost ADC samples, a watchdog reset at boot, and CONFIG bytes that
    don't enable the watchdog. Every one shows "uuu" until power is cycled
    (a reset-pin reset also clears it; the Arduino bridge uses that).
16a. **Over-temperature fault at 280 degC, not the spec's 400 degC.** The
    owner capped targets and reflow profiles at 250 degC
    (`CFG_TARGET_MAX_C`) after the plate smelled at 300+ degC in the first
    calibration run (2026-11-22). The fault sits 30 degC above that, and
    auto-tune aborts at 25 degC overshoot (was 40) so it trips first; a
    static assert keeps that ordering. `CFG_FATAL_OVERTEMP_C`.
16b. **No low-rail sensor fault.** The thermocouple amplifier reads only a
    few hundred mV at room temperature, so 0 V can't be told apart from a
    cold plate; only a reading near VDD (open/failed amplifier) is a fault.
    `CFG_SENSOR_MIN_PERMILLE` (0 = off).
17. **The 10-minute idle timeout returns to standby (heater off) on a button
    press; it doesn't resume heating.** It applies to standby, heating and the
    menu. Reflow and auto-tune are exempt (they're time-limited themselves:
    each heating waypoint must be reached within 180 s; auto-tune aborts after
    45 min).
18. **The hardware watchdog is enabled in the CONFIG bytes** (software can't turn
    it off) with a 410 ms timeout, and the firmware refuses to run if those
    bytes don't enable it (`CFG_REQUIRE_HW_WATCHDOG`). nuvoispy's summary
    line prints "WDT is Disabled" for CONFIG4 = 0x5F, but that contradicts its
    own bit table and the firmware's check, so I've treated it as a display
    bug in the tool.
19. **Brown-out reset at 3.7 V** with flash writes inhibited below it
    (`config/chip_config.json`).

## User interface

20. **"Set" is a long press (1 s) of ENTER.** Short ENTER starts/stops heating.
    In the menu: UP/DOWN browse, ENTER opens/confirms, long ENTER goes back;
    the menu closes itself after 20 s.
21. **Menu labels:** `rFL` reflow (choose a profile by its label, ENTER starts
    it), `tun` auto-tune (choose the temperature), `P` `I` `d` PID values
    (editable, not just viewable), `OFS` offset (+/- 25 degC), `rSt` factory
    reset (`no`/`yES`).
22. **While heating, UP/DOWN show the target flashing for 2 s**, then the
    plate temperature again.
23. **The setting changes are saved 3 s after the last change** (wear-levelled
    over 16 slots in 2 flash pages). **Flashing new firmware erases the
    settings** (defaults load).
24. **A finished or failed run shows `End` / `Err`** until a button is pressed.
    Reflow and auto-tune animate the decimal points (instead of the heater DP).
25. **Display: `---` for numbers outside -99..999.**

## Reflow profiles

26. **Three profiles ship: Sn63/Pb37 (`Pb`), SAC305 (`SAC`), Sn42/Bi58 (`bi`),**
    as waypoints {temperature, ramp time, hold time}. Each waypoint waits until
    the plate actually gets there (cooling is passive and may take minutes).
    Edit `profiles/*.json`; the build validates them.

## Tooling

27. **The plant simulator is not in the product build** (it doesn't leave
    enough flash headroom). `.\build.ps1 -Sim` builds a bench image that boots
    into it, with the real heater output held off.
28. **Unit tests run in SDCC's 8051 simulator (ucsim)**, with the same
    compiler and memory model as the firmware: no host C compiler is
    installed, and this catches 8051-specific integer behaviour. They can't
    see MS51-only hardware differences: see AGENTS.md for the one that
    already bit (XDATA initialisers).
