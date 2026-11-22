# Notes

(Firmware notes. Paths are relative to `mcct-firmware/`. Programmer / Arduino
bridge notes are in `../tools/AGENTS.md`.)

## Toolchain
- Top priority: Track local patches to the OpenNuvoton library BSP. The BSP is
  the `../tools/MS51_BSP` git submodule and is currently used **unmodified**; if
  it ever needs changes, keep them as `../tools/ms51-bsp-local.patch` (exported
  with `git -C ../tools/MS51_BSP diff`) and list them here, the same way the
  bridge's `../tools/nuvo51icp-local.patch` works.
- SDCC 4.6.0 lives in `../tools/sdcc` (extracted from the official NSIS installer
  with 7-Zip, so it is not on PATH and not in the registry). `bin/cc1` had to be
  renamed `cc1.exe`, and `sdcpp` only finds it via PATH, so `build.ps1`
  prepends `../tools/sdcc/bin` to PATH for its own process.
- The BSP's SDCC projects target the SDCC bundled with Nuvoton's NuEclipse; the
  BSP repo itself ships no compiler. It targets an older SDCC: its
  `sdcc_stdio.c` declares `char getchar()`, which SDCC 4.6 rejects, so
  `build.ps1` skips that file (and `putchar.c`, which has no SDCC variant);
  `src/console.c` provides `putchar`/`getchar` instead.
- Nuvoton headers test `__SDCC__`, which SDCC doesn't define (it uses `__SDCC`):
  the build passes `-D__SDCC__`. VS Code IntelliSense defines it too, plus
  dummy macros for `__sfr`, `__sbit`, `__at()`, `__interrupt()` etc.

## build.ps1
- Small memory model; 16 KB APROM (no LDROM), 256 B IRAM, 1 KB XRAM.
- BSP drivers (`../tools/MS51_BSP/MS51FB9AE_MS51XB9AE_MS51XB9BE/Library/StdDriver/src`,
  minus `$BspSkip`) are compiled into `build/obj/ms51bsp.lib` (rebuilt only
  when the BSP Library folder or build.ps1 changes), so only
  referenced modules are linked. Never add the BSP .c files to the link
  directly: `isr.c` defines stub handlers for every interrupt vector.
- Every `src/*.c` is compiled; `main.rel` is linked first.
- Windows PowerShell 5.1 turns native stderr into terminating errors under
  `$ErrorActionPreference = 'Stop'`; `Invoke-Tool` relaxes it and echoes
  output via Write-Host (so function return values stay clean). Keep that
  pattern for any new native tool call.
- `Invoke-Isp` does the same relaxing for nuvoispy, which prints a harmless
  "APROM file size does not match" warning on stderr; without it, redirecting
  stderr (e.g. `*> log`) aborted a flash midway.
- `-Flash` writes APROM via `../tools/nuvoispy.ps1` and the Arduino bridge;
  `-Status` reads chip info; `-Monitor` opens pyserial miniterm on the bridge
  at 115200 (the bridge's USB-side rate).

## Hardware (MS51FB9AE, TSSOP-20, 5 V board)
- Package pin 3 = P0.7 = ADC_CH2 (and UART0_RXD). Note BSP names AINx by ADC
  channel, not package pin (AIN3 is P0.6 = pin 2).
- ICP / debug console share pins: pin 8 P1.6 = ICPDA = UART1_TXD (Uno D11),
  pin 18 P0.2 = ICPCK = UART1_RXD (Uno D12), pin 4 P2.0 = nRESET (Uno D13).
- No LED is connected (LED code was removed from main.c).
- Reset pin is enabled, so ICP entry via the bridge needs no power cycle.

## BSP gotchas
- `Timer0_Delay(fsys, count, unit_us)`: the third argument is a time base in
  **microseconds** (1000 = 1 ms), not the `UNIT_1MS` enum from delay.h. Passing
  `UNIT_1MS` (=3) gave 3 us ticks and flooded the console.

## Status (2026-11-21)
Test firmware flashed and verified on the first chip; console via the bridge
shows the boot banner, readings every 500 ms (pin 3 read ~3.7 V), and the
p/r/echo keys work.

## Debug console (src/console.c, inc/console.h)
- UART1 via BSP `UART_Open(..., UART1_Timer3, ...)`; `console.c` defines
  `putchar`/`getchar` (overriding the BSP's UART0 ones in sdcc_stdio.c) and a
  non-blocking `console_getc()`. `\n` is sent as `\r\n`. Use `printf_tiny`
  (small) rather than `printf`.
- `CONSOLE_BAUD` (38400) must equal `PASSTHROUGH_BAUD` in the Arduino bridge
  (`../tools/bridge.ps1 -Baud N` rebuilds it). Above ~57600 the Uno's
  SoftwareSerial can't reliably receive; the MS51's hardware UART is not the
  limit. Full rationale and bridge behaviour: `../tools/AGENTS.md`.
- Only one program can hold the COM port: close the monitor before flashing.
