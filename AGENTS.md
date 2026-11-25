# Notes for agents

Everything agent-facing for this repo is in this file; README.md is the
human-facing guide (use, installation, flashing). Don't add other
agent or readme files.

Don't edit the readme, but DO suggest additions when new features are added or todo-list items are completed.

Keep this file short: no machine-specific setup, run history, or numbers that
the code, `user_config.h` or the build output already state.

Paths in Firmware and Design constraints are relative to `mcct-firmware/`, in
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
- After changes run `.\build.ps1 -Test` (from `mcct-firmware`), then
  `.\build.ps1` and watch the size line: the product build must leave headroom,
  and `-Sim` must fit (it is nearly full).

## Toolchain
- Top priority: Track local patches to the OpenNuvoton library BSP. The BSP is
  the `tools/MS51_BSP` git submodule and is currently used **unmodified**; if
  it ever needs changes, keep them as `tools/ms51-bsp-local.patch` (exported
  with `git -C tools/MS51_BSP diff`) and list them here, the same way the
  bridge's patch works (see Programmer tools). The firmware only uses the
  BSP headers (SFR names); no BSP functions are linked.
- SDCC 4.6.0 lives in `tools/sdcc`. `bin/cc1` must be named `cc1.exe`, and
  `sdcpp` only finds it via PATH, so `build.ps1` prepends `tools/sdcc/bin`.
- Nuvoton headers test `__SDCC__`, which SDCC doesn't define (it uses `__SDCC`):
  the build passes `-D__SDCC__`.
- Python runs through uv (`pyproject.toml` / `uv.lock` in the repo root; numpy
  and matplotlib are in the `tuning` group). The generators use only the
  standard library.

## build.ps1
- Steps: generate `build/generated/calibration_gen.h` and `profiles_gen.c`
  (rewritten only when their content changes) -> compile `src/**/*.c`
  incrementally -> link -> refuse `__xdata` initialisers (see below)
  -> hex/bin -> size report.
- Small memory model; code limited to 0x3F00: the last two 128-byte APROM
  pages (0x3F00-0x3FFF) are the settings store (`SETTINGS_FLASH_BASE`).
- `-Sim` adds `-DCFG_PLANT_SIM=2` (bench build that boots simulated, real
  heater output held off). The simulator doesn't fit in the product build.
- `-Cal` builds the calibration firmware (`src/cal/`): it holds a sensor
  voltage, with no temperature logic. Its tests (`-Cal -Test`) are a separate
  image because the main one has no internal RAM left.
- `-Test` links every non-hardware module with `test/*.c` for a plain 8052
  (64 KB code) and runs it in `ucsim_51` with the simulator interface at XRAM
  0xFFFF (print + stop). ucsim must get its own hidden console
  (`Start-Process -WindowStyle Hidden`): attached to ours it runs ~8x slower.
- `-Flash` writes APROM **and** CONFIG, replacing the whole image, so settings
  reset to defaults.
- Windows PowerShell 5.1 turns native stderr into terminating errors under
  `$ErrorActionPreference = 'Stop'`; `Invoke-Tool` / `Invoke-Python` /
  `Invoke-Isp` relax it. PowerShell variables are case-insensitive: `$sim`
  *is* the `-Sim` switch.

## 8051 / SDCC rules
- **No initialised `__xdata` variables (XDATA initialisers).** SDCC's startup
  copies them with `MOVX @R0` paged by P2, which the MS51 doesn't do, so they
  start as garbage. ucsim emulates a classic 8051, so tests pass anyway.
  Initialise in code; the build fails if the map shows `l_XINIT` != 0.
  (Zero-initialised xdata is fine: the XRAM clear uses `MOVX @DPTR`.)
- **Internal RAM (DATA, 0x08-0x7F) is the scarcest resource.** SDCC only
  overlays the locals of leaf functions, and any 16/32-bit multiply/divide is
  a library call, so locals and 2nd+ parameters of most functions are
  permanent. Keep module state in `__xdata` structs, prefer single-instance
  modules over pointer parameters (generic pointers also cost `__gptrget`),
  avoid extra parameters (logic reads `clock_ms()` instead of taking `now`),
  use 16-bit math where ranges allow. ISR-only buffers sit in `__idata`.
  The linker error is "Could not get N consecutive bytes in internal RAM".
