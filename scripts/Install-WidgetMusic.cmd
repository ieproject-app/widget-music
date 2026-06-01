@echo off
setlocal enableextensions enabledelayedexpansion

set "ROOT=%~dp0"
set "DLL=%ROOT%WidgetMusicDeskband.dll"
set "ACTION=%~1"
set "SKIP_ENABLE=%~2"
set "ENABLE_SCRIPT=%ROOT%Enable-WidgetMusicTaskbar.ps1"
set "ENABLE_WRAPPER=%ROOT%Invoke-WidgetMusicTaskbarEnable.ps1"

if not exist "%DLL%" (
  echo [Install] Deskband DLL not found next to this script: "%DLL%"
  exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

if /i "%ACTION%"=="restart" (
  "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "$sessionId = (Get-Process -Id $PID).SessionId; $killer = $null; if ($sessionId -ne 0) { $killer = Start-Job -ArgumentList $sessionId -ScriptBlock { param($sid) while ($true) { Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid } | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 100 } } }; try { Get-Process WidgetMusicHost -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sessionId } | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; & '%~f0' norestart skipenable; $code = $LASTEXITCODE } finally { if ($killer) { Stop-Job $killer -ErrorAction SilentlyContinue; Remove-Job $killer -Force -ErrorAction SilentlyContinue }; if ($sessionId -ne 0) { $explorerUp = $false; for ($i = 0; $i -lt 24; $i++) { if (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sessionId }) { $explorerUp = $true; break }; Start-Process explorer.exe -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 350 }; if (-not $explorerUp) { Start-Process explorer.exe -ErrorAction SilentlyContinue } } }; exit $code"
  set "ERR=%ERRORLEVEL%"
  if not errorlevel 1 (
    if exist "%ENABLE_SCRIPT%" (
      echo [Install] Ensuring Widget Music is shown after Explorer restart...
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
        echo [Install] Retrying after one more Explorer restart...
        "%PS%" -NoProfile -Command "$sid = (Get-Process -Id $PID).SessionId; Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid } | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; if ($sid -ne 0) { Start-Process explorer.exe -ErrorAction SilentlyContinue }" >nul 2>nul
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
          echo [Install] Auto-enable timed out waiting for taskbar confirmation. You can enable manually from Taskbar ^> Toolbars ^> Widget Music.
        ) else (
          echo [Install] Warning: could not auto-enable taskbar band after restart. You can enable it manually from Taskbar ^> Toolbars ^> Widget Music.
        )
      )
    ) else (
      echo [Install] Enable script not found; skip auto-enable after restart.
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

if /i not "%SKIP_ENABLE%"=="skipenable" (
  if exist "%ENABLE_SCRIPT%" (
    echo [Install] Enabling Widget Music on taskbar...
    call :run_enable
    set "ENABLE_EXIT=%ERRORLEVEL%"
    if "%ENABLE_EXIT%"=="2" (
      echo [Install] Auto-enable timed out waiting for taskbar confirmation. You can enable manually from Taskbar ^> Toolbars ^> Widget Music.
    ) else if errorlevel 1 (
      echo [Install] Warning: could not auto-enable taskbar band. You can enable it manually from Taskbar ^> Toolbars ^> Widget Music.
    )
  ) else (
    echo [Install] Enable script not found. Enable Widget Music manually from taskbar toolbar menu.
  )
) else (
  echo [Install] Auto-enable deferred until Explorer restart completes.
)

if /i "%ACTION%"=="restart" (
  echo [Install] Restarting Explorer...
  "%PS%" -NoProfile -Command "Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe" >nul 2>nul
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
