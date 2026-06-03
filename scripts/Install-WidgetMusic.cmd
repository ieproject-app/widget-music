@echo off
setlocal enableextensions

set "ROOT=%~dp0"
set "DLL=%ROOT%WidgetMusicDeskband.dll"
set "HOST=%ROOT%WidgetMusicHost.exe"
set "ACTION=%~1"
set "RESTART_HELPER=%ROOT%Restart-WidgetMusicExplorer.ps1"
set "PREWARM_HELPER=%ROOT%Configure-WidgetMusicPrewarm.ps1"

if not exist "%DLL%" (
  echo [Install] Deskband DLL not found next to this script: "%DLL%"
  exit /b 1
)
if not exist "%HOST%" (
  echo [Install] Host EXE not found next to this script: "%HOST%"
  exit /b 1
)
if not exist "%PREWARM_HELPER%" (
  echo [Install] Prewarm helper not found next to this script: "%PREWARM_HELPER%"
  exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

if /i "%ACTION%"=="restart" (
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -RegistrationCommand "%~f0" -RegistrationArgumentsText "norestart" -StopHost
  exit /b %ERRORLEVEL%
)

echo [Install] Registering "%DLL%"...
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
  echo [Install] regsvr32 failed.
  exit /b 1
)

echo [Install] Enabling SnipTune 10 prewarm for this user...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%PREWARM_HELPER%" -Action Install -HostPath "%HOST%" -StartupDelayMs 15000
if errorlevel 1 (
  echo [Install] prewarm setup failed.
  exit /b 1
)

echo [Install] Done.
echo [Install] Enable manually from Taskbar ^> Toolbars ^> SnipTune 10.
exit /b 0
