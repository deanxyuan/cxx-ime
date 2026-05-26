@echo off
setlocal enabledelayedexpansion

:: CxxIME Uninstaller
:: Requires administrator privileges. Right-click -^> Run as administrator.

set "PRODUCT=CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"
set "DEFAULT_DATA=%USERPROFILE%\cxxime"
:: CLSID from tsf/src/globals.h — must match c_clsidTextService
set "TSF_CLSID={B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

echo.
echo  ====================================================
echo         CxxIME Uninstaller
echo  ====================================================
echo.

:: Admin check
net session >nul 2>&1
if errorlevel 1 (
    echo  ERROR: Administrator privileges required.
    echo  Right-click this file and select "Run as administrator".
    echo.
    pause
    exit /b 1
)

:: Parse arguments
set "DATA_DIR=%~1"
if "%DATA_DIR%"=="" set "DATA_DIR=%DEFAULT_DATA%"

:: Confirmation
echo  This will completely remove %PRODUCT% from your system.
echo  Data directory: %DATA_DIR%
echo.
set /p "CONFIRM=  Continue? [Y/N]: "
if /i not "%CONFIRM%"=="Y" (
    echo  Cancelled.
    exit /b 0
)
echo.

set "HAD_ERRORS=0"

:: Step 1: Stop server
echo  [1/6] Stopping server...
tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    taskkill /f /im "%SERVER_EXE%" >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
    echo         Server stopped.
) else (
    echo         No running server found.
)

:: Step 2: Unregister TSF DLL
echo  [2/6] Unregistering TSF text service...
if exist "%DATA_DIR%\%TSF_DLL%" (
    regsvr32 /u /s "%DATA_DIR%\%TSF_DLL%" >nul 2>&1
    if errorlevel 1 (
        echo         WARNING: Could not unregister TSF DLL. May still be in use.
        set "HAD_ERRORS=1"
    ) else (
        echo         TSF DLL unregistered.
    )
) else (
    echo         TSF DLL not found - already removed.
)

:: Step 3: Remove auto-start
echo  [3/6] Removing auto-start entry...
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" >nul 2>&1
if not errorlevel 1 (
    reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" /f >nul 2>&1
    echo         Auto-start entry removed.
) else (
    echo         No auto-start entry found.
)

:: Step 4: Clean TSF registry (CLSID registration)
echo  [4/6] Cleaning TSF COM registration...
set "CLSID_KEY=HKLM\SOFTWARE\Classes\CLSID\%TSF_CLSID%"
reg query "%CLSID_KEY%" >nul 2>&1
if not errorlevel 1 (
    reg delete "%CLSID_KEY%" /f >nul 2>&1
    echo         CLSID registration removed.
) else (
    echo         No CLSID registration found.
)

:: Step 5: Clean TSF TIP profile
echo  [5/6] Cleaning TSF TIP registry entries...
set "TIP_KEY=HKLM\SOFTWARE\Microsoft\CTF\TIP\%TSF_CLSID%"
reg query "%TIP_KEY%" >nul 2>&1
if not errorlevel 1 (
    reg delete "%TIP_KEY%" /f >nul 2>&1
    echo         TSF TIP entry removed.
) else (
    echo         No TSF TIP entry found.
)

:: Step 6: Remove data files
echo  [6/6] Removing data files...
if exist "%DATA_DIR%" (
    timeout /t 1 /nobreak >nul 2>&1
    rmdir /s /q "%DATA_DIR%" 2>nul
    if exist "%DATA_DIR%" (
        echo         WARNING: Could not remove all files. Some may be in use.
        echo         Restart your computer and try again, or delete manually:
        echo         %DATA_DIR%
        set "HAD_ERRORS=1"
    ) else (
        echo         Data directory removed.
    )
) else (
    echo         Data directory not found.
)

:: Done
echo.
if "%HAD_ERRORS%"=="1" (
    echo  ====================================================
    echo    Uninstall completed with warnings
    echo  ====================================================
    echo.
    echo  Some steps had warnings. You may need to:
    echo    - Restart your computer to fully unload the TSF DLL
    echo    - Manually delete: %DATA_DIR%
) else (
    echo  ====================================================
    echo         Uninstall Complete!
    echo  ====================================================
)
echo.
echo  Log off and log back in (or restart) for changes to take effect.
echo.

endlocal
pause
