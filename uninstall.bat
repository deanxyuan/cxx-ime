@echo off
setlocal enabledelayedexpansion

:: CxxIME Uninstaller
:: Requires administrator privileges. Right-click -^> Run as administrator.

set "PRODUCT=CxxIME"
set "DEFAULT_DIR=%ProgramFiles%\CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"
set "TSF_CLSID={6A4B85B0-3D02-4B3A-9E69-715725DA7706}"

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
set "INSTALL_DIR=%~1"
if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%DEFAULT_DIR%"

:: Confirmation
echo  This will completely remove %PRODUCT% from your system.
echo  Install directory: %INSTALL_DIR%
echo.
set /p "CONFIRM=  Continue? [Y/N]: "
if /i not "%CONFIRM%"=="Y" (
    echo  Cancelled.
    exit /b 0
)
echo.

set "HAD_ERRORS=0"

:: Step 1: Stop server
echo  [1/5] Stopping server...
tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    taskkill /f /im "%SERVER_EXE%" >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
    echo         Server stopped.
) else (
    echo         No running server found.
)

:: Step 2: Unregister TSF DLL
echo  [2/5] Unregistering TSF text service...
set "DLL_PATH="
if exist "%INSTALL_DIR%\bin\%TSF_DLL%" set "DLL_PATH=%INSTALL_DIR%\bin\%TSF_DLL%"
if exist "%INSTALL_DIR%\%TSF_DLL%" set "DLL_PATH=%INSTALL_DIR%\%TSF_DLL%"

if not "%DLL_PATH%"=="" (
    regsvr32 /u /s "%DLL_PATH%" >nul 2>&1
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
echo  [3/5] Removing auto-start entry...
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" >nul 2>&1
if not errorlevel 1 (
    reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" /f >nul 2>&1
    echo         Auto-start entry removed.
) else (
    echo         No auto-start entry found.
)

:: Step 4: Clean TSF registry
echo  [4/5] Cleaning TSF registry entries...
set "TIP_KEY=HKLM\SOFTWARE\Microsoft\CTF\TIP\%TSF_CLSID%"
reg query "%TIP_KEY%" >nul 2>&1
if not errorlevel 1 (
    reg delete "%TIP_KEY%" /f >nul 2>&1
    echo         TSF TIP registry entry removed.
) else (
    echo         No TSF TIP entry found.
)

:: Step 5: Remove files
echo  [5/5] Removing files...
if exist "%INSTALL_DIR%" (
    timeout /t 1 /nobreak >nul 2>&1
    rmdir /s /q "%INSTALL_DIR%" 2>nul
    if exist "%INSTALL_DIR%" (
        echo         WARNING: Could not remove all files. Some may be in use.
        echo         Restart your computer and try again, or delete manually:
        echo         %INSTALL_DIR%
        set "HAD_ERRORS=1"
    ) else (
        echo         Installation directory removed.
    )
) else (
    echo         Installation directory not found.
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
    echo    - Manually delete: %INSTALL_DIR%
) else (
    echo  ====================================================
    echo         Uninstall Complete!
    echo  ====================================================
)
echo.
echo  Please log off and log back in or restart for changes to take effect.
echo.

endlocal
pause
