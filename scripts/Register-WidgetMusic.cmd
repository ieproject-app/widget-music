@echo off
setlocal enableextensions enabledelayedexpansion

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ACTION=%~2"
set "SKIP_ENABLE=%~3"

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul || exit /b 1

set "PS=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"
if exist "%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe" set "PS=%SystemRoot%\\Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe"
set "ENABLE_SCRIPT=%ROOT%\scripts\Enable-WidgetMusicTaskbar.ps1"
set "ENABLE_WRAPPER=%ROOT%\scripts\Invoke-WidgetMusicTaskbarEnable.ps1"

if /i "%ACTION%"=="restart" (
  "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "$killer = Start-Job -ScriptBlock { while ($true) { Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 100 } }; try { Stop-Process -Name WidgetMusicHost -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; & '%~f0' '%CONFIG%' norestart skipenable; $code = $LASTEXITCODE } finally { Stop-Job $killer -ErrorAction SilentlyContinue; Remove-Job $killer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe }; exit $code"
  set "ERR=%ERRORLEVEL%"
  if not errorlevel 1 (
    echo [Register] Ensuring Widget Music is shown after Explorer restart...
    set "ENABLE_OK="
    set "ENABLE_TIMED_OUT="
    for /l %%I in (1,1,10) do (
      call :run_enable
      set "ENABLE_EXIT=!ERRORLEVEL!"
      if "!ENABLE_EXIT!"=="0" (
        set "ENABLE_OK=1"
        goto :after_restart_enable
      )
      if "!ENABLE_EXIT!"=="2" (
        set "ENABLE_TIMED_OUT=1"
        goto :after_restart_enable
      )
      "%PS%" -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>nul
    )

    if not defined ENABLE_OK if not defined ENABLE_TIMED_OUT (
      echo [Register] Retrying after one more Explorer restart...
      "%PS%" -NoProfile -Command "Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; Start-Process explorer.exe" >nul 2>nul
      "%PS%" -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>nul
      for /l %%I in (1,1,10) do (
        call :run_enable
        set "ENABLE_EXIT=!ERRORLEVEL!"
        if "!ENABLE_EXIT!"=="0" (
          set "ENABLE_OK=1"
          goto :after_restart_enable
        )
        if "!ENABLE_EXIT!"=="2" (
          set "ENABLE_TIMED_OUT=1"
          goto :after_restart_enable
        )
        "%PS%" -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>nul
      )
    )
:after_restart_enable
    if not defined ENABLE_OK (
      if defined ENABLE_TIMED_OUT (
        echo [Register] Auto-enable timed out waiting for taskbar confirmation. You can enable manually from Taskbar ^> Toolbars ^> Widget Music.
      ) else (
        echo [Register] Warning: could not auto-enable taskbar band after restart. You can enable it manually from Taskbar ^> Toolbars ^> Widget Music.
      )
    )
  )
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

echo [Register] Registering "%DLL%" (per-user)...
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
  echo [Register] regsvr32 failed.
  popd >nul
  exit /b 1
)

if /i not "%SKIP_ENABLE%"=="skipenable" (
  echo [Register] Enabling Widget Music on taskbar...
  call :run_enable
  set "ENABLE_EXIT=%ERRORLEVEL%"
  if "%ENABLE_EXIT%"=="2" (
    echo [Register] Auto-enable timed out waiting for taskbar confirmation. You can enable manually from Taskbar ^> Toolbars ^> Widget Music.
  ) else if errorlevel 1 (
    echo [Register] Warning: could not auto-enable taskbar band. You can enable it manually from Taskbar ^> Toolbars ^> Widget Music.
  )
) else (
  echo [Register] Auto-enable deferred until Explorer restart completes.
)

if /i "%ACTION%"=="restart" (
  echo [Register] Restarting Explorer...
  "%PS%" -NoProfile -Command "Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe" >nul 2>nul
)

echo [Register] Done.
popd >nul
exit /b 0

:run_enable
if exist "%ENABLE_WRAPPER%" (
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ENABLE_WRAPPER%" -EnableScriptPath "%ENABLE_SCRIPT%" -TimeoutSeconds 8
  exit /b %ERRORLEVEL%
)
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ENABLE_SCRIPT%"
exit /b %ERRORLEVEL%
