# MCCT firmware (MS51FB9AE)

Toolchain: SDCC 4.6.0, installed locally in `../tools/sdcc` (not on PATH).
Flashing: Arduino Uno running the `nuvo51icp` ISP-to-ICP bridge sketch, driven by
`../tools/nuvoispy.ps1` (wiring: D11 -> ICPDA, D12 -> ICPCK, D13 -> nRESET, GND -> VSS).

```powershell
.\build.ps1                 # build -> build\mcct.hex, build\mcct.bin
.\build.ps1 -Clean          # clean rebuild
.\build.ps1 -Status         # read chip ID/config through the bridge (COM8)
.\build.ps1 -Flash          # build and write APROM (add -Port COMx if needed)
.\build.ps1 -Monitor        # open the chip's UART1 console through the bridge (Ctrl+] quits)
.\build.ps1 -Flash -Monitor # flash, then open the console
```

In VS Code: Ctrl+Shift+B builds; *Terminal > Run Task* has Flash, Flash + Monitor,
Serial Monitor and Status.

## Debug console
UART1 uses the ICP pins (TX = P1.6/ICPDA, RX = P0.2/ICPCK), so the Arduino bridge
also acts as a serial adapter: between flashes it releases reset and passes
USB <-> UART1 through at 38400 baud (`CONSOLE_BAUD` in `inc/console.h` must match
`PASSTHROUGH_BAUD` in the bridge). `printf_tiny()`/`putchar()` go to the console,
`console_getc()` reads without blocking. Flashing switches the bridge to ICP
automatically; the monitor must be closed first since only one program can hold
the COM port. Opening the monitor restarts the chip. Bridge details:
`../tools/AGENTS.md`; rebuild/upload it with `../tools/bridge.ps1`.

## Layout
- `src/` – application sources (every `*.c` is compiled; `main.c` is linked first)
- `inc/` – project headers
- Nuvoton MS51 16K BSP (Apache-2.0): the `../tools/MS51_BSP` git submodule, used
  unmodified from `MS51FB9AE_MS51XB9AE_MS51XB9BE/Library/`
  - `Device/Include/` – register (SFR) names, bit set/clear macros, pin-mode helpers
  - `StdDriver/` – drivers (ADC, UART, timers, I2C, SPI, PWM, IAP/EEPROM, WDT, WKT…)

`#include "numicro_8051.h"` pulls in all of the above. The drivers are built into
`build/obj/ms51bsp.lib`, so only the modules you call end up in the image
(`sdcc_stdio.c` and `putchar.c` are skipped; `src/console.c` replaces them).
Nuvoton's ~50 SDCC sample programs are in
`../tools/MS51_BSP/MS51FB9AE_MS51XB9AE_MS51XB9BE/SampleCode`.

## Current example
`src/main.c` reads package pin 3 (P0.7 / ADC_CH2) every 500 ms and prints the
raw value and an estimated mV (assuming `VDD_MV` = 5000) on the debug console.
Keys: `p` pauses/resumes, `r` reads once, anything else is echoed.

## Notes
- `__SDCC__` is defined by the build because the Nuvoton headers test for it.
- Memory model is small (`$Model` in `build.ps1`); 16 KB APROM (no LDROM),
  256 B IRAM, 1 KB XRAM. The BSP's own SDCC samples use the large model.
