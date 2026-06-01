@echo off
setlocal enableextensions enabledelayedexpansion

set "ROOT=%~dp0"
set "DLL=%ROOT%WidgetMusicDeskband.dll"
set "ACTION=%~1"
set "ENABLE_MODE=%~2"
set "FORWARD_ENABLE_MODE=%~3"
set "INTERNAL_SKIP="
if /i "%ENABLE_MODE%"=="skipenable" (
  set "INTERNAL_SKIP=1"
  set "ENABLE_MODE=%FORWARD_ENABLE_MODE%"
)
set "AUTO_ENABLE="
if /i "%ENABLE_MODE%"=="auto" set "AUTO_ENABLE=1"
if /i "%ENABLE_MODE%"=="enable" set "AUTO_ENABLE=1"
set "ENABLE_SCRIPT=%ROOT%Enable-WidgetMusicTaskbar.ps1"
set "ENABLE_WRAPPER=%ROOT%Invoke-WidgetMusicTaskbarEnable.ps1"
set "RESTART_HELPER=%ROOT%Restart-WidgetMusicExplorer.ps1"

if not exist "%DLL%" (
  echo [Install] Deskband DLL not found next to this script: "%DLL%"
  exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

if /i "%ACTION%"=="restart" (
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -RegistrationCommand "%~f0" -RegistrationArgumentsText "norestart|skipenable|%ENABLE_MODE%" -StopHost
  set "ERR=%ERRORLEVEL%"
  if not errorlevel 1 (
    if defined AUTO_ENABLE (
      if exist "%ENABLE_SCRIPT%" (
        echo [Install] Ensuring SnipTune 10 is shown after Explorer restart...
        set "ENABLE_OK="
        set "ENABLE_TIMED_OUT="
        for /l %%I in (1,1,10) do (
          if not defined ENABLE_OK if not defined ENABLE_TIMED_OUT (
            call :run_enable
            set "ENABLE_EXIT=!ERRORLEVEL!"
            if "!ENABLE_EXIT!"=="0" set "ENABLE_OK=1"
            if "!ENABLE_EXIT!"=="2" set "ENABLE_TIMED_OUT=1"
            if not defined ENABLE_OK if not defined ENABLE_TIMED_OUT (
              "%PS%" -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>nul
            )
          )
        )

        if not defined ENABLE_OK if not defined ENABLE_TIMED_OUT (
          echo [Install] Retrying after one more Explorer restart...
          "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -StopHost >nul 2>nul
          "%PS%" -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>nul
          for /l %%I in (1,1,10) do (
            if not defined ENABLE_OK if not defined ENABLE_TIMED_OUT (
              call :run_enable
              set "ENABLE_EXIT=!ERRORLEVEL!"
              if "!ENABLE_EXIT!"=="0" set "ENABLE_OK=1"
              if "!ENABLE_EXIT!"=="2" set "ENABLE_TIMED_OUT=1"
              if not defined ENABLE_OK if not defined ENABLE_TIMED_OUT (
                "%PS%" -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>nul
              )
            )
          )
        )
        if not defined ENABLE_OK (
          if defined ENABLE_TIMED_OUT (
            echo [Install] Auto-enable timed out waiting for taskbar confirmation. You can enable manually from Taskbar ^> Toolbars ^> SnipTune 10.
          ) else (
            echo [Install] Warning: could not auto-enable taskbar band after restart. You can enable it manually from Taskbar ^> Toolbars ^> SnipTune 10.
          )
        )
      ) else (
        echo [Install] Enable script not found; skip auto-enable after restart.
      )
    ) else (
      echo [Install] Auto-enable not requested. Enable manually from Taskbar ^> Toolbars ^> SnipTune 10.
    )
  )
  exit /b %ERR%
)

echo [Install] Registering "%DLL%"...
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
  echo [Install] regsvr32 failed.
  exit /b 1
)

if defined INTERNAL_SKIP (
  echo [Install] Auto-enable deferred until Explorer restart completes.
) else if defined AUTO_ENABLE (
  if exist "%ENABLE_SCRIPT%" (
    echo [Install] Enabling SnipTune 10 on taskbar...
    call :run_enable
    set "ENABLE_EXIT=%ERRORLEVEL%"
    if "%ENABLE_EXIT%"=="2" (
      echo [Install] Auto-enable timed out waiting for taskbar confirmation. You can enable manually from Taskbar ^> Toolbars ^> SnipTune 10.
    ) else if errorlevel 1 (
      echo [Install] Warning: could not auto-enable taskbar band. You can enable it manually from Taskbar ^> Toolbars ^> SnipTune 10.
    )
  ) else (
    echo [Install] Enable script not found. Enable SnipTune 10 manually from taskbar toolbar menu.
  )
) else (
  echo [Install] Auto-enable not requested. Enable manually from Taskbar ^> Toolbars ^> SnipTune 10.
)

if /i "%ACTION%"=="restart" (
  echo [Install] Restarting Explorer...
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%RESTART_HELPER%" -StopHost >nul 2>nul
)

echo [Install] Done.
exit /b 0

:run_enable
if exist "%ENABLE_WRAPPER%" (
  "%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ENABLE_WRAPPER%" -EnableScriptPath "%ENABLE_SCRIPT%" -TimeoutSeconds 8
  exit /b %ERRORLEVEL%
)
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ENABLE_SCRIPT%"
exit /b %ERRORLEVEL%
