# Contributing

## Building Firmware

- Follow the [wiring](README.md#wiring) and [flashing](README.md#flash-binary) instructions first.
- Be careful if you ever power the hot plate over AC. It will be partially disassembled while connected to the programmer. Do not touch the internals of the device at all while it is plugged in. **You only need to do this if you are running a calibration routine or developing**; otherwise, you don't even need to solder a connector to the PCB; simply hold the pins in place (with the device unplugged) during the flash.
- **Do not** connect the +5V wire while the board is powered from its own supply. A common ground connection is fine, and actually required to complete the (optional) calibration.
- While developing, especially reading from or calibrating the sensor, you must use a laptop on battery. An earthed PC will offset the ADC readings significantly, as the thermocouple is earthed and you may experience grounding issues.

### Windows Setup

Requires git and this repo's submodules; the Nuvoton BSP and ICP programmer bridge:
```
git config --global core.longpaths true
git submodule update --init --recursive
```
Have SDCC 4.6.0 installed to `tools\sdcc` (or on PATH) \
Rename `sdcc\bin\cc1` to `cc1.exe`. \
Now, build and flash the firmware:
```
uv run mcct.py [command]
```
Subcommands:
- `build` build the firmware (`--sim`: simulated plate, no heating; `--cal`: calibration firmware; `--clean`: rebuild from scratch)
- `test` unit tests, simulations (`--cal`: calibration firmware tests)
- `flash` build, then write firmware + config (`--bin FILE`: write a prebuilt binary; `--monitor`: open the console afterwards; also `--sim`, `--cal`)
- `status` read chip ID/config
- `monitor` open the serial console for debug
- `bridge` load the bridge onto the Uno
- `--port` append to any command to specify a port

### Linux

- Building/configuring is not yet supported, but you can still [flash the binary](README.md#flash-binary).

## KiCAD
- kicad-cli v10.0+ on path
- Run `git config core.hooksPath .githooks` after cloning