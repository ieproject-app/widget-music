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
set "DIST=%ROOT%\out\dist\WidgetMusic"

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
copy /y "%ROOT%\scripts\Enable-WidgetMusicTaskbar.ps1" "%DIST%\Enable-WidgetMusicTaskbar.ps1" >nul
copy /y "%ROOT%\scripts\Invoke-WidgetMusicTaskbarEnable.ps1" "%DIST%\Invoke-WidgetMusicTaskbarEnable.ps1" >nul

> "%DIST%\README.txt" echo Widget Music runtime package
>> "%DIST%\README.txt" echo.
>> "%DIST%\README.txt" echo Files in this folder are the runtime package. PDB and intermediate build files stay in out\%CONFIG%\x64 for developer diagnostics.
>> "%DIST%\README.txt" echo.
>> "%DIST%\README.txt" echo Install:   Register-WidgetMusic.cmd restart
>> "%DIST%\README.txt" echo Uninstall: Unregister-WidgetMusic.cmd restart

for /f "usebackq delims=" %%S in (`powershell -NoProfile -Command "$sum=(Get-ChildItem -LiteralPath '%DIST%' -File | Measure-Object Length -Sum).Sum; [math]::Round($sum/1KB,1)"`) do set "SIZEKB=%%S"

echo [Package] Created "%DIST%" (%SIZEKB% KB).
popd >nul
exit /b 0
