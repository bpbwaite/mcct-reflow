# Notes for agents

Everything agent-facing for this repo is in this file; README.md is the
human-facing guide (use, installation, flashing). Don't add other
agent or readme files.

Don't edit the readme, but DO suggest additions when new features are added or todo-list items are completed.

Paths in Firmware and Design decisions are relative to `mcct-firmware/`, in
Programmer tools to `tools/` and in Tuning to `mcct-tuning/`, unless they start
with a top-level folder. Commands run from the repo root if they name one.

# Firmware (`mcct-firmware/`)

## Ground rules
- All firmware options live in `config/user_config.h`; chip CONFIG bytes in
  `config/chip_config.json`. Don't scatter tunables into modules.
- Logic modules never include `board/pins.h` / `numicro_8051.h`: they must
  link into the simulator tests. Hardware access goes through the small
  driver APIs (`board.h`, `display.h`, `adc.h`, `flash.h`, `tick.h`), which
  `test/fakes.c` replaces. `build.ps1 $HardwareSources` lists the driver files.
- Run `.\build.ps1 -Test` (from `mcct-firmware`) after changes (70 s). Then `.\build.ps1` and watch
  the size line: the product build must leave headroom, and `-Sim` must fit.

## Toolchain
- Top priority: Track local patches to the OpenNuvoton library BSP. The BSP is
  the `tools/MS51_BSP` git submodule and is currently used **unmodified**; if
  it ever needs changes, keep them as `tools/ms51-bsp-local.patch` (exported
  with `git -C tools/MS51_BSP diff`) and list them here, the same way the
  bridge's patch works (see Programmer tools). The firmware only uses the
  BSP headers (SFR names); no BSP functions are linked.
- SDCC 4.6.0 lives in `tools/sdcc` (it is not on PATH and not in the registry). `bin/cc1` had to be
  renamed `cc1.exe`, and `sdcpp` only finds it via PATH, so `build.ps1`
  prepends `tools/sdcc/bin` to PATH for its own process.
- Nuvoton headers test `__SDCC__`, which SDCC doesn't define (it uses `__SDCC`):
  the build passes `-D__SDCC__`.
- Python 3 in `tools/venv` runs the generators (standard library only).

## build.ps1
- Steps: generate `build/generated/calibration_gen.h` and `profiles_gen.c`
  (rewritten only when their content changes) -> compile `src/**/*.c`
  incrementally (an object is rebuilt if its source, any header, or the
  compiler flags changed) -> link -> refuse `__xdata` initialisers (see below)
  -> hex/bin -> size report.
- Small memory model; code limited to 0x3F00: the last two 128-byte APROM
  pages (0x3F00-0x3FFF) are the settings store (`SETTINGS_FLASH_BASE`, passed
  in as a define). 256 B IRAM, 1 KB XRAM.
- `-Sim` adds `-DCFG_PLANT_SIM=2` (bench build that boots simulated).
- `-Test` links every non-hardware module with `test/*.c` for a plain 8052
  (64 KB code) and runs it in `ucsim_51` with the simulator interface at XRAM
  0xFFFF (print + stop). ucsim must get its own hidden console
  (`Start-Process -WindowStyle Hidden`): attached to ours it runs ~8x slower.
- Windows PowerShell 5.1 turns native stderr into terminating errors under
  `$ErrorActionPreference = 'Stop'`; `Invoke-Tool` / `Invoke-Python` /
  `Invoke-Isp` relax it. PowerShell variables are case-insensitive: `$sim`
  *is* the `-Sim` switch (that broke once).
- `-Flash` writes APROM **and** CONFIG (`nuvoispy -c config/chip_config.json`).
  Flashing replaces the whole image, so expect settings to reset to defaults.

## 8051 / SDCC rules learned the hard way
- **No initialised `__xdata` variables.** SDCC's startup copies them with
  `MOVX @R0` paged by P2 (`__XPAGE` = 0xA0). The MS51 doesn't page `MOVX @Ri`
  through P2, so the copy lands in page 0 and the variable starts as garbage.
  ucsim emulates a classic 8051, so tests pass anyway. Symptom seen:
  simulated plate at -2349 degC. Initialise in code; `build.ps1` fails the
  build if the map shows `l_XINIT` != 0. (Zero-initialised xdata is fine: the
  XRAM clear uses `MOVX @DPTR`.)