- **Code size.** Small model is the smallest. Unsigned division avoids pulling
  in the signed routines; use the one shared render frame, not frame pointers.
  Check `build/obj/*.lst` for the biggest functions when it gets tight.
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
- ICP and the debug console share pins: P1.6 = ICPDA = UART1_TXD (Uno D11),
  P0.2 = ICPCK = UART1_RXD (Uno D12); P2.0 = nRESET (Uno D13).
- Pins are input-only from reset until `board_init()`; a board pull-down keeps
  the heater off while its pin floats. Unplugging the Uno's USB resets the chip
  (D13 goes low), which also turns the heater off.
- Sensor: thermocouple + cold-junction-compensated amplifier, ratiometric to
  the MCU's 5 V. A grounded bridge (e.g. an earthed PC) creates a ground loop
  that corrupts readings: use a laptop on battery, a USB isolator, or no link.
- nuvoispy's summary prints "WDT is Disabled" for this CONFIG; its own bit
  table and the firmware's boot check say enabled. Treat it as a display bug.

## Debug console (src/debug)
- UART1 at `CFG_CONSOLE_BAUD` = `PASSTHROUGH_BAUD` in the bridge. Keys and
  telemetry columns: `src/debug/cli.h`. Telemetry `ms` is a 16-bit clock (wraps
  every 65.5 s); `mv` is mV of a nominal 5.000 V.
- The serial link can corrupt single characters and has no checksum yet.
- Opening the port restarts the chip. Only one program can hold the port:
  close the monitor before flashing.

## How it works

```
src/
  main.c      boot, main loop, fatal handling
  board/      clock, pin map (pins.h), watchdog, reset cause, 2 kHz tick ISR
  cal/        calibration firmware (-Cal)
  display/    segment-multiplexed driver + button sampling, font, rendering
  input/      debounce, long press, accelerating auto-repeat
  sensor/     ADC window averaging (ISR), calibration polynomial
  control/    heater PDM, thermal model, PID, controller, reflow, auto-tune
  safety/     fault supervisor
  storage/    flash IAP, wear-levelled settings
  app/        process (what runs) and UI (what you see)
  debug/      UART console, telemetry, simulated hot plate
test/         unit tests (run in ucsim) + fakes for the hardware drivers
scripts/      code generators
```

- **Timing.** Timer2 interrupts at 2 kHz: one display slot, one ADC
  conversion, the millisecond clock. Everything else runs in the main loop:
  one control step per finished ADC window (250 ms = 15 cycles of 60 Hz, so
  mains hum averages out), one UI step per 10 ms.
- **Display.** One *segment* per slot across all three digits, plus a dark
  slot in which the button lines (shared with segments F and B) are read.
- **Control.** setpoint ramp (model-limited, eased into the target) ->
  feed-forward from the thermal model (power to hold + power to ramp) ->
  PID on the residual (derivative on filtered error, conditional
  anti-windup) -> heater duty -> sigma-delta pulse-density modulation in
  5-cycle quanta for the zero-cross optotriac.
- **Thermal model.** `power = LOSS*(T - ambient) + CAP*dT/dt`. It sets how fast
  the setpoint may ramp and supplies the feed-forward; the auto-tuner measures
  LOSS and CAP. A richer model only has to replace `control/thermal_model.c`.
- **Calibration.** Least-squares polynomial in u = V/VDD, fitted at build time,
  evaluated by Horner's rule in 32-bit fixed point. The tests check that the
  chip evaluates it exactly as fitted.
- **Safety.** Independent of the controller, every 250 ms: sensor top rail,
  over-temperature, heating timeout, stuck heater, missing samples. Plus a
  dead-man on the heater output, the hardware watchdog (enabled in CONFIG,
  verified at boot), and a watchdog reset at boot is itself fatal.

