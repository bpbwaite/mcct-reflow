<#
.SYNOPSIS
  Build, test and flash the MCCT hot plate firmware (MS51FB9AE, SDCC).

.EXAMPLE
  .\build.ps1                    # generate tables, build -> build\mcct.hex / build\mcct.bin
  .\build.ps1 -Test              # build and run the unit tests in the 8051 simulator
  .\build.ps1 -Sim -Flash        # bench build: boots into the simulated hot plate
  .\build.ps1 -Clean             # remove build\ first
  .\build.ps1 -Flash             # build, then write APROM + CONFIG via the Arduino ICP bridge
  .\build.ps1 -Flash -Port COM5  # use a different serial port
  .\build.ps1 -Status            # only query the chip through the bridge
  .\build.ps1 -Monitor           # open the chip's UART1 console through the bridge
  .\build.ps1 -Flash -Monitor    # build, flash, then open the console (Ctrl+] to quit)
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Test,
    [switch]$Sim,
    [switch]$Flash,
    [switch]$Status,
    [switch]$Monitor,
    [string]$Port = 'COM8'
)

$ErrorActionPreference = 'Stop'

$Root    = $PSScriptRoot
$Tools   = Join-Path $Root '..\tools' | Resolve-Path
$SdccBin = Join-Path $Tools 'sdcc\bin'
$Isp     = Join-Path $Tools 'nuvoispy.ps1'
$Python  = Join-Path $Tools 'venv\Scripts\python.exe'
# The bridge's USB side always runs at 115200; it converts to the target's
# console baud (CFG_CONSOLE_BAUD / PASSTHROUGH_BAUD, 38400) itself.
$BridgeBaud = 115200

# sdcpp locates cc1.exe via PATH, so put the local SDCC first (this process only).
$env:PATH = "$SdccBin;$env:PATH"

$Target     = 'mcct'
$SrcDir     = Join-Path $Root 'src'
$TestDir    = Join-Path $Root 'test'
$ConfigDir  = Join-Path $Root 'config'
$ChipConfig = Join-Path $ConfigDir 'chip_config.json'
$Scripts    = Join-Path $Root 'scripts'
# Nuvoton MS51 BSP (tools/MS51_BSP submodule, unmodified): only its headers
# (SFR names) are used; the firmware has its own drivers.
$BspDir  = Join-Path $Tools 'MS51_BSP\MS51FB9AE_MS51XB9AE_MS51XB9BE\Library'
if (-not (Test-Path $BspDir)) { throw "MS51 BSP not found at $BspDir (run: git submodule update --init)" }
$OutDir  = Join-Path $Root 'build'
$GenDir  = Join-Path $OutDir 'generated'
$ObjDir  = Join-Path $OutDir 'obj'
$TestObj = Join-Path $OutDir 'test'

# MS51FB9AE memory map (no LDROM -> 16 KB APROM). The last two 128-byte
# pages hold the settings (src/storage/settings.c), so code stops below them.
$SettingsBase = 0x3F00
$CodeSize = $SettingsBase
$IramSize = 256
$XramSize = 1024
# Small model: globals default to fast internal RAM; bulky state is placed
# in XRAM explicitly with __xdata.
$Model    = 'small'

# Modules that touch hardware registers. Everything else is pure logic and
# is linked into the simulator unit tests (test/fakes.c stands in for these).
$HardwareSources = @('main.c', 'board\*', 'display\display.c', 'sensor\adc.c',
                     'storage\flash.c', 'debug\console.c', 'debug\cli.c')

$IncDirs = @($SrcDir, $ConfigDir, $GenDir,
             (Join-Path $BspDir 'Device\Include'),
             (Join-Path $BspDir 'StdDriver\inc'))
# Nuvoton headers select their SDCC variant with __SDCC__, which SDCC itself
# doesn't define (it uses __SDCC), so define it here.
$Cflags = @('-mmcs51', "--model-$Model", '--std-c11', '--opt-code-size', '-D__SDCC__',
            ('-DSETTINGS_FLASH_BASE=0x{0:X4}' -f $SettingsBase)) +
          ($IncDirs | ForEach-Object { "-I$_" })
# Bench build: the simulated hot plate runs from power-up (CFG_PLANT_SIM).
if ($Sim) { $Cflags += '-DCFG_PLANT_SIM=2' }

