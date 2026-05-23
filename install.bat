@echo off
setlocal enabledelayedexpansion

:: ── CxxIME Installer ─────────────────────────────────────────────────────────
:: Requires administrator privileges. Right-click -> Run as administrator.
:: Usage: install.bat [install_dir]

:: ── Configuration ────────────────────────────────────────────────────────────
set "PRODUCT=CxxIME"
set "VERSION=0.1.0"
set "DEFAULT_DIR=%ProgramFiles%\CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"
set "TEST_EXE=cxxime-test.exe"
set "LOG_FILE=%TEMP%\cxxime_install.log"

:: ── Banner ───────────────────────────────────────────────────────────────────
echo.
echo  ====================================================
echo         CxxIME Installer v%VERSION%
echo         Lightweight Pinyin Input Method
echo  ====================================================
echo.

:: ── Admin check ──────────────────────────────────────────────────────────────
net session >nul 2>&1
if errorlevel 1 (
    echo  ERROR: Administrator privileges required.
    echo  Right-click this file and select "Run as administrator".
    echo.
    pause
    exit /b 1
)

:: ── Locate script directory ──────────────────────────────────────────────────
:: Admin elevation may change cwd to System32, so we must use %~dp0
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%" 2>nul
if errorlevel 1 (
    echo  ERROR: Cannot access script directory: %SCRIPT_DIR%
    echo.
    pause
    exit /b 1
)

:: ── Parse arguments ──────────────────────────────────────────────────────────
set "INSTALL_DIR=%~1"
if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%DEFAULT_DIR%"

set "BIN_DIR=%SCRIPT_DIR%bin"
set "DATA_DIR=%SCRIPT_DIR%data"

:: ── Diagnostic output ───────────────────────────────────────────────────────
echo  Script dir  : %SCRIPT_DIR%
echo  Install dir : %INSTALL_DIR%
echo  Log file    : %LOG_FILE%
echo.

echo [%date% %time%] CxxIME Installer v%VERSION% > "%LOG_FILE%"
echo [%date% %time%] Script dir: %SCRIPT_DIR% >> "%LOG_FILE%"
echo [%date% %time%] Install dir: %INSTALL_DIR% >> "%LOG_FILE%"

:: ── Step 1: Verify source files ─────────────────────────────────────────────
echo  [1/6] Verifying source files...
set "MISSING="
if not exist "%BIN_DIR%\%TSF_DLL%" set "MISSING=%BIN_DIR%\%TSF_DLL%"
if not exist "%BIN_DIR%\%SERVER_EXE%" set "MISSING=%BIN_DIR%\%SERVER_EXE%"
if not "%MISSING%"=="" (
    echo  ERROR: Required file not found: %MISSING%
    echo [%date% %time%] ERROR: Missing file: %MISSING% >> "%LOG_FILE%"
    echo.
    echo  Files in %BIN_DIR%:
    dir /b "%BIN_DIR%" 2>nul
    echo.
    echo  Please run package.bat first or ensure you extracted the full zip.
    echo.
    pause
    exit /b 1
)
echo         All required files found.
echo [%date% %time%] Source files verified >> "%LOG_FILE%"

:: ── Step 2: Stop existing server ─────────────────────────────────────────────
echo  [2/6] Stopping existing server...
tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    taskkill /f /im "%SERVER_EXE%" >nul 2>&1
    timeout /t 1 /nobreak >nul 2>&1
    echo         Server stopped.
) else (
    echo         No running server found.
)

:: ── Step 3: Unregister previous TSF DLL ─────────────────────────────────────
echo  [3/6] Cleaning previous installation...
if exist "%INSTALL_DIR%\bin\%TSF_DLL%" (
    regsvr32 /u /s "%INSTALL_DIR%\bin\%TSF_DLL%" >nul 2>&1
    echo         Previous TSF DLL unregistered.
) else if exist "%INSTALL_DIR%\%TSF_DLL%" (
    regsvr32 /u /s "%INSTALL_DIR%\%TSF_DLL%" >nul 2>&1
    echo         Previous TSF DLL unregistered.
) else (
    echo         No previous installation found.
)

