param(
  [string]$Port = "",

  [switch]$Force,
  [switch]$ListPorts,
  [switch]$ShowHelp
)

$ErrorActionPreference = "Stop"

function Write-Step($Text) {
  Write-Host ""
  Write-Host "==> $Text" -ForegroundColor Cyan
}

function Write-Usage {
  Write-Host "PixelUhr Reset Tool"
  Write-Host ""
  Write-Host "Usage:"
  Write-Host "  .\tools\reset-pixeluhr.ps1"
  Write-Host "  .\tools\reset-pixeluhr.ps1 -Port COM5"
  Write-Host "  .\tools\reset-pixeluhr.ps1 -Force"
  Write-Host "  .\tools\reset-pixeluhr.ps1 -ListPorts"
  Write-Host ""
  Write-Host "Options:"
  Write-Host "  -Port COMx                 Optional serial port. If omitted, PlatformIO auto-detects."
  Write-Host "  -Force                     Skip the confirmation prompt."
  Write-Host "  -ListPorts                 Show available COM ports."
  Write-Host ""
  Write-Host "This erases the complete ESP32 flash: firmware (incl. web UI), Wi-Fi,"
  Write-Host "settings, admin login, OTA slots, and all persisted data."
}

function Find-Pio {
  $commands = @("pio", "platformio")
  foreach ($command in $commands) {
    $found = Get-Command $command -ErrorAction SilentlyContinue
    if ($found) {
      return $found.Source
    }
  }

  $userPio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
  if (Test-Path $userPio) {
    return $userPio
  }

  throw "PlatformIO wurde nicht gefunden. Installiere PlatformIO oder starte einmal die PlatformIO IDE."
}

function Show-Ports {
  Write-Step "Serielle Ports"
  $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
  if ($ports.Count -eq 0) {
    Write-Host "Keine COM-Ports gefunden."
    return
  }

  foreach ($portName in $ports) {
    Write-Host "  $portName"
  }
}

function Invoke-Pio($Arguments) {
  Write-Host "pio $($Arguments -join ' ')" -ForegroundColor DarkGray
  & $script:PioPath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "PlatformIO-Befehl fehlgeschlagen: pio $($Arguments -join ' ')"
  }
}

if ($ShowHelp) {
  Write-Usage
  exit 0
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

if ($ListPorts) {
  Show-Ports
  exit 0
}

$script:PioPath = Find-Pio

Write-Host "PixelUhr Reset Tool" -ForegroundColor Yellow
Write-Host "Projekt: $repoRoot"
Write-Host "PlatformIO: $script:PioPath"
if ($Port) {
  Write-Host "Port: $Port"
} else {
  Write-Host "Port: automatisch"
}
Write-Host ""
Write-Host "WARNUNG: Der komplette ESP32-Flash wird geloescht." -ForegroundColor Yellow
Write-Host "Danach sind Firmware mit Weboberflaeche, WLAN, Einstellungen,"
Write-Host "Admin-Zugangsdaten, OTA-Slots und alle gespeicherten Daten weg."
Write-Host "Der ESP32 startet danach erst wieder sinnvoll, wenn die Firmware neu geflasht wurde."

if (-not $Force) {
  $answer = Read-Host "Zum Fortfahren ERASE eingeben"
  if ($answer -ne "ERASE") {
    Write-Host "Abgebrochen. Es wurde nichts geloescht." -ForegroundColor Green
    exit 0
  }
}

$eraseArgs = @("run", "--target", "erase")
if ($Port) {
  $eraseArgs += @("--upload-port", $Port)
}

Write-Step "ESP32-Flash komplett loeschen"
Invoke-Pio $eraseArgs

Write-Host ""
Write-Host "Fertig. Der ESP32-Flash wurde geloescht." -ForegroundColor Green
Write-Host "Naechster Schritt: .\flash-pixeluhr.cmd ausfuehren, um die Firmware neu zu flashen."