function Invoke-Tool([string]$exe, [string[]]$toolArgs) {
    # SDCC prints warnings on stderr; Windows PowerShell 5.1 would turn those
    # into terminating errors under 'Stop', so relax it and echo them as text.
    # Output goes to the host only, so callers' return values stay clean.
    $ErrorActionPreference = 'Continue'
    & (Join-Path $SdccBin $exe) @toolArgs 2>&1 | ForEach-Object { Write-Host "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "$exe failed (exit $code)" }
}

function Invoke-Python([string[]]$pyArgs) {
    $ErrorActionPreference = 'Continue'
    & $Python -I @pyArgs 2>&1 | ForEach-Object { Write-Host "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "code generation failed (exit $code)" }
}

function Invoke-Isp([string[]]$ispArgs) {
    # nuvoispy prints warnings on stderr; don't let PowerShell 5.1 abort mid-flash
    # if a caller redirects stderr (see Invoke-Tool).
    $ErrorActionPreference = 'Continue'
    & $Isp @ispArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "nuvoispy failed (exit $code)" }
}

# Newest header / config / generated file / this script: any change to them
# rebuilds every object (simple and safe; a full build takes seconds).
function Get-HeaderStamp {
    $files = @(Get-ChildItem $SrcDir, $ConfigDir, $GenDir, $TestDir -Recurse -File -Include *.h) +
             @(Get-Item $PSCommandPath)
    ($files | Sort-Object LastWriteTime | Select-Object -Last 1).LastWriteTime
}

# Compile sources whose object is missing or older than the source or any
# header. Objects are named after the source's path, so names stay unique.
function Build-Objects($sources, [string]$objDir, [string]$baseDir, [string[]]$extra = @()) {
    New-Item -ItemType Directory -Force $objDir | Out-Null
    $stamp = Get-HeaderStamp
    foreach ($c in $sources) {
        $relName = $c.FullName.Substring($baseDir.Length).TrimStart('\') -replace '[\\/]', '_'
        $rel = Join-Path $objDir ([IO.Path]::ChangeExtension($relName, '.rel'))
        if (-not (Test-Path $rel) -or (Get-Item $rel).LastWriteTime -lt $c.LastWriteTime -or
            (Get-Item $rel).LastWriteTime -lt $stamp) {
            Write-Host "CC  $($c.FullName.Substring($Root.Length + 1))"
            Invoke-Tool 'sdcc.exe' ($Cflags + $extra + @('-c', $c.FullName, '-o', $rel))
        }
        $rel
    }
}

function Test-IsHardware($file) {
    $rel = $file.FullName.Substring($SrcDir.Length + 1)
    foreach ($pattern in $HardwareSources) { if ($rel -like $pattern) { return $true } }
    return $false
}

function Start-Monitor {
    # Opening the port resets the Uno, whose bootloader pulses D13 = nRESET,
    # so the target restarts and you see its output from the beginning.
    Write-Host "Console on $Port (bridge passthrough). Ctrl+] to quit."
    & $Python -m serial.tools.miniterm --eol LF --quiet $Port $BridgeBaud
}

if ($Status) { Invoke-Isp @('-p', $Port, '-u'); return }
if ($Monitor -and -not $Flash) { Start-Monitor; return }

if ($Clean -and (Test-Path $OutDir)) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force $ObjDir, $GenDir | Out-Null

# ---- generated tables (calibration polynomial, reflow profiles) ----
Invoke-Python @((Join-Path $Scripts 'gen_calibration.py'),
                (Join-Path $Root 'calibration\adc_to_temperature.csv'),
                (Join-Path $GenDir 'calibration_gen.h'))
Invoke-Python @((Join-Path $Scripts 'gen_profiles.py'),
                (Join-Path $Root 'profiles'),
                (Join-Path $ConfigDir 'user_config.h'),
                (Join-Path $SrcDir 'display\font.c'),
                (Join-Path $GenDir 'profiles_gen.c'))

$allSources = @(Get-ChildItem $SrcDir -Recurse -Filter *.c) + @(Get-Item (Join-Path $GenDir 'profiles_gen.c'))

# ---- unit tests in the simulator ----
if ($Test) {
    $testSources = @($allSources | Where-Object { -not (Test-IsHardware $_) })
    $testDefs = @('-DUNIT_TEST', '-DCFG_PLANT_SIM=1')     # the closed-loop tests need the plant
    $testRels = @(Build-Objects $testSources $TestObj $Root $testDefs) +
                @(Build-Objects (Get-ChildItem $TestDir -Filter *.c) $TestObj $Root $testDefs)
    $runner = $testRels | Where-Object { (Split-Path $_ -Leaf) -eq 'test_test_main.rel' }
    $testRels = @($runner) + @($testRels | Where-Object { $_ -ne $runner })
    $ihx = Join-Path $TestObj 'tests.ihx'
    Write-Host "LD  tests.ihx"
    Invoke-Tool 'sdcc.exe' (@('-mmcs51', "--model-$Model", '--code-size', 65536, '--iram-size', 256,
                              '--xram-size', 0xF000, '--xram-loc', 0, '-o', $ihx) + $testRels)
    # 8052 core; the program prints and stops through ucsim's simulator
    # interface at XRAM 0xFFFF (see test/test_main.c). ucsim runs ~8x slower
    # when attached to this console, so it gets a hidden one of its own.
    Write-Host "RUN tests (ucsim, about a minute)"
    $log = Join-Path $TestObj 'tests.log'
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $ucsim = Start-Process -FilePath (Join-Path $SdccBin 'ucsim_51.exe') -WindowStyle Hidden -PassThru `
        -WorkingDirectory $TestObj -ArgumentList @('-q', '-t', '8052', '-I', 'if=xram[0xffff]', '-G', 'tests.ihx') `
        -RedirectStandardOutput $log -RedirectStandardError (Join-Path $TestObj 'tests.err')
    if (-not $ucsim.WaitForExit(15 * 60 * 1000)) { $ucsim.Kill(); throw 'unit tests timed out' }
    $out = Get-Content $log
    $out | Where-Object { $_ -notmatch '^(uCsim|This is free|under certain|\s*$)' } | ForEach-Object { Write-Host $_ }
    Write-Host ("    ({0:N0} s in the simulator)" -f $sw.Elapsed.TotalSeconds)
    if (-not ($out -match '^ALL TESTS PASSED')) { throw 'unit tests failed' }
    return
}

# ---- application ----
# Different flags (e.g. -Sim) need fresh objects: remember the last set.
$flagStamp = Join-Path $ObjDir 'cflags.txt'
$flagText  = $Cflags -join ' '
if (-not (Test-Path $flagStamp) -or (Get-Content $flagStamp -Raw) -ne $flagText) {
    Remove-Item (Join-Path $ObjDir '*.rel') -ErrorAction SilentlyContinue
    Set-Content $flagStamp $flagText -NoNewline
}
$rels = @(Build-Objects $allSources $ObjDir $Root)

# ---- link (the file containing main() first) ----
$mainRel = $rels | Where-Object { (Split-Path $_ -Leaf) -eq 'src_main.rel' }
$rels    = @($mainRel) + @($rels | Where-Object { $_ -ne $mainRel })
$ihx     = Join-Path $ObjDir "$Target.ihx"
Write-Host "LD  $Target.ihx"
Invoke-Tool 'sdcc.exe' (@('-mmcs51', "--model-$Model", '--code-size', $CodeSize, '--iram-size', $IramSize,
                          '--xram-size', $XramSize, '--xram-loc', '0x0000', '-o', $ihx) +
                        $rels)

# SDCC copies initialised __xdata variables at startup with MOVX @R0 paged
# by P2, which the MS51 does not do: such variables would silently start as
# garbage. Refuse to build if any exist (initialise them in code instead).
$xinit = Select-String -Path (Join-Path $ObjDir "$Target.map") -Pattern '^\s*C:\s+([0-9A-F]+)\s+l_XINIT\s*$' |
         Select-Object -First 1
if ($xinit -and [Convert]::ToInt32($xinit.Matches[0].Groups[1].Value, 16) -ne 0) {
    throw "initialised __xdata variables found (l_XINIT = 0x$($xinit.Matches[0].Groups[1].Value)): initialise them in code (see AGENTS.md)"
}

# ---- hex / bin ----
$hex = Join-Path $OutDir "$Target.hex"
$bin = Join-Path $OutDir "$Target.bin"
# packihx prints its summary on stderr; Windows PowerShell 5.1 turns that into
# an error under 'Stop', so relax it for this one call.
$ErrorActionPreference = 'Continue'
& (Join-Path $SdccBin 'packihx.exe') $ihx 2>$null | Set-Content -Encoding ascii $hex
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw "packihx failed (exit $LASTEXITCODE)" }
Invoke-Tool 'makebin.exe' @('-p', $ihx, $bin)   # -p: trim to last used byte

# ---- size report (from the linker memory map) ----
$mem = Get-Content (Join-Path $ObjDir "$Target.mem") -ErrorAction SilentlyContinue
$used = (Get-Item $bin).Length
Write-Host ("OK  {0}  {1} / {2} bytes code ({3:P1}); settings at 0x{4:X4}" -f (Split-Path $bin -Leaf), $used, $CodeSize, ($used / $CodeSize), $SettingsBase)
if ($mem) { $mem | Select-String 'Stack starts|Other memory|ROM/EPROM|EXTERNAL RAM' | ForEach-Object { Write-Host "    $($_.Line.Trim())" } }

# ---- flash (APROM + CONFIG bytes from config/chip_config.json) ----
if ($Flash) {
    Write-Host "ISP write $bin + $(Split-Path $ChipConfig -Leaf) via Arduino bridge on $Port"
    Invoke-Isp @('-p', $Port, '-c', $ChipConfig, '-w', $bin)
}

if ($Monitor) { Start-Monitor }
