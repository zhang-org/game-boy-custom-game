# Build every game in src\ (each folder except common\ is one ROM).
#   .\build.ps1                 -> build all games
#   .\build.ps1 -Run pingpong   -> build all, then open build\pingpong.gbc in mGBA
#   .\build.ps1 -Run flappy
param([string]$Run)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot           # relative paths only: lcc dislikes spaces

$lcc = 'C:\gbdk\bin\lcc.exe'
New-Item -ItemType Directory -Force build | Out-Null

# mGBA locks the ROM file while it's open, so close it before rebuilding
Get-Process mGBA -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

$failed = $false
foreach ($dir in Get-ChildItem src -Directory | Where-Object Name -ne 'common') {
    $name  = $dir.Name
    $title = $name.ToUpper()
    if ($title.Length -gt 15) { $title = $title.Substring(0, 15) }
    # Optional extra compiler flags for one game (e.g. a cartridge with battery save): src\<game>\lcc_flags.txt
    $extra = @()
    $flagsFile = "src\$name\lcc_flags.txt"
    if (Test-Path $flagsFile) { $extra = (Get-Content $flagsFile -Raw).Trim() -split '\s+' }
    & $lcc -Wm-yc "-Wm-yn$title" @extra -o "build\$name.gbc" "src\$name\main.c"
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED: $name" -ForegroundColor Red; $failed = $true }
    else { Write-Host "Built build\$name.gbc" -ForegroundColor Green }
}
if ($failed) { exit 1 }

if ($Run) {
    $rom = "build\$Run.gbc"
    if (-not (Test-Path $rom)) { Write-Host "No such game: $Run" -ForegroundColor Red; exit 1 }
    $mgba = @("$env:ProgramFiles\mGBA\mGBA.exe", "${env:ProgramFiles(x86)}\mGBA\mGBA.exe", "$env:LOCALAPPDATA\Programs\mGBA\mGBA.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $mgba) { Write-Host 'mGBA not found' -ForegroundColor Red; exit 1 }
    Start-Process $mgba -ArgumentList "`"$(Resolve-Path $rom)`""
}
