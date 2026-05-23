@echo off
setlocal

set BUILD_DIR=%~dp0build
set CONFIG=Release

if "%1"=="debug" set CONFIG=Debug
if "%1"=="Debug" set CONFIG=Debug
if "%1"=="clean" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Build directory cleaned.
    exit /b 0
)

echo === CxxIME Build (%CONFIG%) ===

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo [1/2] Configuring CMake...
cmake .. -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

echo [2/2] Building...
cmake --build . --config %CONFIG%
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo === Build succeeded (%CONFIG%) ===
echo Output: %BUILD_DIR%\server\%CONFIG%\cxxime-server.exe
echo         %BUILD_DIR%\tsf\%CONFIG%\cxxime_tsf.dll
echo         %BUILD_DIR%\test\%CONFIG%\cxxime-test.exe

endlocal
