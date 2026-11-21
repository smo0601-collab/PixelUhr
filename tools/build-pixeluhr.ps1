param(
  [ValidateSet("All", "Firmware")]
  [string]$Target = "All",

  [string]$OutputDir = "dist",

  [switch]$Clean,
  [switch]$ShowHelp
)

$ErrorActionPreference = "Stop"

function Write-Step($Text) {
  Write-Host ""
  Write-Host "==> $Text" -ForegroundColor Cyan
}

function Write-Usage {
  Write-Host "PixelUhr Build Tool"
  Write-Host ""
  Write-Host "Usage:"
  Write-Host "  .\tools\build-pixeluhr.ps1"
  Write-Host "  .\tools\build-pixeluhr.ps1 -Target Firmware"
  Write-Host "  .\tools\build-pixeluhr.ps1 -OutputDir release"
  Write-Host "  .\tools\build-pixeluhr.ps1 -Clean"
  Write-Host ""
  Write-Host "Options:"
  Write-Host "  -Target All|Firmware       Build the firmware (the web interface is embedded)."
  Write-Host "  -OutputDir path            Folder for copied .bin files. Default: dist."
  Write-Host "  -Clean                     Clean before building."
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

function Invoke-Pio($Arguments) {
  Write-Host "pio $($Arguments -join ' ')" -ForegroundColor DarkGray
  & $script:PioPath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "PlatformIO-Befehl fehlgeschlagen: pio $($Arguments -join ' ')"
  }
}

function Read-FirstRegexMatch($Path, $Pattern) {
  if (-not (Test-Path $Path)) {
    return ""
  }

  $content = Get-Content -Path $Path -Raw
  $match = [regex]::Match($content, $Pattern)
  if ($match.Success) {
    return $match.Groups[1].Value
  }
  return ""
}

function Read-RequiredVersion($Path, $Pattern, $Name) {
  $version = Read-FirstRegexMatch -Path $Path -Pattern $Pattern
  if (-not $version) {
    throw "$Name-Version wurde nicht gefunden in: $Path"
  }
  return $version
}

function Copy-Binary($Source, $BaseName, $Version) {
  if (-not (Test-Path $Source)) {
    throw "Erwartete Datei wurde nicht gefunden: $Source"
  }

  $name = if ($Version) { "$BaseName-v$Version.bin" } else { "$BaseName.bin" }
  $destination = Join-Path $script:OutputPath $name
  Copy-Item -Path $Source -Destination $destination -Force
  Write-Host "  $destination" -ForegroundColor Green
}

if ($ShowHelp) {
  Write-Usage
  exit 0
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

$script:PioPath = Find-Pio
$script:OutputPath = Join-Path $repoRoot $OutputDir
$buildRoot = Join-Path $repoRoot ".pio\build\esp32dev"
$firmwareVersion = Read-RequiredVersion `
  -Path (Join-Path $repoRoot "src\app_state.h") `
  -Pattern '#define\s+FIRMWARE_VERSION_TEXT\s+"([^"]+)"' `
  -Name "Firmware"
$littleFsVersion = Read-RequiredVersion `
  -Path (Join-Path $repoRoot "data\updates.js") `
  -Pattern 'PIXELUHR_LITTLEFS_VERSION=([^"'';\\r\\n]+)' `
  -Name "LittleFS"

Write-Host "PixelUhr Build Tool" -ForegroundColor Green
Write-Host "Projekt: $repoRoot"
Write-Host "PlatformIO: $script:PioPath"
Write-Host "Ziel: $Target"
Write-Host "Ausgabe: $script:OutputPath"
if ($firmwareVersion) { Write-Host "Firmware-Version: $firmwareVersion" }
if ($littleFsVersion) { Write-Host "Weboberflaeche: $littleFsVersion" }

if ($Clean) {
  Write-Step "Clean"
  Invoke-Pio -Arguments @("run", "--target", "clean")
}

New-Item -ItemType Directory -Path $script:OutputPath -Force | Out-Null

if ($Target -eq "All" -or $Target -eq "Firmware") {
  Write-Step "Firmware bauen"
  Invoke-Pio -Arguments @("run")
  Copy-Binary `
    -Source (Join-Path $buildRoot "firmware.bin") `
    -BaseName "pixeluhr-firmware" `
    -Version $firmwareVersion
}


Write-Host ""
Write-Host "Fertig. Es wurde nichts geflasht." -ForegroundColor Green