# Design constraints

Values are in `config/user_config.h`; these are the reasons not to change
things casually.

- **Segment-at-a-time display:** the resistors are on the anodes, so this gives
  every LED its own resistor and even brightness. A segment cathode sinks up to
  3 LED currents.
- **LEDs run at full brightness**, with no dimming. This may change in the
  future (e.g. PWM on the common anodes).
- **Quasi-bidirectional segment cathodes:** UP/DOWN pull segment lines F/B to
  ground, so a pressed button must not fight a push-pull high. Anodes and the
  heater are push-pull; the heater is active high and starts low.
- **Mains:** the code assumes a zero-crossing optotriac and `CFG_MAINS_HZ`.
- **Calibration CSV keeps a nominal `vdd_v = 5.0`**, because readings are
  ratiometric. Calibrating in place absorbs the actual VDD.
- **No low-rail sensor fault:** 0 V can't be told apart from a cold plate.
  Only a reading near VDD is a fault.
- **Derivative acts on the error, not the measurement:** every setpoint is a
  ramp, so there is no kick, and D on the measurement fights P during warm-up
  and winds up the integral.
- **Slow integral is deliberate:** the feed-forward does most of the work. The
  auto-tuner's Ziegler-Nichols gains are much hotter than the defaults.
- **Reflow ramps drop their slope feed-forward early (`CFG_MODEL_LAG_S`).** The
  lag was never measured on its own; the long value avoids overshoot on
  slow plates.
- **Constant-temperature mode ramps and eases into the target**, trading a
  slower approach for no overshoot.
- **Over-temperature ordering:** auto-tune's overshoot abort must trip before
  the over-temperature fault, which sits above `CFG_TARGET_MAX_C`. A static
  assert keeps it.
- **Faults latch "uuu"** until power is cycled (or the reset pin is pulsed).
- **Idle timeout only applies above `CFG_IDLE_TIMEOUT_C`**: after
  `CFG_IDLE_TIMEOUT_S` without a button press it turns the heater off. The next
  press returns to standby; it never resumes heating. It covers standby,
  heating and the menu. Reflow and auto-tune are exempt: they have their own
  time limits.
- **Watchdog is enabled in CONFIG** so software can't turn it off; brown-out
  resets and blocks flash writes.
- **Settings are saved after a delay**, wear-levelled over the two settings
  pages.
- **Reflow waypoints wait until the plate gets there**; cooling is passive.
- **Unit tests run in ucsim** with the firmware's compiler and memory model.
  They can't see MS51-only hardware differences.

# Programmer tools (`tools/`)