- **Internal RAM (DATA, 0x08-0x7F) is the scarcest resource.** SDCC only
  overlays the locals of leaf functions, and any 16/32-bit multiply/divide is
  a library call, so locals and 2nd+ parameters of most functions are
  permanent. Keep module state in `__xdata` structs, prefer single-instance
  modules over pointer parameters (generic pointers also cost `__gptrget`),
  avoid extra parameters (logic reads `clock_ms()` instead of taking `now`),
  use 16-bit math where ranges allow. ISR-only buffers sit in `__idata`.
  The linker error is "Could not get N consecutive bytes in internal RAM".
- **Code size.** Small model is the smallest (medium +9 %, large +21 %).
  Unsigned division avoids pulling in the signed routines; one shared
  render frame instead of frame pointers saved ~0.5 KB. Check the size line
  and `build/obj/*.lst` for the biggest functions when it gets tight.
- **TA-protected SFRs**: the write must directly follow `TA=0xAA; TA=0x55`.
  Use `TA_WRITE` / `TA_SET_BITS` / `TA_CLR_BITS` (`board/pins.h`), which
  compute the value first. The firmware never selects SFR page 1, so the ISR
  can touch PxM registers without saving SFRS.
- Functions called from the ISR (`display_scan`, `adc_scan`) have no locals,
  no parameters and no library arithmetic.
- `printf_tiny` can't print longs; the console has its own `console_put_int`.

## Hardware (MS51FB9AE, TSSOP-20, 5 V board)
- Pin map with package numbers: `src/board/pins.h`. Package pin 3 = P0.7 =
  ADC_CH2 (BSP names AINx by ADC channel, not package pin).
- ICP / debug console share pins: pin 8 P1.6 = ICPDA = UART1_TXD (Uno D11),
  pin 18 P0.2 = ICPCK = UART1_RXD (Uno D12), pin 4 P2.0 = nRESET (Uno D13).
- Segment cathodes are quasi-bidirectional (buttons share F/B), anodes and
  the heater push-pull. Pins are input-only from reset until `board_init()`.
- CONFIG as flashed: `FF FF DF FF 5F` (unlocked, reset pin on, BOD 3.7 V reset
  + IAP inhibit, WDT on). Ignore nuvoispy's "WDT is Disabled" (decision 18).
- Sensor: thermocouple + cold-junction-compensated amplifier, powered from
  the MCU's (isolated) 5 V supply. Room temperature reads well under 1 V,
  so there is no low-rail fault (CFG_SENSOR_MIN_PERMILLE 0).
- Don't trust sensor readings taken with the bridge grounded to the
  plate; use an isolated link (USB isolator / battery laptop) or none.
- Sensor calibration firmware: `./build.ps1 -Cal` (src/cal/): holds a sensor
  voltage, no temperature logic; tests `-Cal -Test` (own image: the main one
  has no internal RAM left). Procedure: Tuning, Calibration run.
  Hardware checks: ENTER and Ctrl-C stop the hold, and a frozen
  runner trips the host-lost timeout.
- Unplugging the Uno's USB pulls D13 (nRESET) low: the chip resets, the display
  goes dark and the board's pull-down turns the heater off (owner-checked).
- Telemetry `ms` is a 16-bit clock (wraps every 65.5 s); `mv` is mV of a
  nominal 5.000 V. The `hil.py` logs and the tuning tools are in
  `mcct-tuning/` (see Tuning).

## Debug console (src/debug)
- UART1 at `CFG_CONSOLE_BAUD` (38400) = `PASSTHROUGH_BAUD` in the bridge.
  Above ~57600 the Uno's SoftwareSerial can't reliably receive.
- Keys `u d e E` press UP/DOWN/ENTER/SET, `t` telemetry, `s` settings;
  bench builds only: `S` simulated plate, `W` hang. Full list and telemetry
  columns: `src/debug/cli.h`.
