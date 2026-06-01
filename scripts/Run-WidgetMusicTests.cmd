@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "ROOT=%~dp0.."
set "TESTS=%ROOT%\out\%CONFIG%\x64\WidgetMusicTests.exe"

if not exist "%TESTS%" (
  echo [Tests] Missing "%TESTS%". Build the solution first.
  exit /b 1
)

"%TESTS%"
exit /b %ERRORLEVEL%
