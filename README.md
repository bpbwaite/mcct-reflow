# MCCT Reflow

An open alternative firmware for generic "MicroComputer Constant Temperature"-style hot plates that adds solder reflowing curves for common paste types.

## Supported Hardware

Tested on the 350W (110V, 400C) 100mm x 100mm model. (PCB: `SW-1010 V1.96`)
![source: amazon.com](/images/product.png)

## Features

Ported from the original firmware:
- Automatic and manual PID tuning
- Manual temperature offset
- Overtemperature shutdown
- Last config saved to flash

New:
- Improved menu
- Automatic shutdown (10 min timeout)
- Customizable reflow curves for Sn63Pb37, SAC305, and Sn42Bi58 pastes
- Fully configurable before compilation
- Optional calibration routine (requires external probe)

Planned (TODO):
- Checksum for serial communications during calibration
- In-depth reflow testing

## Quick Flash (Default Config)

- ***UNPLUG THE HOTPLATE FIRST*** 
- Wire your programmer to the chip. The MS51FB9AE firmware must be flashed over ICP, and I use an Arduino Uno R3 as a bridge.
![](/images/header_pinout.png)
- The first flash may require power cycling the chip to get it to reset, as the reset pin could be disabled by factory firmware.
- ***DISCONNECT THE PROGRAMMING WIRES BEFORE PLUGGING THE PLATE BACK IN***

## Devleopment Environment Setup

***IMPORTANT INFO***
- Be careful if you power the hot plate over AC. It will be partially disassembled while connected to the programmer. Do not touch the internals of the device at all while it is plugged in.
- Do not connect the +5V wire while the board is powered from its own supply. A common ground connection is fine, and actually required to complete the (optional) calibration.
- While developing, especially reading from or calibrating the sensor, you must use a laptop on battery. An earthed PC will offset the ADC readings significantly, as the thermocouple is earthed and you will create a ground loop.

Software Required:
- Run `git config --global core.longpaths true` because the Nuvoton BSP submodule has paths over 150 characters, which breaks checkout under Windows' 260-character limit.
- Get the Nuvoton BSP and ICP programmer bridge repositories:
```
git submodule update --init --recursive
```
- Have SDCC 4.6.0 installed to `tools\sdcc` (path variables not required)
- arduino-cli unzipped to `tools\arduino-cli\`
- Install the patch that adds UART passthrough.
```
git -C tools\NuMicro-8051-prog apply ..\nuvo51icp-local.patch
```
- Load the bridge onto the Uno (`tools\arduino-cli\arduino-cli.exe board list` to find the port)
```powershell
$a = (Resolve-Path tools\arduino-cli).Path -replace '\\','/'
"directories:`n  data: $a/data`n  downloads: $a/staging`n  user: $a/sketchbook" | Set-Content tools\arduino-cli\arduino-cli.yaml
tools\arduino-cli\arduino-cli.exe --config-file tools\arduino-cli\arduino-cli.yaml core update-index
tools\arduino-cli\arduino-cli.exe --config-file tools\arduino-cli\arduino-cli.yaml core install arduino:avr
```
(arduino-cli resolves relative paths against the current folder, so the yaml uses absolute ones. Re-run the first two lines if you move the project.)

Build and Flash:
```
cd mcct-firmware 
.\build.ps1 -[flags]
```
- `-Status -Port COM...` read chip ID/config
- `-Flash -Port COM...` write firmware + config
- `-Monitor` open serial monitor bridge (use with `-Flash`) for debug.
- `-Test` unit tests, simulations
- `-Sim` run on-chip tests, simulations (no heating)

### Calibration

- Procedure: todo

### Configuration

- Firmware options: see `mcct-firmware/config/user_config.h`.
- Calibration: see `mcct-firmware/calibration/adc_to_temperature.csv`. A curve is fitted during build and written to the chip.
- To alter the reflow profiles, edit `mcct-firmware/profiles/*.json`. The profiles will be validated during the build and written to the chip. For each waypoint, the setpoint ramps linearly to `temp_c` over `ramp_s`, the controller waits for the plate to get there, then holds `hold_s`.

## Basic Usage

- In standby: Up/Down keys to change target temperature, ENT to confirm or stop heating
- SET to enter settings menu. Options: reflow, tune, PID, offset, or reset. ENT to select, SET to exit.
    - Reflow (rFL): select a profile
    - Tune (tun): select a target temperature


## Disclaimers

- This is not a reverse-engineered or decompilation of the original firmware. That firmware arrives locked to the chip, and must be erased to use this firmware.
- The schematic is for pinout reference only; the PCB file only shows mounting hole locations.
- Use at your own risk! This thing gets hot, and if something goes wrong...