- `scripts/hil.py` scripts and logs a session:
  `..\tools\venv\Scripts\python.exe scripts\hil.py COM8 "wait:2 key:t key:e wait:240" --log run.csv`
- Opening the port restarts the chip.
- Only one program can hold the COM port: close the monitor before flashing.

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
- **Calibration.** Least-squares polynomial in u = V/VDD, fitted at build time,
  evaluated by Horner's rule in 32-bit fixed point: no lookup table, ~200 bytes
  of code. The tests check that the chip evaluates it exactly as fitted.
- **Safety.** Independent of the controller, every 250 ms: sensor rails,
  over-temperature, heating timeout, stuck heater, missing samples. Plus a
  dead-man on the heater output (off if not refreshed for 1 s), the hardware
  watchdog (enabled in CONFIG, verified at boot), and a watchdog reset at
  boot is itself fatal.

## Size

Product build: about 15.2 KB of 15.75 KB code (the last 256 bytes of flash
hold the settings), ~120 B of internal RAM plus 117 B stack, 260 B XRAM.
`-Sim` bench build: about 16.0 KB.

## Status (2026-11-23)

- 2574 unit checks pass in ucsim (math, display, buttons, heater PDM, PID,
  controller, reflow, safety, settings store incl. torn writes, UI flows,
  closed loop and auto-tune against the simulated plate).
- Sensor calibrated with a new probe (decisions 7-9); plate model refitted and
  PID retuned offline (decisions 10-12a). The simulator (`plant_sim.c`) copies
  the fitted plate. The new constants have not run closed-loop on the plate.
- HIL bench: the bridge runs on the battery laptop (see Hardware, ground
  loop). Verified on hardware (first product build): display, buttons, sensor,
  heater, settings persistence across power cycles.
- Planned: checksums on the serial telemetry and the command keys. The link
  corrupts single characters (one flipped state digit stopped a run).

# Design decisions to review

Decisions made without information, roughly in order of how much they matter.
Each says where to change it.

## Hardware

1. **Package pins map to these MS51FB9AE ports** (TSSOP-20 datasheet):
   1 P0.5, 2 P0.6, 3 P0.7, 5 P3.0, 6 P1.7, 10 P1.5, 11 P1.4, 12 P1.3, 13 P1.2, 14 P1.1,
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
   high-impedance from reset until `board_init()` drives P0.3 low. The
   board has a pull-down that keeps the heater off while the pin floats
   (owner, 2026-11-23).
5. **Mains is 60 Hz** (`CFG_MAINS_HZ`) and the optotriac is zero-crossing.
6. **The ADC reference is VDD (5 V)** and the sensor network's output is
   ratiometric to it. Calibrating in place absorbs the actual VDD.

## Sensor and calibration

7. **Calibration**
   Readings are ratiometric, so the CSV keeps a nominal `vdd_v = 5.0`.
8. **Sensor fault only near the top rail** (99.5 % of VDD); see 16b for why
   there is no low-rail fault.
9. **Straight line (degree 1)**, T = 0.2038 * mV - 110.0: rms 1.0 degC,
   worst 2.3. Quadratic and cubic fit no better.

## Control

10. **PID gains are standard (ISA) form, tuned offline: Kp 3.0 %/degC,
    Ti 800 s, Td 8 s.** `mcct-tuning/tune_pid.py` runs a copy of the firmware's
    controller against the fitted plate (`plate_model.py`: 152 J/K, convection
    + radiation loss) and against copies 15-25 % off (heat capacity, loss,
    mains power) and with the lag tripled. Worst simulated overshoot: about
    1.8 degC over all holds and reflow profiles. A slightly better
    score (Kp 3, Ti 500, Td 4) was passed over for its 2.3 degC overshoot. The
    slow integral is deliberate: the feed-forward does most of the work.
    The auto-tuner's Ziegler-Nichols rule gives far hotter gains (Kp 9.3, Ti 11
    on the simulated plate); it still holds +/-1 degC there.
