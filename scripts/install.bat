@echo off
setlocal enabledelayedexpansion

:: CxxIME Installer
:: Requires administrator privileges. Right-click -^> Run as administrator.
:: Usage: install.bat [data_dir]
::   data_dir defaults to %%USERPROFILE%%\cxxime

set "PRODUCT=CxxIME"
set "VERSION=0.1.0"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"

echo.
echo  ====================================================
echo         CxxIME Installer v%VERSION%
echo         Lightweight Pinyin Input Method
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

:: Locate script directory - admin elevation changes cwd to System32
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%" 2>nul
if errorlevel 1 (
    echo  ERROR: Cannot access script directory: %SCRIPT_DIR%
    echo.
    pause
    exit /b 1
)

:: Parse arguments - data directory
set "DATA_DIR=%~1"
if "%DATA_DIR%"=="" set "DATA_DIR=%USERPROFILE%\cxxime"

echo  Script dir : %SCRIPT_DIR%
echo  Data dir   : %DATA_DIR%
echo.

:: Step 1: Verify source files
echo  [1/5] Verifying source files...

if not exist "%SCRIPT_DIR%%TSF_DLL%" (
    echo  ERROR: Required file not found: %SCRIPT_DIR%%TSF_DLL%
    echo  Please run package.bat first.
    echo.
    pause
    exit /b 1
)
if not exist "%SCRIPT_DIR%%SERVER_EXE%" (
    echo  ERROR: Required file not found: %SCRIPT_DIR%%SERVER_EXE%
    echo.
    pause
    exit /b 1
)
echo         Required files found.

:: Step 2: Stop existing server
echo  [2/5] Stopping existing server...
tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    taskkill /f /im "%SERVER_EXE%" >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
    echo         Server stopped.
) else (
    echo         No running server found.
)

:: Step 3: Unregister previous TSF DLL (from any location)
echo  [3/5] Cleaning previous installation...
regsvr32 /u /s "%DATA_DIR%\%TSF_DLL%" >nul 2>&1
if not errorlevel 1 (
    echo         Previous TSF DLL unregistered.
) else (
    echo         No previous installation found.
)

:: Step 4: Install files
echo  [4/5] Installing files to %DATA_DIR%...

if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"

:: Core binaries
copy /y "%SCRIPT_DIR%%TSF_DLL%" "%DATA_DIR%\" >nul
if errorlevel 1 (
    echo  ERROR: Failed to copy %TSF_DLL%.
    pause
    exit /b 1
)
copy /y "%SCRIPT_DIR%%SERVER_EXE%" "%DATA_DIR%\" >nul
if errorlevel 1 (
    echo  ERROR: Failed to copy %SERVER_EXE%.
    pause
    exit /b 1
)

:: Config files
if exist "%SCRIPT_DIR%data\default.json" copy /y "%SCRIPT_DIR%data\default.json" "%DATA_DIR%\" >nul
if exist "%SCRIPT_DIR%data\themes.json" copy /y "%SCRIPT_DIR%data\themes.json" "%DATA_DIR%\" >nul

:: Dictionary files (binary + source)
for %%f in (pinyin wubi86) do (
    for %%e in (dict.bin dict.idx spellings.bin dict.db) do (
        if exist "%SCRIPT_DIR%data\%%f.%%e" (
            copy /y "%SCRIPT_DIR%data\%%f.%%e" "%DATA_DIR%\" >nul
        )
    )
)

echo         Files installed.

:: Step 5: Register TSF DLL and set auto-start
echo  [5/5] Registering TSF text service and configuring auto-start...

regsvr32 /s "%DATA_DIR%\%TSF_DLL%"
if errorlevel 1 (
    echo  ERROR: Failed to register %TSF_DLL%.
    echo  Try running manually:
    echo    regsvr32 "%DATA_DIR%\%TSF_DLL%"
    echo.
    pause
    exit /b 1
)
echo         TSF DLL registered.

:: Auto-start via registry
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" /t REG_SZ /d "\"%DATA_DIR%\%SERVER_EXE%\"" /f >nul 2>&1
if errorlevel 1 (
    echo         WARNING: Could not set auto-start.
) else (
    echo         Auto-start configured.
)

:: Start server
start "" "%DATA_DIR%\%SERVER_EXE%"
timeout /t 1 /nobreak >nul 2>&1

tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo         Server started.
) else (
    echo         WARNING: Server may not have started. Check Event Viewer.
)

:: Done
echo.
echo  ====================================================
echo         Installation Complete!
echo  ====================================================
echo.
echo  Data directory: %DATA_DIR%
echo.
echo  Next steps:
echo    1. Log off and log back in or restart
echo    2. Press Ctrl+Space or Win+Space to switch input method
echo    3. Type pinyin letters to start composing
echo.
echo  To uninstall: Run uninstall.bat as administrator
echo.

endlocal
pause
