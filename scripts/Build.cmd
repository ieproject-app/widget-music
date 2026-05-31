@echo off
setlocal enableextensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

rem Normalize PATH casing before calling VS/MSBuild. Some shells inject both PATH and Path,
rem which trips MSBuild when it copies the environment for CL.exe. Preserve the value first,
rem because clearing the duplicate Path entry can otherwise hide System32 tools such as findstr.
set "__WIDGETMUSIC_PATH=%PATH%"
set "Path="
set "PATH=%__WIDGETMUSIC_PATH%"
set "__WIDGETMUSIC_PATH="

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vswhere

set "TMP1=%TEMP%\WidgetMusic_vsinstall.txt"
set "TMP2=%TEMP%\WidgetMusic_msbuild.txt"
del /q "%TMP1%" "%TMP2%" 2>nul

"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TMP1%"
set "VSINSTALL="
set /p VSINSTALL=<"%TMP1%"
if "%VSINSTALL%"=="" goto :no_vs

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" goto :no_vs

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 goto :no_vs

"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe > "%TMP2%"
set "MSBUILD="
set /p MSBUILD=<"%TMP2%"
if "%MSBUILD%"=="" goto :no_msbuild

"%MSBUILD%" "%ROOT%\WidgetMusic.sln" /m /t:Build /p:Configuration=%CONFIG% /p:Platform=x64
set "ERR=%ERRORLEVEL%"

del /q "%TMP1%" "%TMP2%" 2>nul
popd >nul
exit /b %ERR%

:no_vswhere
echo [Build] vswhere.exe not found at "%VSWHERE%".
echo Install Visual Studio Build Tools 2022 (Desktop development with C++).
popd >nul
exit /b 1

:no_vs
echo [Build] Visual Studio Build Tools not found or could not initialize VS environment.
popd >nul
exit /b 1

:no_msbuild
echo [Build] MSBuild.exe not found via vswhere.
popd >nul
exit /b 1
