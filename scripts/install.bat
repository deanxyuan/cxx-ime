@echo off
setlocal enabledelayedexpansion

:: CxxIME Dev Installer
:: Run from scripts/ — finds DLL in build/tsf/, server in build/server/.
:: Also works if placed next to the DLL (same-directory fallback).

set "REG_KEY=HKLM\SOFTWARE\CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"

echo === CxxIME Installer ===
echo.

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Administrator privileges required.
    pause
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"

:: ---- Find DLL ----
set "DLL="
if exist "%SCRIPT_DIR%%TSF_DLL%" set "DLL=%SCRIPT_DIR%%TSF_DLL%"
for %%c in (Release Debug) do (
    if not defined DLL if exist "%SCRIPT_DIR%..\build\tsf\%%c\%TSF_DLL%" set "DLL=%SCRIPT_DIR%..\build\tsf\%%c\%TSF_DLL%"
)

if not defined DLL (
    echo ERROR: %TSF_DLL% not found.
    echo Tried: %SCRIPT_DIR%
    echo        %SCRIPT_DIR%..\build\tsf\{Release,Debug}\
    pause
    exit /b 1
)

for %%p in ("%DLL%") do set "DLL=%%~fp"

:: ---- Find Server ----
set "SERVER="
if exist "%SCRIPT_DIR%%SERVER_EXE%" set "SERVER=%SCRIPT_DIR%%SERVER_EXE%"
for %%c in (Release Debug) do (
    if not defined SERVER if exist "%SCRIPT_DIR%..\build\server\%%c\%SERVER_EXE%" set "SERVER=%SCRIPT_DIR%..\build\server\%%c\%SERVER_EXE%"
)
if not defined SERVER (
    echo WARNING: %SERVER_EXE% not found, server will not be started.
    echo Tried: %SCRIPT_DIR%
    echo        %SCRIPT_DIR%..\build\server\{Release,Debug}\
) else (
    for %%p in ("%SERVER%") do set "SERVER=%%~fp"
)

echo DLL   : %DLL%
echo Server: %SERVER%
echo.

:: 1. Unregister old DLL
echo [1/3] Unregister old DLL...
for /f "tokens=2*" %%a in ('reg query "%REG_KEY%" /v DllPath 2^>nul ^| findstr /i DllPath') do (
    if exist "%%b" (
        regsvr32 /u /s "%%b" >nul 2>&1
        echo       Unregistered: %%b
    )
)
echo       Done.

:: 2. Register new DLL
echo [2/3] Register %DLL%...
regsvr32 /s "%DLL%"
if errorlevel 1 (
    echo ERROR: regsvr32 failed.
    pause
    exit /b 1
)
reg add "%REG_KEY%" /v DllPath /t REG_SZ /d "%DLL%" /f >nul 2>&1
echo       Registered.

:: 3. Start server (only if not running)
echo [3/3] Start server...
if not defined SERVER (
    echo       SKIP — server not found.
) else (
    tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
    if not errorlevel 1 (
        echo       Already running.
    ) else (
        start "" "%SERVER%"
        timeout /t 1 /nobreak >nul 2>&1
        echo       Started.
    )
)

echo.
echo === Done ===
echo.
endlocal
pause
