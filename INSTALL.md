# Install & first flash

Toolchain for the MS51FB9AE firmware in `mcct-firmware/`, flashed over ICP with
an Arduino Uno. Everything lives under `tools/`; nothing is added to PATH.
All commands run from this directory in PowerShell.

## Before you start
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

## 1. SDCC
Extract the installer (no install, no PATH change), then fix one file name:
```powershell
& "C:\Program Files\7-Zip\7z.exe" x path\to\sdcc-4.6.0-x64-setup.exe -otools\sdcc
Rename-Item tools\sdcc\bin\cc1 cc1.exe
Remove-Item -Recurse 'tools\sdcc\$PLUGINSDIR', 'tools\sdcc\uninstall.exe'
```
(Running the installer with `tools\sdcc` as the destination and PATH unticked
also works.)

## 2. Submodules and programmer software
The Nuvoton BSP and the ICP programmer are git submodules (clone this repo with
`--recurse-submodules`, or run the first line). The patch adds UART
passthrough and Uno fixes to the bridge sketch; see `tools/AGENTS.md`.
```powershell
git submodule update --init
git -C tools\NuMicro-8051-prog apply ..\nuvo51icp-local.patch
python -m venv tools\venv
tools\venv\Scripts\pip install pyserial pyzmq windows-curses
```

## 3. arduino-cli
Unzip `arduino-cli.exe` into `tools\arduino-cli\`, then:
```powershell
$a = (Resolve-Path tools\arduino-cli).Path -replace '\\','/'
"directories:`n  data: $a/data`n  downloads: $a/staging`n  user: $a/sketchbook" | Set-Content tools\arduino-cli\arduino-cli.yaml
tools\arduino-cli\arduino-cli.exe --config-file tools\arduino-cli\arduino-cli.yaml core update-index
tools\arduino-cli\arduino-cli.exe --config-file tools\arduino-cli\arduino-cli.yaml core install arduino:avr
```
(arduino-cli resolves relative paths against the current folder, so the yaml
uses absolute ones. Re-run the first two lines if you move the project.)

## 4. Wire the Uno to the chip
| Uno | MS51FB9AE (TSSOP-20) |
|-----|----------------------|
| D11 | pin 8  P1.6 (ICPDA / UART1 TX) |
| D12 | pin 18 P0.2 (ICPCK / UART1 RX) |
| D13 | pin 4  P2.0 (nRESET) |
| GND | pin 7  VSS |
| 5V  | pin 9  VDD (or board supply; keep grounds common) |

## 5. Load the bridge onto the Uno
Find the Uno's port with `tools\arduino-cli\arduino-cli.exe board list`
(examples use COM8).
```powershell
.\tools\bridge.ps1 -Port COM8
```

## 6. Build, flash, watch
```powershell
cd mcct-firmware
.\build.ps1                         # compile -> build\mcct.bin
.\build.ps1 -Status -Port COM8      # read chip ID and config
.\build.ps1 -Flash -Monitor -Port COM8
```
`-Monitor` opens the chip's debug console (38400 on the chip, 115200 on the
Uno's port); Ctrl+] quits. Close it before flashing again. In VS Code the
same actions are under Terminal > Run Task.

## Notes
- **Locked chips**: new chips may arrive locked. Flashing mass-erases them
  automatically (old contents are lost) and leaves them unlocked.
- **No power cycle** is needed while the chip's reset pin is enabled (the
  default). If `-Status` says "Device not found", see the troubleshooting
  section in `tools/AGENTS.md` (`.\tools\bridge.ps1 -Probe` loads a read-only
  probe sketch for diagnosing the chip).
- Opening the Uno's port restarts the chip.
- Nuvoton's ~50 SDCC sample programs are in the BSP submodule under
  `tools/MS51_BSP/MS51FB9AE_MS51XB9AE_MS51XB9BE/SampleCode`.
- More detail: `mcct-firmware/README.md`, `mcct-firmware/AGENTS.md`, `tools/AGENTS.md`.
