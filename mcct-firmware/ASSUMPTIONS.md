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

7. **The calibration CSV is a placeholder: an ideal 10 mV/degC sensor.**
   The real curve comes from your HIL calibration run (log `mv` from the
   telemetry next to a reference probe). On the bench the pin reads ~0 V, so
   the product build stops with a sensor fault ("uuu", console `FATAL 2`);
   use the `-Sim` bench build until the sensor is connected.
8. **Sensor fault = reading below 0.2 % or above 99.5 % of VDD**
   (`CFG_SENSOR_MIN/MAX_PERMILLE`). Adjust once the real sensor's range is known.
9. **Polynomial degree 3** (set in the CSV header, 1..4). The generator reports
   the residuals and warns if the fit isn't monotonic.

## Control

10. **PID defaults P=29, I=49, D=12 are read as standard (ISA) form:
    Kp = 29 %/degC, Ti = 49 s, Td = 12 s.** Ti/Td of about 4 matches a
    Ziegler-Nichols tune with a ~100 s oscillation period, which is typical of
    a hot plate. If your numbers are parallel-form (Ki, Kd) or a 0..255 output
    scale, they need converting (`CFG_PID_*_DEFAULT`).
11. **The derivative acts on the error, not on the measurement.** Every setpoint
    is a ramp here, so there is no derivative kick, and it avoids D fighting P
    during warm-up (which wound up the integral and overshot 2.5 degC on the
    simulated plate; now 0.1-0.5 degC).
12. **The thermal model's defaults (LOSS 0.130 %/degC, CAP 77.0 %·s/degC)
    assume ~270 J/K of plate and 0.45 W/K of loss.** The auto-tuner measures
    both; on the simulated plate it recovered them to within 1 %.
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
    for 90 s yet +20 degC within 60 s), sensor rails, over-temperature (400
    degC), lost ADC samples, a watchdog reset at boot, and CONFIG bytes that
    don't enable the watchdog. Every one shows "uuu" until power is cycled
    (a reset-pin reset also clears it; the Arduino bridge uses that).
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