:: ── Step 4: Install files ───────────────────────────────────────────────────
echo  [4/6] Installing files to %INSTALL_DIR%...

if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin"
if not exist "%INSTALL_DIR%\data" mkdir "%INSTALL_DIR%\data"

copy /y "%BIN_DIR%\%TSF_DLL%" "%INSTALL_DIR%\bin\" >nul
if errorlevel 1 (
    echo  ERROR: Failed to copy %TSF_DLL%.
    echo [%date% %time%] ERROR: Failed to copy %TSF_DLL% >> "%LOG_FILE%"
    pause
    exit /b 1
)

copy /y "%BIN_DIR%\%SERVER_EXE%" "%INSTALL_DIR%\bin\" >nul
if errorlevel 1 (
    echo  ERROR: Failed to copy %SERVER_EXE%.
    echo [%date% %time%] ERROR: Failed to copy %SERVER_EXE% >> "%LOG_FILE%"
    pause
    exit /b 1
)

copy /y "%BIN_DIR%\%TEST_EXE%" "%INSTALL_DIR%\bin\" >nul 2>&1

if exist "%DATA_DIR%\default.json" copy /y "%DATA_DIR%\default.json" "%INSTALL_DIR%\data\" >nul
if exist "%DATA_DIR%\pinyin.dict.db" (
    copy /y "%DATA_DIR%\pinyin.dict.db" "%INSTALL_DIR%\data\" >nul
    echo         Dictionary installed.
) else (
    echo         WARNING: pinyin.dict.db not found. IME requires a dictionary.
)
echo         Files installed.
echo [%date% %time%] Files installed to %INSTALL_DIR% >> "%LOG_FILE%"

:: ── Step 5: Register TSF DLL ────────────────────────────────────────────────
echo  [5/6] Registering TSF text service...
regsvr32 /s "%INSTALL_DIR%\bin\%TSF_DLL%"
if errorlevel 1 (
    echo  ERROR: Failed to register %TSF_DLL%.
    echo  The IME will not appear in the input method list.
    echo [%date% %time%] ERROR: regsvr32 failed for %TSF_DLL% >> "%LOG_FILE%"
    echo.
    echo  Try running manually:
    echo    regsvr32 "%INSTALL_DIR%\bin\%TSF_DLL%"
    echo.
    pause
    exit /b 1
)
echo         TSF DLL registered.
echo [%date% %time%] TSF DLL registered >> "%LOG_FILE%"

:: ── Step 6: Configure auto-start and start server ───────────────────────────
echo  [6/6] Configuring auto-start and starting server...

reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" /t REG_SZ /d "\"%INSTALL_DIR%\bin\%SERVER_EXE%\"" /f >nul 2>&1
if errorlevel 1 (
    echo         WARNING: Could not set auto-start. Start server manually.
) else (
    echo         Auto-start configured.
)

start "" "%INSTALL_DIR%\bin\%SERVER_EXE%"
timeout /t 1 /nobreak >nul 2>&1

tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo         Server started.
    echo [%date% %time%] Server started >> "%LOG_FILE%"
) else (
    echo         WARNING: Server may not have started. Check Event Viewer.
    echo [%date% %time%] WARNING: Server may not have started >> "%LOG_FILE%"
)

:: ── Done ─────────────────────────────────────────────────────────────────────
echo.
echo  ====================================================
echo         Installation Complete!
echo  ====================================================
echo.
echo  Installed to: %INSTALL_DIR%
echo.
echo  Next steps:
echo    1. Log off and log back in (or restart)
echo    2. Press Ctrl+Space or Win+Space to switch input method
echo    3. Type pinyin letters to start composing
echo.
echo  To uninstall: Run uninstall.bat as administrator
echo  Install log:  %LOG_FILE%
echo.

endlocal
pause
