# MCCT: 350 W soldering hot plate controller

Constant-temperature and reflow controller for a hot plate, built on a Nuvoton
MS51FB9AE, with a 3-digit 7-segment display, three buttons and a zero-cross
optotriac heater switch. Firmware is in `mcct-firmware/`, the board in
`mcct-pcb/`, calibration and tuning in `mcct-tuning/`, programmer tools in
`tools/`. Code internals and design decisions: AGENTS.md.

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

## Install and first flash

Toolchain for the MS51FB9AE firmware in `mcct-firmware/`, flashed over ICP with
an Arduino Uno. Everything lives under `tools/`; nothing is added to PATH.
All commands run from the repo root in PowerShell, except in step 6.

### Before you start
- Windows with PowerShell, **git**, **Python 3.9+**, and **7-Zip**
  (`C:\Program Files\7-Zip\7z.exe`).
- Run `git config --global core.longpaths true` before cloning: the Nuvoton BSP
  submodule has paths over 150 characters, which breaks checkout under
  Windows' 260-character limit unless the repo sits in a very short folder.
- **Arduino Uno** (16U2 USB, e.g. R3) and jumper wires.
- Downloads, saved anywhere:
  - SDCC **4.6.0** Windows x64 installer (`sdcc-4.6.0-x64-setup.exe`) from sourceforge.net/projects/sdcc
  - **arduino-cli** Windows 64-bit zip from arduino.github.io/arduino-cli
- (Optional) VS Code with the C/C++ extension; open `mcct-firmware/` as the folder.

### 1. SDCC
Extract the installer (no install, no PATH change), then fix one file name:
```powershell
& "C:\Program Files\7-Zip\7z.exe" x path\to\sdcc-4.6.0-x64-setup.exe -otools\sdcc
Rename-Item tools\sdcc\bin\cc1 cc1.exe
Remove-Item -Recurse 'tools\sdcc\$PLUGINSDIR', 'tools\sdcc\uninstall.exe'
```
(Running the installer with `tools\sdcc` as the destination and PATH unticked
also works.)

### 2. Submodules and programmer software
The Nuvoton BSP and the ICP programmer are git submodules (clone this repo with
`--recurse-submodules`, or run the first line). The patch adds UART
passthrough and Uno fixes to the bridge sketch; see AGENTS.md.
```powershell
git submodule update --init
git -C tools\NuMicro-8051-prog apply ..\nuvo51icp-local.patch
python -m venv tools\venv
tools\venv\Scripts\pip install pyserial pyzmq windows-curses
```

### 3. arduino-cli
Unzip `arduino-cli.exe` into `tools\arduino-cli\`, then:
```powershell
$a = (Resolve-Path tools\arduino-cli).Path -replace '\\','/'
"directories:`n  data: $a/data`n  downloads: $a/staging`n  user: $a/sketchbook" | Set-Content tools\arduino-cli\arduino-cli.yaml
tools\arduino-cli\arduino-cli.exe --config-file tools\arduino-cli\arduino-cli.yaml core update-index
tools\arduino-cli\arduino-cli.exe --config-file tools\arduino-cli\arduino-cli.yaml core install arduino:avr
```
(arduino-cli resolves relative paths against the current folder, so the yaml
uses absolute ones. Re-run the first two lines if you move the project.)

### 4. Wire the Uno to the chip
| Uno | MS51FB9AE (TSSOP-20) |
|-----|----------------------|
| D11 | pin 8  P1.6 (ICPDA / UART1 TX) |
| D12 | pin 18 P0.2 (ICPCK / UART1 RX) |
| D13 | pin 4  P2.0 (nRESET) |
| GND | pin 7  VSS |
| 5V  | pin 9  VDD (or board supply; keep grounds common) |

To use or calibrate the sensor, run the Uno from a laptop on battery: an
earthed PC corrupts the temperature reading (ground loop).

### 5. Load the bridge onto the Uno
Find the Uno's port with `tools\arduino-cli\arduino-cli.exe board list`
(examples use COM8).
```powershell
.\tools\bridge.ps1 -Port COM8
```

### 6. Build, flash, watch
```powershell
cd mcct-firmware
.\build.ps1                         # compile -> build\mcct.bin
.\build.ps1 -Test                   # unit tests in SDCC's 8051 simulator (~70 s)
.\build.ps1 -Status -Port COM8      # read chip ID and config
.\build.ps1 -Flash -Monitor -Port COM8
.\build.ps1 -Sim -Flash -Port COM8  # bench build: simulated hot plate, heater held off
```
`-Flash` also writes the CONFIG bytes from `mcct-firmware/config/chip_config.json`
(hardware watchdog on, brown-out reset). `-Monitor` opens the chip's debug
console (38400 on the chip, 115200 on the Uno's port); Ctrl+] quits. Close
it before flashing again. Without a temperature sensor connected, the
product build stops with a sensor fault (`uuu`); use `-Sim` on the bench.
In VS Code the same actions are under Terminal > Run Task.

### Notes
- **Locked chips**: new chips may arrive locked. Flashing mass-erases them
  automatically (old contents are lost) and leaves them unlocked.
- **No power cycle** is needed while the chip's reset pin is enabled (the
  default). If `-Status` says "Device not found", see the troubleshooting
  section in AGENTS.md (`.\tools\bridge.ps1 -Probe` loads a read-only
  probe sketch for diagnosing the chip).
- Opening the Uno's port restarts the chip.
- Nuvoton's ~50 SDCC sample programs are in the BSP submodule under
  `tools/MS51_BSP/MS51FB9AE_MS51XB9AE_MS51XB9BE/SampleCode`.

## Configuration

- `mcct-firmware/config/user_config.h`: **every** firmware option: temperature limits,
  safety thresholds, timings, PID and model defaults, button feel, debug.
- `mcct-firmware/config/chip_config.json`: the MS51 CONFIG bytes (lock, hardware watchdog,
  brown-out), written on every `-Flash`.
- `mcct-firmware/profiles/*.json`: up to three reflow profiles, validated and compiled in.
- `mcct-firmware/calibration/adc_to_temperature.csv`: sensor calibration points; a
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

## Calibrating the sensor

Follow "Calibration run" in AGENTS.md: the calibration firmware holds steady
sensor voltages, you enter the reading of a calibrated probe at each one, and
the fitted result goes into `mcct-firmware/calibration/adc_to_temperature.csv`.
Rebuild and run the tests afterwards; the build prints the fit residual.
