# Builds the GBA game (gba\snowberry.gba). Windows version of build.sh - keep them doing the same thing.
#   .\build.ps1          build
#   .\build.ps1 -Run     build, then open the ROM in mGBA
#   .\build.ps1 -Clean   delete build output
# Needs devkitPro (with the gba-dev packages) in C:\devkitPro and Python 3 in PATH. See README.
param([switch]$Run, [switch]$Clean)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if ($PSScriptRoot -match ' ') {
    Write-Host "The folder path has a space in it ($PSScriptRoot) and the build tools can't handle that."
    Write-Host 'Give the project folder a drive letter and build from there, for example:'
    Write-Host ('  subst G: "' + (Split-Path $PSScriptRoot -Parent) + '"')
    Write-Host '  G:\gba\build.ps1'
    exit 1
}

$dkp = if ($env:DEVKITPRO_WIN) { $env:DEVKITPRO_WIN } else { 'C:\devkitPro' }
$make = Join-Path $dkp 'msys2\usr\bin\make.exe'
if (-not (Test-Path $make)) { Write-Host "devkitPro not found in $dkp (see README: GBA game setup)"; exit 1 }
if (-not (Test-Path 'butano\butano\butano.mak')) {
    # git prints progress on stderr. With 'Stop', Windows PowerShell 5.1 can treat that as a fatal error, so relax it here.
    $ErrorActionPreference = 'Continue'
    git submodule update --init
    $ErrorActionPreference = 'Stop'
    if (-not (Test-Path 'butano\butano\butano.mak')) { Write-Host 'Could not download the Butano engine (git submodule update --init failed).'; exit 1 }
}

# devkitPro's tools expect these MSYS-style paths.
if (-not $env:DEVKITPRO) { $env:DEVKITPRO = '/opt/devkitpro' }
if (-not $env:DEVKITARM) { $env:DEVKITARM = '/opt/devkitpro/devkitARM' }
$env:PATH = "$dkp\msys2\usr\bin;$dkp\devkitARM\bin;$dkp\tools\bin;$env:PATH"

$python = $null
# Windows ships fake 'python'/'python3' shortcuts that only open the Microsoft Store, so check that each one really runs.
foreach ($candidate in 'python', 'python3', 'py') {
    if (-not (Get-Command $candidate -ErrorAction SilentlyContinue)) { continue }
    $ErrorActionPreference = 'Continue'
    $version = & $candidate --version 2>&1 | Out-String
    $ok = ($LASTEXITCODE -eq 0) -and ($version -match 'Python 3')
    $ErrorActionPreference = 'Stop'
    if ($ok) { $python = $candidate; break }
}
if (-not $python) { Write-Host 'Python 3 not found. Install it from python.org (tick "Add to PATH").'; exit 1 }

if ($Clean) { & $make clean; exit $LASTEXITCODE }

$jobs = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { 4 }
& $make -j $jobs "PYTHON=$python"
if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED'; exit 1 }
Write-Host "Built $PSScriptRoot\snowberry.gba"

if ($Run) {
    Get-Process mGBA -ErrorAction SilentlyContinue | Stop-Process
    Start-Process 'C:\Program Files\mGBA\mGBA.exe' -ArgumentList "`"$PSScriptRoot\snowberry.gba`""
}
