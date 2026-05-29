@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0.
set ROOT=%SCRIPT_DIR%\..
set SCRIPT_DIR=%~dp0
set BUILD_DIR=%ROOT%\build
set DIST_DIR=%ROOT%\dist
set OUTPUT_DIR=%ROOT%\..\output
set CONFIG=Release
set VERSION=0.1.0

if "%1"=="debug" set CONFIG=Debug
if "%1"=="Debug" set CONFIG=Debug

echo === CxxIME Packager v%VERSION% (%CONFIG%) ===
echo.

:: Check build exists
if not exist "%BUILD_DIR%\tsf\%CONFIG%\cxxime_tsf.dll" (
    echo Build artifacts not found. Building %CONFIG% first...
    if "%CONFIG%"=="Debug" (
        call "%ROOT%\build.bat" debug
    ) else (
        call "%ROOT%\build.bat" release
    )
    if errorlevel 1 (
        echo ERROR: Build failed. Cannot package.
        exit /b 1
    )
)

:: Clean dist directory
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\data"

echo [1/6] Copying binaries...
copy /y "%BUILD_DIR%\tsf\%CONFIG%\cxxime_tsf.dll" "%DIST_DIR%\" >nul
copy /y "%BUILD_DIR%\server\%CONFIG%\cxxime-server.exe" "%DIST_DIR%\" >nul
copy /y "%BUILD_DIR%\settings\%CONFIG%\cxxime-settings.exe" "%DIST_DIR%\" >nul

echo [2/6] Copying config and themes...
copy /y "%ROOT%\data\default.json" "%DIST_DIR%\data\" >nul
if exist "%ROOT%\data\themes.json" (
    copy /y "%ROOT%\data\themes.json" "%DIST_DIR%\data\" >nul
    echo        themes.json included.
) else (
    echo        WARNING: themes.json not found.
)

echo [3/6] Generating dictionary binaries...
set "HAS_DICT=0"

:: Pinyin dictionary — build_binary.py auto-extracts .zip if .db not present
set "PINYIN_SRC=%ROOT%\data\pinyin.dict.db"
if not exist "%PINYIN_SRC%" set "PINYIN_SRC=%ROOT%\data\pinyin.dict.db.zip"
if exist "%PINYIN_SRC%" (
    set "HAS_DICT=1"
    echo        Processing pinyin dictionary...
    python "%ROOT%\data\tools\build_binary.py" --input "%PINYIN_SRC%" --output "%DIST_DIR%\data\pinyin" 2>&1
    if exist "%DIST_DIR%\data\pinyin.dict.bin" (
        echo        pinyin.dict.bin generated.
    ) else (
        echo        WARNING: Failed to generate pinyin binary dict.
    )
) else (
    echo        WARNING: pinyin.dict.db.zip not found. Cannot build pinyin dictionary.
)

:: Wubi dictionary — same auto-extract logic
set "WUBI_SRC=%ROOT%\data\wubi86.dict.db"
if not exist "%WUBI_SRC%" set "WUBI_SRC=%ROOT%\data\wubi86.dict.db.zip"
if exist "%WUBI_SRC%" (
    set "HAS_DICT=1"
    echo        Processing wubi86 dictionary...
    python "%ROOT%\data\tools\build_binary.py" --input "%WUBI_SRC%" --output "%DIST_DIR%\data\wubi86" --skip-idx 2>&1
    if exist "%DIST_DIR%\data\wubi86.dict.bin" (
        echo        wubi86.dict.bin generated.
    ) else (
        echo        WARNING: Failed to generate wubi86 binary dict.
    )
)

if "!HAS_DICT!"=="0" (
    echo        ERROR: No dictionary source found. Need .dict.db or .dict.db.zip in data/.
    echo        The IME will not function without a dictionary.
)

echo [4/6] Copying installer scripts...
copy /y "%SCRIPT_DIR%install.bat" "%DIST_DIR%\" >nul
copy /y "%SCRIPT_DIR%uninstall.bat" "%DIST_DIR%\" >nul
copy /y "%SCRIPT_DIR%install.ps1" "%DIST_DIR%\" >nul 2>&1
copy /y "%SCRIPT_DIR%uninstall.ps1" "%DIST_DIR%\" >nul 2>&1

echo [5/6] Building NSIS installer...
copy /y "%SCRIPT_DIR%cxxime-setup.nsi" "%DIST_DIR%\" >nul
copy /y "%ROOT%\license.txt" "%DIST_DIR%\" >nul

:: Locate makensis: try PATH first, then registry
set "MAKENSIS="
where makensis >nul 2>&1
if not errorlevel 1 set "MAKENSIS=makensis"
if not defined MAKENSIS (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\NSIS" /ve 2^>nul ^| findstr /i "REG_SZ"') do (
        if exist "%%b\makensis.exe" set "MAKENSIS=%%b\makensis.exe"
    )
    if not defined MAKENSIS (
        for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\NSIS" /ve 2^>nul ^| findstr /i "REG_SZ"') do (
            if exist "%%b\makensis.exe" set "MAKENSIS=%%b\makensis.exe"
        )
    )
)

if defined MAKENSIS (
    echo        Using NSIS: !MAKENSIS!
    pushd "%DIST_DIR%"
    "!MAKENSIS!" cxxime-setup.nsi
    if not errorlevel 1 (
        if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
        move /y "cxxime-v%VERSION%-setup.exe" "%OUTPUT_DIR%" >nul
        echo        Installer created: %OUTPUT_DIR%\cxxime-v%VERSION%-setup.exe
    ) else (
        echo        ERROR: NSIS compilation failed.
    )
    popd
) else (
    echo        WARNING: makensis not found. Install NSIS 3.x or add it to PATH.
    echo        Distribution files are in: %DIST_DIR%
)

echo.
echo === Packaging complete ===
echo.
echo Distribution contents:
echo   %DIST_DIR%\
echo     cxxime_tsf.dll         TSF text service DLL
echo     cxxime-server.exe      Background server process
echo     cxxime-settings.exe    Configuration editor
echo     data\
echo       default.json         Default configuration
echo       themes.json          Color themes (14 schemes^)
echo       pinyin.dict.bin      Pinyin binary dictionary (runtime^)
echo       pinyin.dict.idx      Pinyin syllable index (runtime^)
echo       pinyin.spellings.bin Pinyin spelling trie (runtime^)
echo       wubi86.dict.bin      Wubi binary dictionary (if available^)
echo     install.bat            Installer (run as administrator^)
echo     uninstall.bat          Uninstaller (run as administrator^)
echo     install.ps1            PowerShell installer (optional^)
echo     uninstall.ps1          PowerShell uninstaller (optional^)

endlocal
