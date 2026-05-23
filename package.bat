@echo off
setlocal

set ROOT=%~dp0
set BUILD_DIR=%ROOT%build
set DIST_DIR=%ROOT%dist
set CONFIG=Release
set VERSION=0.1.0

echo === CxxIME Packager (v%VERSION%) ===
echo.

:: Check build exists
if not exist "%BUILD_DIR%\tsf\%CONFIG%\cxxime_tsf.dll" (
    echo Build artifacts not found. Building Release first...
    call "%ROOT%build.bat"
    if errorlevel 1 (
        echo ERROR: Build failed. Cannot package.
        exit /b 1
    )
)

:: Clean dist directory
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\bin"
mkdir "%DIST_DIR%\data"

echo [1/5] Copying binaries...
copy /y "%BUILD_DIR%\tsf\%CONFIG%\cxxime_tsf.dll" "%DIST_DIR%\bin\" >nul
copy /y "%BUILD_DIR%\server\%CONFIG%\cxxime-server.exe" "%DIST_DIR%\bin\" >nul
copy /y "%BUILD_DIR%\test\%CONFIG%\cxxime-test.exe" "%DIST_DIR%\bin\" >nul

echo [2/5] Copying data files...
copy /y "%ROOT%data\default.json" "%DIST_DIR%\data\" >nul
if exist "%ROOT%data\pinyin.dict.db" (
    copy /y "%ROOT%data\pinyin.dict.db" "%DIST_DIR%\data\" >nul
    echo        pinyin.dict.db found.
) else (
    echo        WARNING: pinyin.dict.db not found. IME requires a dictionary to function.
)

echo [3/5] Copying installer scripts...
copy /y "%ROOT%install.bat" "%DIST_DIR%\" >nul
copy /y "%ROOT%uninstall.bat" "%DIST_DIR%\" >nul
copy /y "%ROOT%install.ps1" "%DIST_DIR%\" >nul 2>&1
copy /y "%ROOT%uninstall.ps1" "%DIST_DIR%\" >nul 2>&1
copy /y "%ROOT%README.md" "%DIST_DIR%\" >nul 2>&1

:: Generate setup.bat as a friendly entry point
echo [4/5] Generating setup launcher...
(
echo @echo off
echo echo.
echo echo  CxxIME - Lightweight Pinyin Input Method
echo echo.
echo echo  Options:
echo echo    1. Install
echo echo    2. Uninstall
echo echo    3. Exit
echo echo.
echo set /p "CHOICE=  Select: "
echo if "%%CHOICE%%"=="1" goto install
echo if "%%CHOICE%%"=="2" goto uninstall
echo exit /b 0
echo :install
echo call "%%~dp0install.bat" %%*
echo exit /b 0
echo :uninstall
echo call "%%~dp0uninstall.bat" %%*
) > "%DIST_DIR%\setup.bat"

:: Create zip archive
echo [5/5] Creating archive...
set ARCHIVE=%ROOT%cxxime-v%VERSION%-win64.zip
if exist "%ARCHIVE%" del /f "%ARCHIVE%"

:: Try PowerShell for zip, fall back to no-zip
where powershell >nul 2>&1
if not errorlevel 1 (
    powershell -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%ARCHIVE%' -Force" 2>nul
    if not errorlevel 1 (
        echo        Archive created: %ARCHIVE%
    ) else (
        echo        Zip creation failed. Distribution files are in: %DIST_DIR%
    )
) else (
    echo        PowerShell not available. Skipping zip creation.
    echo        Distribution files are in: %DIST_DIR%
)

echo.
echo === Packaging complete ===
echo.
echo Distribution contents:
echo   %DIST_DIR%\
echo     bin\
echo       cxxime_tsf.dll       TSF text service DLL
echo       cxxime-server.exe    Background server process
echo       cxxime-test.exe      Test suite
echo     data\
echo       default.json         Default configuration
echo       pinyin.dict.db       Pinyin dictionary
echo     setup.bat              Interactive setup launcher
echo     install.bat            Installer (run as administrator)
echo     uninstall.bat          Uninstaller (run as administrator)
echo     install.ps1            PowerShell installer (optional)
echo     uninstall.ps1          PowerShell uninstaller (optional)
echo     README.md              Documentation

endlocal
