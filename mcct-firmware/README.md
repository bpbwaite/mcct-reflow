# MCCT firmware: 350 W soldering hot plate (Nuvoton MS51FB9AE)

Constant-temperature and reflow controller for a hot plate with a 3-digit
7-segment display, three buttons and a zero-cross optotriac heater switch.
SDCC 4.6, 16 KB flash, 256 B IRAM, 1 KB XRAM. Decisions made without
information are listed in [ASSUMPTIONS.md](ASSUMPTIONS.md) for review.

## Using it

| Screen | What you see | Buttons |
|---|---|---|
| boot | `[=]` | |
| standby | target, flashing | UP/DOWN: target (30..250 degC, held = faster). ENTER: heat. Hold ENTER: menu |
| heating | plate temperature; right decimal point = heater on | UP/DOWN: target (shown for 2 s). ENTER: stop. Hold ENTER: menu |
| menu | `rFL` `tun` `P` `I` `d` `OFS` `rSt` | UP/DOWN: browse. ENTER: open. Hold ENTER: back |
| reflow / auto-tune | plate temperature, decimal points sweep; flashes twice at each reflow waypoint | any button cancels |
| `End` / `Err` | run finished / gave up | any button: standby |
| idle | snake animation: above 120 degC with no input for 10 min, the heater turns off | any button: standby |
| `uuu` flashing | fatal fault, heater off | cycle the power |

Menu: `rFL` pick a profile by its label and ENTER starts it; `tun` pick the
auto-tune temperature and ENTER starts it (results are saved); `P`/`I`/`d`
show and edit Kp (%/degC), Ti and Td (s); `OFS` adds an offset to the
reading; `rSt` restores defaults. Settings are saved 3 s after the last change.

## Build, test, flash

```powershell
.\build.ps1                    # build -> build\mcct.bin (prints flash/RAM use)
.\build.ps1 -Test              # unit tests in the 8051 simulator (~70 s)
.\build.ps1 -Flash             # build, write APROM + CONFIG bytes via the Arduino bridge
.\build.ps1 -Sim -Flash        # bench build: boots into a simulated hot plate
.\build.ps1 -Monitor           # debug console (Ctrl+] quits)
.\build.ps1 -Status            # read the chip ID / CONFIG
```

Toolchain and bridge setup: `../INSTALL.md`, `../tools/AGENTS.md`.

## Configuration

- `config/user_config.h`: **every** firmware option: temperature limits,
  safety thresholds, timings, PID and model defaults, button feel, debug.
- `config/chip_config.json`: the MS51 CONFIG bytes (lock, hardware watchdog,
  brown-out), written on every `-Flash`.
- `profiles/*.json`: up to three reflow profiles, validated and compiled in.
- `calibration/adc_to_temperature.csv`: sensor calibration points; a
  polynomial is fitted at build time.

### Reflow profiles
```json
{ "name": "SAC305", "label": "SAC",
  "waypoints": [ { "temp_c": 150, "ramp_s": 100, "hold_s": 0 }, ... ] }
```
For each waypoint the setpoint ramps linearly to `temp_c` over `ramp_s`, the
controller waits for the plate to get there (display flashes twice), then
holds `hold_s`. `label` is what the display shows (1-3 characters the font
has). A heating waypoint not reached within 180 s aborts with `Err`.

### Calibrating the sensor
1. Bring the plate to a series of steady temperatures across the range
   (constant-temperature mode is fine), with a calibrated surface probe on it.
2. Log the console telemetry (`t`): the `mv` column is the voltage at pin 3.
3. Put the `voltage_v,temperature_c` pairs into
   `calibration/adc_to_temperature.csv` and rebuild. The build prints the fit
   residual; the test suite checks that the chip evaluates the polynomial
   exactly as fitted.

The fit is a least-squares polynomial in u = V/VDD, evaluated by Horner's
rule in 32-bit fixed point: no lookup table, ~200 bytes of code.

## How it works

```
src/
  main.c      boot, main loop, fatal handling
  board/      clock, pin map (pins.h), watchdog, reset cause, 2 kHz tick ISR
  display/    segment-multiplexed driver + button sampling, font, rendering
  input/      debounce, long press, accelerating auto-repeat
  sensor/     ADC window averaging (ISR), calibration polynomial
  control/    heater PDM, thermal model, PID, controller, reflow, auto-tune
  safety/     fault supervisor
  storage/    flash IAP, wear-levelled settings
  app/        process (what runs) and UI (what you see)
  debug/      UART console, telemetry, simulated hot plate
test/         unit tests (run in ucsim) + fakes for the hardware drivers
scripts/      code generators, HIL console driver (hil.py)
```

- **Timing.** Timer2 interrupts at 2 kHz: one display slot, one ADC
  conversion, the millisecond clock. Everything else runs in the main loop:
  one control step per finished ADC window (250 ms = 500 samples = exactly
  15 cycles of 60 Hz, so mains hum averages out), one UI step per 10 ms.
- **Display.** One *segment* per slot across all three digits, plus a dark
  slot in which the button lines (shared with segments F and B) are read:
  222 Hz refresh, equal brightness for every digit.
- **Control.** setpoint ramp (model-limited, eased into the target) ->
  feed-forward from the thermal model (power to hold + power to ramp) ->
  PID on the residual (derivative on filtered error, conditional
  anti-windup) -> heater duty -> sigma-delta pulse-density modulation in
  5-cycle quanta for the zero-cross optotriac.
- **Thermal model.** `power = LOSS*(T - ambient) + CAP*dT/dt`. It sets how fast
  the setpoint may ramp and supplies the feed-forward; the auto-tuner measures
  LOSS and CAP. A richer model only has to replace `control/thermal_model.c`.
- **Safety.** Independent of the controller, every 250 ms: sensor rails,
  over-temperature, heating timeout, stuck heater, missing samples. Plus a
  dead-man on the heater output (off if not refreshed for 1 s), the hardware
  watchdog (enabled in CONFIG, verified at boot), and a watchdog reset at
  boot is itself fatal.

## Debug console and HIL

UART1 on the ICP pins, through the Arduino bridge at 38400 baud. Keys:
`u d e E` press UP/DOWN/ENTER/long ENTER, `t` telemetry
(`ms,mv,pv,sp,duty,heater,mode,p,i,d`, one line per 250 ms), `s` settings,
`S` simulated plate on/off, `W` hang (watchdog test; both bench builds only).
`scripts/hil.py` scripts a session:

```powershell
..\tools\venv\Scripts\python.exe scripts\hil.py COM8 "wait:2 key:t key:e wait:240" --log run.csv
```

## Size

Product build: about 15.2 KB of 15.75 KB code (the last 256 bytes of flash
hold the settings), ~120 B of internal RAM plus 117 B stack, 260 B XRAM.
`-Sim` bench build: about 16.0 KB.
