@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "BUILD_DIR=%~dp0build"
set "SOURCE_DIR=%~dp0."
set "CONFIG=Release"

if /i "%~1"=="debug" (
    set "CONFIG=Debug"
    shift
)
if /i "%~1"=="clean" goto clean
if "%~1"=="" goto configure
if not "%~1"=="-j" goto usage
set "BUILD_JOBS=%~2"
if not defined BUILD_JOBS goto usage
echo(%BUILD_JOBS%| findstr /r /x "[1-9][0-9]*" >nul
if errorlevel 1 goto usage
if not "%~3"=="" goto usage
set "CMAKE_BUILD_PARALLEL_LEVEL=%BUILD_JOBS%"
goto configure

:usage
echo Usage: build.bat [debug] [-j N] ^| build.bat clean
echo N must be a positive integer. Otherwise CMAKE_BUILD_PARALLEL_LEVEL or the tool default is used.
exit /b 1

:configure
if defined CMAKE_BUILD_PARALLEL_LEVEL (
    echo Build parallelism: %CMAKE_BUILD_PARALLEL_LEVEL%
)

set "CMAKE_EXE=cmake"
if defined CXXIME_CMAKE set "CMAKE_EXE=%CXXIME_CMAKE%"
echo CMake: "%CMAKE_EXE%"
"%CMAKE_EXE%" --version
if errorlevel 1 (
    echo ERROR: Cannot run CMake. Add it to PATH or set CXXIME_CMAKE to cmake.exe.
    exit /b 1
)

echo === CxxIME Build (%CONFIG%) ===
echo [1/2] Configuring CMake...
set "CXXIME_BUILD_CONFIG=%CONFIG%"
"%CMAKE_EXE%" -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -DCXXIME_PRODUCTION_BUILD=OFF -DCXXIME_BUILD_TOOLS=ON ^
    -DCXXIME_BUILD_TESTS=ON
if errorlevel 1 (
    echo ERROR: CMake configuration failed. Use a new build directory when changing toolchains.
    exit /b 1
)

echo Active build configuration ^(existing caches retain their toolchain^):
findstr /b /c:"CMAKE_GENERATOR:" /c:"CMAKE_GENERATOR_PLATFORM:" ^
    /c:"CMAKE_GENERATOR_INSTANCE:" /c:"CMAKE_GENERATOR_TOOLSET:" "%BUILD_DIR%\CMakeCache.txt"

echo [2/2] Building...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

set "OUTPUT_SUFFIX="
for /f "tokens=2 delims==" %%C in (
    'findstr /b /c:"CMAKE_CONFIGURATION_TYPES:" "%BUILD_DIR%\CMakeCache.txt"'
) do set "OUTPUT_SUFFIX=\%CONFIG%"
echo.
echo === Build succeeded (%CONFIG%) ===
echo Build directory: "%BUILD_DIR%"
for %%F in (
    "%BUILD_DIR%\server%OUTPUT_SUFFIX%\cxxime-server.exe"
    "%BUILD_DIR%\tsf%OUTPUT_SUFFIX%\cxxime_tsf_*.dll"
    "%BUILD_DIR%\resource%OUTPUT_SUFFIX%\cxxime-resources.dll"
) do if exist "%%~F" echo Output: "%%~F"
exit /b 0

:clean
set "BUILD_ATTRIBUTES="
for %%I in ("%BUILD_DIR%") do set "BUILD_ATTRIBUTES=%%~aI"
if not defined BUILD_ATTRIBUTES goto cleaned
if not "%BUILD_ATTRIBUTES:l=%"=="%BUILD_ATTRIBUTES%" (
    echo ERROR: Refusing to clean a linked build directory.
    exit /b 1
)
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    if exist "%BUILD_DIR%" exit /b 1
)
:cleaned
echo Build directory cleaned.
exit /b 0
