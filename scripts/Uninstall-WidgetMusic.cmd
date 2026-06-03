@echo off
setlocal enableextensions

set "ROOT=%~dp0"
set "DLL=%ROOT%WidgetMusicDeskband.dll"
set "ACTION=%~1"
set "PREWARM_HELPER=%ROOT%Configure-WidgetMusicPrewarm.ps1"

if not exist "%DLL%" (
  echo [Uninstall] Deskband DLL not found next to this script: "%DLL%"
  exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
set "RESTART_HELPER=%ROOT%Restart-WidgetMusicExplorer.ps1"

echo [Uninstall] Unregistering "%DLL%"...
"%REGSVR%" /s /u "%DLL%"
if errorlevel 1 (
  echo [Uninstall] regsvr32 failed.
  exit /b 1
)

if exist "%PREWARM_HELPER%" (
  echo [Uninstall] Disabling SnipTune 10 prewarm...
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%PREWARM_HELPER%" -Action Uninstall >nul 2>nul
)

if /i "%ACTION%"=="restart" (
  echo [Uninstall] Restarting Explorer...
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -StopHost >nul 2>nul
)

echo [Uninstall] Done.
exit /b 0
