@echo off
setlocal enabledelayedexpansion

:: CxxIME Dev Uninstaller
:: Stops the server, unregisters the DLL (from registry or same directory).
::
:: Usage: run as admin.

set "REG_KEY=HKLM\SOFTWARE\CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"

echo === CxxIME Uninstaller ===
echo.

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Administrator privileges required.
    pause
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%" 2>nul

:: 1. Stop server
echo [1/3] Stopping server...
taskkill /im "%SERVER_EXE%" /f >nul 2>&1
timeout /t 1 /nobreak >nul 2>&1
echo       Done.

:: 2. Unregister DLL — try registry path first, then same-directory
echo [2/3] Unregistering DLL...
set "UNREG_DONE=0"
for /f "tokens=2*" %%a in ('reg query "%REG_KEY%" /v DllPath 2^>nul ^| findstr /i DllPath') do (
    if exist "%%b" (
        regsvr32 /u /s "%%b" >nul 2>&1
        echo       Unregistered: %%b
        set "UNREG_DONE=1"
    )
)
if "%UNREG_DONE%"=="0" (
    if exist "%SCRIPT_DIR%%TSF_DLL%" (
        regsvr32 /u /s "%SCRIPT_DIR%%TSF_DLL%" >nul 2>&1
        echo       Unregistered: %SCRIPT_DIR%%TSF_DLL%
    ) else (
        echo       No DLL found — already clean.
    )
)
reg delete "%REG_KEY%" /v DllPath /f >nul 2>&1
reg delete "%REG_KEY%" /f >nul 2>&1

:: 3. Clean auto-start
echo [3/3] Cleaning auto-start...
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\CLSID\{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\CTF\TIP\{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}" /f >nul 2>&1
echo       Done.

echo.
echo === Done ===
echo.
endlocal
pause
