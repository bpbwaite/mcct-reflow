<#
.SYNOPSIS
  Build (and optionally flash) the MCCT firmware for the MS51FB9AE with SDCC.

.EXAMPLE
  .\build.ps1                    # build -> build\mcct.hex / build\mcct.bin
  .\build.ps1 -Clean             # remove build\ then build
  .\build.ps1 -Flash             # build, then write APROM via the Arduino ICP bridge
  .\build.ps1 -Flash -Port COM5  # use a different serial port
  .\build.ps1 -Status            # only query the chip through the bridge
  .\build.ps1 -Monitor           # open the chip's UART1 console through the bridge
  .\build.ps1 -Flash -Monitor    # build, flash, then open the console (Ctrl+] to quit)
#>
[CmdletBinding()]
param(
    [switch]$Clean,
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
# console baud (CONSOLE_BAUD / PASSTHROUGH_BAUD, 38400) itself.
$BridgeBaud = 115200

# sdcpp locates cc1.exe via PATH, so put the local SDCC first (this process only).
$env:PATH = "$SdccBin;$env:PATH"

$Target  = 'mcct'
$SrcDir  = Join-Path $Root 'src'
# Nuvoton MS51 BSP, used unmodified from the tools/MS51_BSP git submodule.
$BspDir  = Join-Path $Tools 'MS51_BSP\MS51FB9AE_MS51XB9AE_MS51XB9BE\Library'
$BspSrc  = Join-Path $BspDir 'StdDriver\src'
# Not built: putchar.c has no SDCC variant, and sdcc_stdio.c's UART0
# putchar/getchar are replaced by src/console.c (its char getchar() also
# conflicts with SDCC 4.6's stdio.h).
$BspSkip = @('putchar.c', 'sdcc_stdio.c')
if (-not (Test-Path $BspDir)) { throw "MS51 BSP not found at $BspDir (run: git submodule update --init)" }
$IncDirs = @((Join-Path $Root 'inc'),
             (Join-Path $BspDir 'Device\Include'),
             (Join-Path $BspDir 'StdDriver\inc'))
$OutDir  = Join-Path $Root 'build'
$ObjDir  = Join-Path $OutDir 'obj'
$BspObj  = Join-Path $ObjDir 'bsp'
$BspLib  = Join-Path $ObjDir 'ms51bsp.lib'

# MS51FB9AE memory map (no LDROM configured -> full 16 KB APROM)
$CodeSize = 16384
$IramSize = 256
$XramSize = 1024
# Small model: globals default to fast internal RAM. Switch to 'large' if you
# run out of IRAM (the BSP's own SDCC samples use large).
$Model    = 'small'

# Nuvoton headers select their SDCC variant with __SDCC__, which SDCC itself
# doesn't define (it uses __SDCC), so define it here.
$Cflags = @('-mmcs51', "--model-$Model", '--std-c11', '--opt-code-size', '-D__SDCC__') +
          ($IncDirs | ForEach-Object { "-I$_" })
$Lflags = @('-mmcs51', "--model-$Model",
            '--code-size', $CodeSize, '--iram-size', $IramSize,
            '--xram-size', $XramSize, '--xram-loc', '0x0000')

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

function Invoke-Isp([string[]]$ispArgs) {
    # nuvoispy prints warnings on stderr; don't let PowerShell 5.1 abort mid-flash
    # if a caller redirects stderr (see Invoke-Tool).
    $ErrorActionPreference = 'Continue'
    & $Isp @ispArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "nuvoispy failed (exit $code)" }
}

function Build-Objects($sources, [string]$objDir, [string[]]$extra = @()) {
    New-Item -ItemType Directory -Force $objDir | Out-Null
    foreach ($c in $sources) {
        $rel = Join-Path $objDir ($c.BaseName + '.rel')
        Write-Host "CC  $($c.Name)"
        Invoke-Tool 'sdcc.exe' ($Cflags + $extra + @('-c', $c.FullName, '-o', $rel))
        $rel
    }
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
New-Item -ItemType Directory -Force $ObjDir | Out-Null

# ---- BSP driver library ----
# Built as an archive so the linker only pulls in the modules that are used
# (and none of isr.c's stub interrupt handlers unless referenced).
# Rebuilt only when a BSP file or this script is newer than the archive.
$bspNewest = (Get-ChildItem $BspDir -Recurse -File) + (Get-Item $PSCommandPath) |
             Sort-Object LastWriteTime | Select-Object -Last 1
if (-not (Test-Path $BspLib) -or $bspNewest.LastWriteTime -gt (Get-Item $BspLib).LastWriteTime) {
    if (Test-Path $BspObj) { Remove-Item -Recurse -Force $BspObj }
    # 357: vendor code uses non-const __code pointers (harmless, silenced for the BSP only)
    $bspSources = Get-ChildItem $BspSrc -Filter *.c | Where-Object { $BspSkip -notcontains $_.Name }
    $bspRels = Build-Objects $bspSources $BspObj @('--disable-warning', '357')
    if (Test-Path $BspLib) { Remove-Item $BspLib }
    Write-Host "AR  ms51bsp.lib"
    Invoke-Tool 'sdar.exe' (@('-rcs', $BspLib) + $bspRels)
}

# ---- application ----
$rels = @(Build-Objects (Get-ChildItem $SrcDir -Filter *.c) $ObjDir)
if (-not $rels) { throw "no sources in $SrcDir" }

# ---- link ----
# The file containing main() must be linked first.
$mainRel = $rels | Where-Object { (Split-Path $_ -Leaf) -eq 'main.rel' }
$rels    = @($mainRel) + @($rels | Where-Object { $_ -ne $mainRel })
$ihx     = Join-Path $ObjDir "$Target.ihx"
Write-Host "LD  $Target.ihx"
Invoke-Tool 'sdcc.exe' ($Lflags + @('-o', $ihx) + $rels + @("-L$ObjDir", '-lms51bsp.lib'))

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
Write-Host ("OK  {0}  {1} / {2} bytes APROM ({3:P1})" -f (Split-Path $bin -Leaf), $used, $CodeSize, ($used / $CodeSize))
if ($mem) { $mem | Select-String 'Stack starts|Other memory|ROM/EPROM' | ForEach-Object { Write-Host "    $($_.Line.Trim())" } }

# ---- flash ----
if ($Flash) {
    Write-Host "ISP write $bin via Arduino bridge on $Port"
    Invoke-Isp @('-p', $Port, '-w', $bin)
}

if ($Monitor) { Start-Monitor }
