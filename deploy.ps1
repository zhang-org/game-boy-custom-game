# Copy built ROMs to the Everdrive microSD card.   .\deploy.ps1 -Drive E
param([Parameter(Mandatory)][string]$Drive)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$dest = "$($Drive.TrimEnd(':','\')):\HOMEBREW"
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item build\*.gbc $dest -Force
Write-Host "Copied to $dest - safely eject the card, then play on the Chromatic." -ForegroundColor Green