11. **The derivative acts on the error, not on the measurement.** Every setpoint
    is a ramp here, so there is no derivative kick, and it avoids D fighting P
    during warm-up (which wound up the integral and overshot 2.5 degC on the
    simulated plate; now 0.1-0.5 degC).
12. **Thermal model defaults LOSS 0.064 %/degC, CAP 43.4 %·s/degC** from the
    fitted plate (152 J/K; 15 W to hold 100 degC, 55 W for 250 degC; LOSS is
    the best straight line over 100..250 degC). Losses vary about 10 % between
    days (the 2026-11-22 hold log needed less power at the same readings). The
    auto-tuner measures both.
12a. **Reflow ramps drop their slope feed-forward 11 s before the ramp
    ends** (`CFG_MODEL_LAG_S`). The lag was never measured on its own (the 2026-11-23
    runs were for plateaus): logged steps suggest about 4 s, the first fit gave
    11 s. 11 s is kept because the tuner's test set includes a long-lag plate,
    which overshoots up to 8 degC without the lead.
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
    (`CFG_TARGET_MAX_C`) after the plate smelled at an indicated 300+ degC in the first
    calibration run (2026-11-22; about 385 degC on the new scale, extrapolated). The fault sits 30 degC above that, and
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
    bug in the tool (no FATAL 7 at boot, and the `W` hang test proves it
    resets).
19. **Brown-out reset at 3.7 V** with flash writes inhibited below it
    (`config/chip_config.json`).

## User interface

20. **SET (pin 12, P1.3) opens the menu and goes back.**
    Holding ENTER for 1 s can do the same: `CFG_ENTER_HOLD_IS_SET`, off by
    default (owner). Otherwise ENTER acts on release, however long it was held.
    ENTER starts/stops heating. In the menu: UP/DOWN browse, ENTER opens/confirms; the menu closes itself
    after 20 s.
21. **Menu labels:** `rFL` reflow (choose a profile by its label, ENTER starts
    it), `tun` auto-tune (choose the temperature), `P` `I` `d` PID values
    (editable, not just viewable), `OFS` offset (+/- 25 degC), `rSt` factory
    reset (`no`/`yES`).
22. **After UP/DOWN the target shows solid for 2 s** (`CFG_PREVIEW_MS`);
    then standby flashes it again, and heating shows the plate
    temperature again.
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
    see MS51-only hardware differences: the one that already bit is
    XDATA initialisers (8051 / SDCC rules).

# Programmer tools (`tools/`)

`NuMicro-8051-prog/` is a git submodule of https://github.com/nikitalita/NuMicro-8051-prog
pinned at commit 1093ddf, **with local changes applied in its working tree**
from `nuvo51icp-local.patch` (in `tools/`). `.gitmodules` sets
`ignore = dirty` for it, so those changes don't show in the parent repo's
`git status`. The patch file is the tracked copy: after any change inside the
submodule, **regenerate it** (`git -C tools/NuMicro-8051-prog diff > tools/nuvo51icp-local.patch`).
Don't `pull`, `reset`, `checkout` or `stash` in the submodule without doing that
first. Every local change is marked with a `LOCAL PATCH` comment.

`MS51_BSP/` is a git submodule of https://github.com/OpenNuvoton/MS51_BSP,
used unmodified by the firmware (see Toolchain).

## Local patches

### nuvo51icp/nuvo51icp.ino (Arduino Uno ISP-to-ICP bridge)
- **Packet timeout per byte**: a partial packet times out 500 ms after the last
  *byte* (`last_byte_time`), not the last full packet. Without this the bridge
  never answered the first CMD_CONNECT after >500 ms uptime.
- **UART passthrough**: while no ISP session is active the bridge releases
  nRESET (D13) and bridges USB <-> the target's UART1 with SoftwareSerial on the
  ICP pins (D11 = ICPDA = P1.6 = UART1_TXD, D12 = ICPCK = P0.2 = UART1_RXD),
  at `PASSTHROUGH_BAUD` (default 38400). The start of a CMD_CONNECT packet
  (`AE 00 00 00`) switches to ISP/ICP; CMD_RUN_APROM/RESET (sent by nuvoispy on
  exit), a half-received connect packet, or `CONNECTION_TIMEOUT` switch back.
  A partial signature older than 20 ms is forwarded to the target as data.
