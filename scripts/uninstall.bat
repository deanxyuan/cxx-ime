@echo off
setlocal enabledelayedexpansion

:: CxxIME Uninstaller
:: Requires administrator privileges. Right-click -> Run as administrator.

set "PRODUCT=CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"
set "DEFAULT_DATA=%USERPROFILE%\cxxime"
:: CLSID from tsf/src/globals.h — must match c_clsidTextService
set "TSF_CLSID={B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

echo.
echo  ====================================================
echo        CxxIME Uninstaller
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

:: Step 1: Deactivate input method — switch system default to English
echo  [1/8] Deactivating input method...
:: Remove CxxIME from the language profile list so TSF unloads it from all processes
reg add "HKCU\Keyboard Layout\Preload" /v 1 /t REG_SZ /d 00000409 /f >nul 2>&1
:: Also try to remove CxxIME from Substitutes if it was registered there
reg delete "HKCU\Keyboard Layout\Substitutes" /v 00000409 /f >nul 2>&1
echo         System keyboard set to English.

:: Step 2: Stop server
echo  [2/8] Stopping server...
tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    taskkill /im "%SERVER_EXE%" >nul 2>&1
    timeout /t 2 /nobreak >nul 2>&1
    tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
    if not errorlevel 1 (
        taskkill /f /im "%SERVER_EXE%" >nul 2>&1
    )
    echo         Server stopped.
) else (
    echo         No running server found.
)

:: Step 3: Unregister TSF DLL
echo  [3/8] Unregistering TSF text service...
if exist "%DATA_DIR%\%TSF_DLL%" (
    %SystemRoot%\System32\regsvr32 /u /s "%DATA_DIR%\%TSF_DLL%" >nul 2>&1
    if errorlevel 1 (
        echo         WARNING: Could not unregister TSF DLL. May still be in use.
        set "HAD_ERRORS=1"
    ) else (
        echo         TSF DLL unregistered.
    )
) else (
    echo         TSF DLL not found - already removed.
)

:: Step 4: Remove auto-start
echo  [4/8] Removing auto-start entry...
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" >nul 2>&1
if not errorlevel 1 (
    reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" /f >nul 2>&1
    echo         Auto-start entry removed.
) else (
    echo         No auto-start entry found.
)

:: Step 5: Clean TSF registry (CLSID registration)
echo  [5/8] Cleaning TSF COM registration...
set "CLSID_KEY=HKLM\SOFTWARE\Classes\CLSID\%TSF_CLSID%"
reg query "%CLSID_KEY%" >nul 2>&1
if not errorlevel 1 (
    reg delete "%CLSID_KEY%" /f >nul 2>&1
    echo         CLSID registration removed.
) else (
    echo         No CLSID registration found.
)

:: Step 6: Clean TSF TIP profile
echo  [6/8] Cleaning TSF TIP registry entries...
set "TIP_KEY=HKLM\SOFTWARE\Microsoft\CTF\TIP\%TSF_CLSID%"
reg query "%TIP_KEY%" >nul 2>&1
if not errorlevel 1 (
    reg delete "%TIP_KEY%" /f >nul 2>&1
    echo         TSF TIP entry removed.
) else (
    echo         No TSF TIP entry found.
)

:: Step 7: Clean Add/Remove Programs entry
echo  [7/8] Cleaning Add/Remove Programs entry...
set "UNINSTALL_KEY=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME"
reg query "%UNINSTALL_KEY%" >nul 2>&1
if not errorlevel 1 (
    reg delete "%UNINSTALL_KEY%" /f >nul 2>&1
    echo         Uninstall entry removed.
) else (
    echo         No uninstall entry found.
)

:: Step 8: Remove data files
echo  [8/8] Removing data files...
if exist "%DATA_DIR%" (
    :: Try to delete DLL first; if locked, rename and schedule for reboot cleanup
    if exist "%DATA_DIR%\%TSF_DLL%" (
        del /f "%DATA_DIR%\%TSF_DLL%" >nul 2>&1
        if exist "%DATA_DIR%\%TSF_DLL%" (
            :: DLL is locked by a process — rename to .pending and schedule deletion
            move /y "%DATA_DIR%\%TSF_DLL%" "%DATA_DIR%\%TSF_DLL%.pending" >nul 2>&1
            if exist "%DATA_DIR%\%TSF_DLL%.pending" (
                echo         DLL locked by running processes. Scheduled for cleanup on reboot.
                set "HAD_ERRORS=1"
                set "NEEDS_REBOOT=1"
            )
        )
    )
    timeout /t 1 /nobreak >nul 2>&1
    rmdir /s /q "%DATA_DIR%" 2>nul
    if exist "%DATA_DIR%" (
        :: Directory still exists (likely because of .pending file)
        :: Clean up everything except the .pending file
        for %%f in ("%DATA_DIR%\*") do (
            if /i not "%%~nxf"=="%TSF_DLL%.pending" del /f "%%f" >nul 2>&1
        )
        for /d %%d in ("%DATA_DIR%\*") do rmdir /s /q "%%d" >nul 2>&1
        echo         Some files scheduled for cleanup on reboot.
        echo         Remaining: %DATA_DIR%
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
    if "%NEEDS_REBOOT%"=="1" (
        echo  The TSF DLL is still loaded by running applications.
        echo  It will be removed automatically after reboot.
        echo.
    )
    echo  Please restart your computer to complete uninstallation.
) else (
    echo  ====================================================
    echo        Uninstall Complete!
    echo  ====================================================
)
echo.
echo  Log off and log back in (or restart) for changes to take effect.
echo.

endlocal
pause
