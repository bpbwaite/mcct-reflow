# Notes

(Firmware notes. Paths are relative to `mcct-firmware/`. Programmer / Arduino
bridge notes are in `../tools/AGENTS.md`. User-facing overview: README.md.
Open design questions for the owner: ASSUMPTIONS.md.)

## Ground rules
- All user options live in `config/user_config.h`; chip CONFIG bytes in
  `config/chip_config.json`. Don't scatter tunables into modules.
- Logic modules never include `board/pins.h` / `numicro_8051.h`: they must
  link into the simulator tests. Hardware access goes through the small
  driver APIs (`board.h`, `display.h`, `adc.h`, `flash.h`, `tick.h`), which
  `test/fakes.c` replaces. `build.ps1 $HardwareSources` lists the driver files.
- Run `.\build.ps1 -Test` after changes (70 s). Then `.\build.ps1` and watch
  the size line: the product build must leave headroom, and `-Sim` must fit.

## Toolchain
- Top priority: Track local patches to the OpenNuvoton library BSP. The BSP is
  the `../tools/MS51_BSP` git submodule and is currently used **unmodified**; if
  it ever needs changes, keep them as `../tools/ms51-bsp-local.patch` (exported
  with `git -C ../tools/MS51_BSP diff`) and list them here, the same way the
  bridge's `../tools/nuvo51icp-local.patch` works. The firmware only uses the
  BSP headers (SFR names); no BSP functions are linked any more.
- SDCC 4.6.0 lives in `../tools/sdcc` (extracted from the official NSIS installer
  with 7-Zip, so it is not on PATH and not in the registry). `bin/cc1` had to be
  renamed `cc1.exe`, and `sdcpp` only finds it via PATH, so `build.ps1`
  prepends `../tools/sdcc/bin` to PATH for its own process.
- Nuvoton headers test `__SDCC__`, which SDCC doesn't define (it uses `__SDCC`):
  the build passes `-D__SDCC__`.
- Python 3 in `../tools/venv` runs the generators (standard library only).

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
  Reset pin is enabled, so ICP entry via the bridge needs no power cycle.
- Segment cathodes are quasi-bidirectional (buttons share F/B), anodes and
  the heater push-pull. Pins are input-only from reset until `board_init()`.
- CONFIG as flashed: `FF FF DF FF 5F` (unlocked, reset pin on, BOD 3.7 V reset
  + IAP inhibit, WDT on). nuvoispy prints "WDT is Disabled" for CONFIG4 0x5F;
  its own bit table and the firmware's check (no FATAL 7) say enabled, and
  the `W` hang test proves it resets.
- The ADC pin currently reads ~0 V on the bench (no sensor signal), so the
  product build stops with FATAL 2 (sensor). Use `-Sim` for bench work.

## Debug console (src/debug)
- UART1 at `CFG_CONSOLE_BAUD` (38400) = `PASSTHROUGH_BAUD` in the bridge.
  Above ~57600 the Uno's SoftwareSerial can't reliably receive.
- Keys and telemetry columns: `src/debug/cli.h`. `scripts/hil.py` drives a
  session from a script and logs it. Opening the port restarts the chip.
- Only one program can hold the COM port: close the monitor before flashing.

## Status (2026-11-22)
- Firmware rewritten from scratch; 2571 unit checks pass in ucsim (math,
  display, buttons, heater PDM, PID, controller, reflow, safety, settings
  store incl. torn writes, UI flows, closed loop and auto-tune against the
  simulated plate: +/-0.1 degC steady state, <= 0.5 degC overshoot).
- HIL on the real chip (bench build, simulated plate): boot, console,
  CONFIG check, 150 degC hold (peak 150.1, settles at 150.0), settings
  persist across reset via IAP (a reflash resets them to defaults),
  watchdog reset -> FATAL 6 latched, full Sn63Pb37 profile (peak 220.3 for
  220 degC, then passive cool-down to the last waypoint). The chip is left
  with the `-Sim` bench build.
- Not verified yet (needs eyes or a scope): display segment mapping and
  brightness, physical buttons, heater pin waveform, real sensor.