- `CONNECTION_TIMEOUT` changed from 0 (disabled) to 10000 ms.
- **Connect LED disabled on the Uno** (`CONNECT_LED_ENABLED`): `LED_BUILTIN`
  is D13 = nRESET, and upstream's `enable_connect_led()` right after ICP entry
  drove reset high, kicking the target out of ICP. Symptom: nuvoispy "Connected!"
  then "Device not found" (device ID 0) on every attempt, even with reset
  enabled. Fixed.
- **Boot reset pulse**: `setup()` holds nRESET low 20 ms then starts
  passthrough, so the target restarts once the bridge is listening (otherwise
  its boot banner is printed while the Uno is still in its bootloader).

### nuvoprogpy/nuvoispy/nuvoispy.py
- CONNECT retry waits raised to 0.5 s (`FAST_WAIT`/`SLOW_WAIT`): the bridge
  blocks ~270 ms answering CMD_CONNECT, and faster resends overflowed the Uno's
  serial buffer.

## Why passthrough uses SoftwareSerial (design notes)
- The **target** uses its hardware UART1, which the MS51 maps onto the ICP pins.
  Only the **Uno's** end is software: the ATmega328's single hardware UART
  (D0/D1) is hard-wired to the on-board USB-serial chip (16U2), so it is the
  only route to the PC and can't also face the target. Moving SoftwareSerial to
  the USB side isn't possible (D0/D1 are tied to the 16U2) and would make ISP
  at 115200 unreliable anyway.
- Rates: USB side is always 115200 (`Serial.begin`). SoftwareSerial TX is fine
  at 115200, but RX (target -> Uno) is only reliable to ~57600 on a 16 MHz Uno,
  hence 38400. MS51 UART1 at 24 MHz gives 38400 / 57600 with ~0.16 % error.
- SoftwareSerial disables interrupts per byte, so bulk pastes (PC -> target)
  can overrun the USB RX; the 256-byte `SERIAL_RX_BUFFER_SIZE` mitigates this.
  Typing and debug output are fine. It is also half-duplex in practice.
- Options if more speed is ever needed: AltSoftSerial (timer-based, reliable,
  but fixed to D8 RX / D9 TX -> two extra jumpers to the target's P1.6/P0.2 in
  parallel with D11/D12), or a separate USB-serial adapter on P1.6/P0.2
  (must be idle or unplugged while flashing).
- Opening the COM port resets the Uno (DTR); optiboot blinks D13 = nRESET, so
  the target restarts too. Useful for seeing boot output.
- On the Uno, `BUILTIN_LED` is D13 = nRESET (see the connect-LED patch above).

## Troubleshooting "Device not found"
1. Upload the read-only probe with `.\tools\bridge.ps1 -Probe` and open COM8
   at 115200. It holds reset low
   and loops ICP entry; if the reset pin is disabled, power-cycle the target
   while it runs. It prints DEVICE_ID / CID / CONFIG. Then re-upload the bridge
   (`.\tools\bridge.ps1`).
2. CONFIG0 bit 2 (RPD) = 1 means reset works and no power cycle is needed;
   bit 1 (LOCK) = 0 means locked (CID 0xFF, flash reads FF). Writing a locked
   chip makes the bridge mass-erase it, which also resets CONFIG to FF (unlocked).
3. The first MS51FB9AE (device ID 0x0B004B21) arrived locked, CONFIG `FD FF FF FF FF`.

