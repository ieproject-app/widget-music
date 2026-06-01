@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ACTION=%~2"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

if /i "%ACTION%"=="restart" (
  "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "$killer = Start-Job -ScriptBlock { while ($true) { Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 100 } }; try { Stop-Process -Name WidgetMusicHost -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; & '%~f0' '%CONFIG%' norestart; $code = $LASTEXITCODE } finally { Stop-Job $killer -ErrorAction SilentlyContinue; Remove-Job $killer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe }; exit $code"
  set "ERR=%ERRORLEVEL%"
  popd >nul
  exit /b %ERR%
)

call "%ROOT%\\scripts\\Build.cmd" %CONFIG%
if errorlevel 1 (
  echo [Register] Build failed.
  popd >nul
  exit /b 1
)

set "OUTDIR=%ROOT%\\out\\%CONFIG%\\x64"
set "DLL=%OUTDIR%\\WidgetMusicDeskband.dll"

if not exist "%DLL%" (
  echo [Register] Deskband DLL not found: "%DLL%"
  popd >nul
  exit /b 1
)

set "REGSVR=%SystemRoot%\\System32\\regsvr32.exe"
if exist "%SystemRoot%\\Sysnative\\regsvr32.exe" set "REGSVR=%SystemRoot%\\Sysnative\\regsvr32.exe"

set "PS=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"
if exist "%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe" set "PS=%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe"

echo [Register] Registering "%DLL%" (per-user)...
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
  echo [Register] regsvr32 failed.
  popd >nul
  exit /b 1
)

echo [Register] Enabling Widget Music on taskbar...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\Enable-WidgetMusicTaskbar.ps1"
if errorlevel 1 (
  echo [Register] Warning: could not auto-enable taskbar band. You can enable it manually from Taskbar > Toolbars > Widget Music.
)

if /i "%ACTION%"=="restart" (
  echo [Register] Restarting Explorer...
  "%PS%" -NoProfile -Command "Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe" >nul 2>nul
)

echo [Register] Done.
popd >nul
exit /b 0
