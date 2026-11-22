# Notes: programmer tools (tools/)

`NuMicro-8051-prog/` is a git submodule of https://github.com/nikitalita/NuMicro-8051-prog
pinned at commit 1093ddf, **with local changes applied in its working tree**
from `nuvo51icp-local.patch` (here in tools/). `.gitmodules` sets
`ignore = dirty` for it, so those changes don't show in the parent repo's
`git status`. The patch file is the tracked copy: after any change inside the
submodule, **regenerate it** (`git -C tools/NuMicro-8051-prog diff > tools/nuvo51icp-local.patch`).
Don't `pull`, `reset`, `checkout` or `stash` in the submodule without doing that
first. Every local change is marked with a `LOCAL PATCH` comment.

`MS51_BSP/` is a git submodule of https://github.com/OpenNuvoton/MS51_BSP,
used unmodified by mcct-firmware (see `../mcct-firmware/AGENTS.md`).

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
  enabled. Fixed 2026-11-21; the first real chip then read and flashed fine
  with no power cycle.
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
The passthrough baud must match `CONSOLE_BAUD` in `mcct-firmware/inc/console.h`.

## Related
- `nuvoispy.ps1` runs nuvoispy from `venv`.
- `icp_probe/` is a separate read-only power-cycle probe sketch. `bridge.ps1
  -Probe` copies its ICP library files in from the submodule (they are
  git-ignored). Uploading it replaces the bridge on the Uno.