## Testing the bridge without a target
`bridge_test.py` (from `tools\`: `venv\Scripts\python.exe -I bridge_test.py COM8`)
assumes no target is attached (a running target printing on UART1 would make
the "no reply in passthrough" checks fail). It talks raw ISP packets in one port session and checks: text in passthrough gets
no ISP reply, data containing 0xAE doesn't trigger ISP, CONNECT/GET_FWVER(0xE0)/
RUN_APROM work, passthrough resumes after RUN_APROM, a second session works,
and a half-sent connect recovers. Host packets must use sequence numbers
0, 2, 4, ... (the bridge checks them). `mcct-firmware\build.ps1 -Status`
with no chip should end in "Device not found" within ~2 s.

## Building / uploading the bridge
Use `bridge.ps1` (wraps the local arduino-cli). It must be compiled with
`-DCACHED_ROM_READ=0 -DSERIAL_RX_BUFFER_SIZE=256` (the 64 KB read cache can't
fit in the Uno's 2 KB RAM); the script passes these plus `PASSTHROUGH_BAUD`.
The passthrough baud must match `CFG_CONSOLE_BAUD` in `mcct-firmware/config/user_config.h`.

## Related
- `nuvoispy.ps1` runs nuvoispy from `venv`.
- `icp_probe/` is a separate read-only power-cycle probe sketch. `bridge.ps1
  -Probe` copies its ICP library files in from the submodule (they are
  git-ignored). Uploading it replaces the bridge on the Uno.

# Tuning (`mcct-tuning/`)

Offline analysis for the MCCT hot plate: sensor calibration, a thermal
model of the plate, and PID tuning against it. Python 3 with numpy, scipy
and matplotlib (`requirements.txt`, installed in `tools/venv`). Run from
`mcct-tuning/`, e.g. `..\tools\venv\Scripts\python.exe fit_calibration.py`.
Plots go to `out/` (not tracked).

| Script | Input | Does |
|---|---|---|
| `fit_calibration.py` | `data/cal-*.csv` | The 2026-11-22 video fit, kept for its plot and helpers; `--write` is disabled (poor probe contact). |
| `fit_plateaus.py` | `data/<run>.csv` | Fits a plateau calibration run; `--write` updates the firmware calibration. |
| `bench/cal_plateaus.py` | | Laptop runner for that calibration (needs the `-Cal` firmware). |
| `bench/run_holds.sh` | | The 2026-11-22 hold run, kept for reference. Unsafe to reuse (ENTER toggles). |
| `plot_cycle.py` | `data/cal-*.csv` | The calibration run over time, raw and fitted, in mV and degC. |
| `plate_model.py` | `data/`, `--runs` / `--check logs/*.log` | Fits the plate model (heat capacity, two heater lags, convection + radiation loss) to the video run's sensor and the plateau logs: `out/plate_model.json`. |
| `tune_pid.py` | `out/plate_model.json` | Simulates the firmware's controller on the model and perturbed copies; grid-searches the PID gains; prints model constants for `user_config.h`. |
| `analyze_holds.py` | `logs/*.log` | Scores a closed-loop hold run (rise, overshoot, settling, steady error, duty vs model). |
| `telemetry.py` | | Reads the firmware's console telemetry from a `hil.py` log. |

## Bench runs

Sensor readings are only valid with the bridge on the battery laptop (the
desktop's USB ground corrupts them; see Hardware).

## Calibration run

Heater (mains) power off while flashing; laptop on battery. From `mcct-firmware`:
`.\build.ps1 -Cal -Test`, `.\build.ps1 -Cal`, then scp `build/mcct-cal.bin` and
`config/chip_config.json` to `user@laptop:mcct/fw/` and
`mcct-tuning/bench/cal_plateaus.py` to `mcct/`. On the laptop:
`./nuvoispy -p /dev/ttyACM0 -c fw/chip_config.json -w fw/mcct-cal.bin`.

1. With someone at the plate, check the shutoffs: ENTER, Ctrl-C ("Chip confirms
   heater off"), a paused runner (host lost), USB pull. Any failure: cut power.
2. `venv/bin/python cal_plateaus.py /dev/ttyACM0 plateaus-N.csv --max-probe 240`
   (add `--max-mv` for a reading cutoff). Type each probe reading once it is
   steady; under 15 min per plateau. Don't touch the probe: a jump at a steady
   mV means its contact changed, so start over.
3. Repeat a few plateaus on the way down into a second file.
4. `fit_plateaus.py` on the CSVs, review the plot, `--write`, rebuild, test;
   then `plate_model.py` and `tune_pid.py`, and copy the constants over.
