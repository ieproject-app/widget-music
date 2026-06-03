@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
  for /f "delims=" %%I in ('where ISCC.exe 2^>nul') do (
    if not defined ISCC set "ISCC=%%I"
  )
)

if not defined ISCC (
  echo [Installer] Inno Setup 6 was not found.
  echo [Installer] Install Inno Setup 6 and rerun this script:
  echo [Installer] https://jrsoftware.org/isinfo.php
  popd >nul
  exit /b 1
)

call "%ROOT%\scripts\Package-WidgetMusic.cmd" %CONFIG%
if errorlevel 1 (
  echo [Installer] Package failed.
  popd >nul
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\Check-RuntimeDependencies.ps1"
if errorlevel 1 (
  echo [Installer] Runtime dependency check failed.
  popd >nul
  exit /b 1
)

if not exist "%ROOT%\installer\WidgetMusic.iss" (
  echo [Installer] Missing "%ROOT%\installer\WidgetMusic.iss".
  popd >nul
  exit /b 1
)

"%ISCC%" "%ROOT%\installer\WidgetMusic.iss"
set "ERR=%ERRORLEVEL%"
if errorlevel 1 (
  echo [Installer] Inno Setup failed.
  popd >nul
  exit /b %ERR%
)

echo [Installer] Created "%ROOT%\out\dist\SnipTune10Setup-1.0.5-x64.exe".
popd >nul
exit /b 0
