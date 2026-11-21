@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\flash-pixeluhr.ps1" %*
if errorlevel 1 (
  echo.
  pause
  exit /b %errorlevel%
)
endlocal