`NuMicro-8051-prog/` is a git submodule of https://github.com/nikitalita/NuMicro-8051-prog
pinned at commit 1093ddf, **with local changes applied in its working tree**
from `nuvo51icp-local.patch` (in `tools/`). `.gitmodules` sets
`ignore = dirty` for it, so those changes don't show in the parent repo's
`git status`. The patch file is the tracked copy: after any change inside the
submodule, **regenerate it** (`git -C tools/NuMicro-8051-prog diff > tools/nuvo51icp-local.patch`,
from Git Bash: PowerShell's `>` changes the encoding and line endings).
Don't `pull`, `reset`, `checkout` or `stash` in the submodule without doing that
first. Every local change is marked with a `LOCAL PATCH` comment.

## Local patches

### nuvo51icp/nuvo51icp.ino (Arduino Uno ISP-to-ICP bridge)
- **Packet timeout per byte**: a partial packet times out 500 ms after the last
  *byte* (`last_byte_time`), not the last full packet.
- **UART passthrough**: while no ISP session is active the bridge releases
  nRESET and bridges USB (115200) to the target's UART1 with SoftwareSerial on
  the ICP pins, at `PASSTHROUGH_BAUD`. The start of a CMD_CONNECT packet
  (`AE 00 00 00`) switches to ISP/ICP; CMD_RUN_APROM/RESET (sent by nuvoispy on
  exit), a half-received connect packet, or `CONNECTION_TIMEOUT` switch back.
  A partial signature older than 20 ms is forwarded to the target as data.
- `CONNECTION_TIMEOUT` changed from 0 (disabled) to 10000 ms.
- **Connect LED disabled on the Uno** (`CONNECT_LED_ENABLED`): `LED_BUILTIN`
  is D13 = nRESET, so driving it kicks the target out of ICP.
- **Boot reset pulse**: `setup()` holds nRESET low 20 ms then starts
  passthrough, so the target restarts once the bridge is listening.

### nuvoprogpy/
- Unused `zmq` and `curses` imports removed (`nuvoispy/nuvoispy.py`,
  `config.py`), so it only needs pyserial.
- `nuvoispy/nuvoispy.py`: CONNECT retry waits raised to 0.5 s (`FAST_WAIT`/`SLOW_WAIT`): the bridge
  blocks ~270 ms answering CMD_CONNECT, and faster resends overflow the Uno's
  serial buffer.

## Passthrough limits
- SoftwareSerial is required on the Uno's target side: its only hardware UART
  is wired to USB. Receive is unreliable above ~57600, hence 38400.
- It disables interrupts per byte, so bulk pastes can overrun USB receive. It
  is half-duplex in practice.

## Building / uploading the bridge
Use `bridge.ps1` (wraps the local arduino-cli): it passes the required
`-DCACHED_ROM_READ=0 -DSERIAL_RX_BUFFER_SIZE=256` (the 64 KB read cache can't
fit in the Uno's RAM) and `PASSTHROUGH_BAUD`, which must match
`CFG_CONSOLE_BAUD` in `mcct-firmware/config/user_config.h`.

## Troubleshooting "Device not found"
1. Upload the read-only probe with `.\tools\bridge.ps1 -Probe -Port <port>` and
   open the port at 115200. It holds reset low and loops ICP entry; if the
   reset pin is disabled, power-cycle the target while it runs. It prints
   DEVICE_ID / CID / CONFIG. Uploading it replaces the bridge, so re-upload the
   bridge afterwards.
2. CONFIG0 bit 2 (RPD) = 1 means reset works and no power cycle is needed;
   bit 1 (LOCK) = 0 means locked (CID 0xFF, flash reads FF). Factory chips
   arrive locked; writing one makes the bridge mass-erase it, which also resets
   CONFIG to FF (unlocked).

# Tuning (`mcct-tuning/`)

Sensor calibration against an external probe. Run from `mcct-tuning/`. The
scripts read the hold range from `user_config.h`; results go to `out/` (not
tracked).

| Script | Does |
|---|---|
| `cal_plateaus.py` | Runs the plateau calibration (needs the `-Cal` firmware). |
| `fit_plateaus.py` | Fits its CSVs; `--write` updates the firmware calibration. |

The current calibration, model and PID defaults were fitted offline and have
not yet been run closed-loop on the plate. On other plates, `tun` in the menu
measures the model and PID.

## Calibration run

Keep the heater (mains) power off while flashing, and use a link that avoids
the ground loop (see Hardware). From `mcct-firmware`: `.\build.ps1 -Cal -Test`,
then `.\build.ps1 -Cal -Flash -Port <port>`.

1. With someone at the plate, check the shutoffs: ENTER, Ctrl-C ("Chip confirms
   heater off"), a paused runner (host lost), USB pull. Any failure: cut power.
2. `uv run cal_plateaus.py <port> out/run-1.csv` (`--max-probe` / `--max-mv`
   change where it stops). Type each probe reading once it is steady; under
   15 min per plateau. Don't touch the probe: a jump at a steady mV means its
   contact changed, so start over.
3. Repeat a few plateaus on the way down into a second file
   (`--mv 1500 1200 900`).
4. `uv run --group tuning fit_plateaus.py out/run-1.csv out/run-2.csv`, review
   `out/plateaus.png`, then add `--write`.
5. Rebuild, test and flash the product firmware, then auto-tune.
