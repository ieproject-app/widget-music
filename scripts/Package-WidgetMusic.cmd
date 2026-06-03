@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

call "%ROOT%\scripts\Build.cmd" %CONFIG%
if errorlevel 1 (
  echo [Package] Build failed.
  popd >nul
  exit /b 1
)

set "OUTDIR=%ROOT%\out\%CONFIG%\x64"
set "DIST=%ROOT%\out\dist\SnipTune10"

if not exist "%OUTDIR%\WidgetMusicDeskband.dll" (
  echo [Package] Missing "%OUTDIR%\WidgetMusicDeskband.dll".
  popd >nul
  exit /b 1
)
if not exist "%OUTDIR%\WidgetMusicHost.exe" (
  echo [Package] Missing "%OUTDIR%\WidgetMusicHost.exe".
  popd >nul
  exit /b 1
)

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%" >nul || (
  echo [Package] Could not create "%DIST%".
  popd >nul
  exit /b 1
)

copy /y "%OUTDIR%\WidgetMusicDeskband.dll" "%DIST%\" >nul
copy /y "%OUTDIR%\WidgetMusicHost.exe" "%DIST%\" >nul
copy /y "%ROOT%\scripts\Install-WidgetMusic.cmd" "%DIST%\Register-WidgetMusic.cmd" >nul
copy /y "%ROOT%\scripts\Uninstall-WidgetMusic.cmd" "%DIST%\Unregister-WidgetMusic.cmd" >nul
copy /y "%ROOT%\scripts\Configure-WidgetMusicPrewarm.ps1" "%DIST%\Configure-WidgetMusicPrewarm.ps1" >nul
copy /y "%ROOT%\scripts\Restart-WidgetMusicExplorer.ps1" "%DIST%\Restart-WidgetMusicExplorer.ps1" >nul

> "%DIST%\README.txt" echo SnipTune 10 runtime package
>> "%DIST%\README.txt" echo.
>> "%DIST%\README.txt" echo Files in this folder are the runtime package. PDB and intermediate build files stay in out\%CONFIG%\x64 for developer diagnostics.
>> "%DIST%\README.txt" echo.
>> "%DIST%\README.txt" echo Install:   Register-WidgetMusic.cmd restart
>> "%DIST%\README.txt" echo Enable:    Right click taskbar ^> Toolbars ^> SnipTune 10
>> "%DIST%\README.txt" echo Prewarm:   Installed per-user at login for faster first activation
>> "%DIST%\README.txt" echo Uninstall: Unregister-WidgetMusic.cmd restart
> "%DIST%\VERSION.txt" echo 1.0.5.0

powershell -NoProfile -ExecutionPolicy Bypass -Command "$dist='%DIST%'; Get-ChildItem -LiteralPath $dist -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object { '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant(), $_.Name } | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii"
if errorlevel 1 (
  echo [Package] Could not generate SHA256SUMS.txt.
  popd >nul
  exit /b 1
)

for /f "usebackq delims=" %%S in (`powershell -NoProfile -Command "$sum=(Get-ChildItem -LiteralPath '%DIST%' -File | Measure-Object Length -Sum).Sum; [math]::Round($sum/1KB,1)"`) do set "SIZEKB=%%S"

echo [Package] Created "%DIST%" (%SIZEKB% KB).
popd >nul
exit /b 0
