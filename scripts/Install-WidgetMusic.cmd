@echo off
setlocal enableextensions

set "ROOT=%~dp0"
set "DLL=%ROOT%WidgetMusicDeskband.dll"
set "ACTION=%~1"

if not exist "%DLL%" (
  echo [Install] Deskband DLL not found next to this script: "%DLL%"
  exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

echo [Install] Registering "%DLL%"...
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
  echo [Install] regsvr32 failed.
  exit /b 1
)

if /i "%ACTION%"=="restart" (
  echo [Install] Restarting Explorer...
  "%PS%" -NoProfile -Command "Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe" >nul 2>nul
)

echo [Install] Done. Enable Widget Music from the taskbar toolbar menu.
exit /b 0
