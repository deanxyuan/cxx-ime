@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set ROOT=%SCRIPT_DIR%..
set BUILD_DIR=%ROOT%\build
set DIST_DIR=%ROOT%\dist
set CONFIG=Release
set VERSION=0.1.0

if "%1"=="debug" set CONFIG=Debug
if "%1"=="Debug" set CONFIG=Debug

echo === CxxIME Packager v%VERSION% (%CONFIG%) ===
echo.

:: Check build exists
if not exist "%BUILD_DIR%\tsf\%CONFIG%\cxxime_tsf.dll" (
    echo Build artifacts not found. Building %CONFIG% first...
    call "%ROOT%build.bat" %1
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
copy /y "%ROOT%data\default.json" "%DIST_DIR%\data\" >nul
if exist "%ROOT%data\themes.json" (
    copy /y "%ROOT%data\themes.json" "%DIST_DIR%\data\" >nul
    echo        themes.json included.
) else (
    echo        WARNING: themes.json not found.
)

echo [3/6] Generating dictionary binaries...
set "HAS_DICT=0"

:: Pinyin dictionary
if exist "%ROOT%data\pinyin.dict.db" (
    set "HAS_DICT=1"
    echo        Processing pinyin dictionary...
    python "%ROOT%data\tools\build_binary.py" --input "%ROOT%data\pinyin.dict.db" --output "%DIST_DIR%\data\pinyin" 2>&1
    if exist "%DIST_DIR%\data\pinyin.dict.bin" (
        echo        pinyin.dict.bin generated.
    ) else (
        echo        WARNING: Failed to generate pinyin binary dict.
    )
    :: Copy the source .db for user rebuilds
    copy /y "%ROOT%data\pinyin.dict.db" "%DIST_DIR%\data\" >nul
) else (
    echo        WARNING: pinyin.dict.db not found. Unzip pinyin.dict.db.zip first.
)

:: Wubi dictionary
if exist "%ROOT%data\wubi86.dict.db" (
    set "HAS_DICT=1"
    echo        Processing wubi86 dictionary...
    python "%ROOT%data\tools\build_binary.py" --input "%ROOT%data\wubi86.dict.db" --output "%DIST_DIR%\data\wubi86" 2>&1
    if exist "%DIST_DIR%\data\wubi86.dict.bin" (
        echo        wubi86.dict.bin generated.
    )
    copy /y "%ROOT%data\wubi86.dict.db" "%DIST_DIR%\data\" >nul
)

if "!HAS_DICT!"=="0" (
    echo        ERROR: No dictionary found. Run data\tools\fetch_dict.py first.
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

where makensis >nul 2>&1
if not errorlevel 1 (
    pushd "%DIST_DIR%"
    makensis cxxime-setup.nsi
    if not errorlevel 1 (
        move /y "cxxime-v%VERSION%-setup.exe" "%ROOT%" >nul
        echo        Installer created: %ROOT%cxxime-v%VERSION%-setup.exe
    ) else (
        echo        ERROR: NSIS compilation failed.
    )
    popd
) else (
    echo        WARNING: makensis not found in PATH. Install NSIS 3.x.
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
echo       pinyin.dict.db       Pinyin source dict (optional rebuild^)
echo       wubi86.dict.bin      Wubi binary dictionary (if available^)
echo       wubi86.dict.idx      Wubi syllable index
echo       wubi86.spellings.bin Wubi spelling trie
echo       wubi86.dict.db       Wubi source dict (optional rebuild^)
echo     install.bat            Installer (run as administrator^)
echo     uninstall.bat          Uninstaller (run as administrator^)
echo     install.ps1            PowerShell installer (optional^)
echo     uninstall.ps1          PowerShell uninstaller (optional^)

endlocal
