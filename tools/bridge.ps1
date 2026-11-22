<#
.SYNOPSIS
  Compile and/or upload the nuvo51icp ISP-to-ICP bridge (with UART passthrough)
  to the Arduino Uno, using the local arduino-cli. -Probe builds the read-only
  icp_probe sketch instead.

.EXAMPLE
  .\tools\bridge.ps1                 # compile + upload the bridge to COM8
  .\tools\bridge.ps1 -CompileOnly
  .\tools\bridge.ps1 -Port COM5
  .\tools\bridge.ps1 -Baud 57600     # change the target-side passthrough baud
  .\tools\bridge.ps1 -Probe          # upload icp_probe (replaces the bridge)
#>
[CmdletBinding()]
param(
    [string]$Port = 'COM8',
    [int]$Baud = 38400,
    [switch]$CompileOnly,
    [switch]$Probe
)
$ErrorActionPreference = 'Stop'

$here   = $PSScriptRoot
$cli    = Join-Path $here 'arduino-cli\arduino-cli.exe'
$config = Join-Path $here 'arduino-cli\arduino-cli.yaml'
$icpLib = Join-Path $here 'NuMicro-8051-prog\nuvo51icp'   # git submodule (+ nuvo51icp-local.patch)
$fqbn   = 'arduino:avr:uno'

if (-not (Test-Path $icpLib)) { throw "NuMicro-8051-prog not found (run: git submodule update --init)" }

if ($Probe) {
    # icp_probe reuses the ICP library from the submodule; Arduino needs the
    # files inside the sketch folder, so copy them in (they are git-ignored).
    $sketch = Join-Path $here 'icp_probe'
    foreach ($f in 'arduino.cpp', 'delay.h', 'n51_icp.c', 'n51_icp.h', 'n51_pgm.h') {
        Copy-Item (Join-Path $icpLib $f) $sketch -Force
    }
    $build = Join-Path $here 'arduino-cli\build\icp_probe'
    $flags = ''
} else {
    $sketch = $icpLib
    $build  = Join-Path $here 'arduino-cli\build\nuvo51icp'
    # CACHED_ROM_READ=0: the 64 KB read cache doesn't fit in the Uno's 2 KB RAM.
    # SERIAL_RX_BUFFER_SIZE=256: headroom for 64-byte ISP packets and for USB data
    # arriving while SoftwareSerial has interrupts disabled.
    $flags = "-DCACHED_ROM_READ=0 -DSERIAL_RX_BUFFER_SIZE=256 -DPASSTHROUGH_BAUD=$Baud"
}

& $cli --config-file $config compile -b $fqbn --build-path $build `
    --build-property "compiler.cpp.extra_flags=$flags" `
    --build-property "compiler.c.extra_flags=$flags" `
    $sketch
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

if (-not $CompileOnly) {
    & $cli --config-file $config upload -p $Port -b $fqbn --input-dir $build
    if ($LASTEXITCODE -ne 0) { throw "upload failed" }
}
