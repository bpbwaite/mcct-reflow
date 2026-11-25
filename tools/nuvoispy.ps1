# Wrapper for nuvoispy (NuMicro-8051-prog) through uv.
# Usage examples:
#   .\tools\nuvoispy.ps1 -p COM8 -u                 # show chip info/config
#   .\tools\nuvoispy.ps1 -p COM8 -r dump.bin        # read flash
#   .\tools\nuvoispy.ps1 -p COM8 -w firmware.bin    # write APROM
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location "$here\NuMicro-8051-prog"
try { & uv run --project (Split-Path -Parent $here) python -m nuvoprogpy.nuvoispy @args }
finally { Pop-Location }
