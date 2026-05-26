# CxxIME Installer
# Usage: Right-click -> Run with PowerShell, or: powershell -ExecutionPolicy Bypass -File install.ps1
# Requires Administrator privileges.

param(
    [string]$DataDir = "$env:USERPROFILE\cxxime",
    [switch]$Silent
)

$ErrorActionPreference = "Stop"
$ProductName = "CxxIME"
$ProductVersion = "0.1.0"
$TsfDllName = "cxxime_tsf.dll"
$ServerExeName = "cxxime-server.exe"

# ── Helper functions ──────────────────────────────────────────────────────────

function Write-Banner {
    if (-not $Silent) {
        Write-Host ""
        Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Cyan
        Write-Host "  ║       CxxIME Installer v$ProductVersion        ║" -ForegroundColor Cyan
        Write-Host "  ║   Lightweight Pinyin Input Method    ║" -ForegroundColor Cyan
        Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Cyan
        Write-Host ""
    }
}

function Write-Step($step, $total, $msg) {
    if (-not $Silent) {
        Write-Host "[$step/$total] $msg" -ForegroundColor Yellow
    }
}

function Write-OK($msg) {
    if (-not $Silent) {
        Write-Host "  ✓ $msg" -ForegroundColor Green
    }
}

function Write-Warn($msg) {
    Write-Host "  ⚠ $msg" -ForegroundColor DarkYellow
}

function Write-Err($msg) {
    Write-Host "  ✗ $msg" -ForegroundColor Red
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Request-Elevation {
    if (-not (Test-Administrator)) {
        Write-Host "Requesting administrator privileges..." -ForegroundColor Yellow
        $scriptPath = $MyInvocation.ScriptName
        if (-not $scriptPath) { $scriptPath = $PSCommandPath }
        $args = "-ExecutionPolicy Bypass -File `"$scriptPath`""
        if ($Silent) { $args += " -Silent" }
        if ($DataDir -ne "$env:USERPROFILE\cxxime") {
            $args += " -DataDir `"$DataDir`""
        }
        try {
            Start-Process powershell -Verb RunAs -ArgumentList $args -Wait
            exit 0
        } catch {
            Write-Err "Failed to elevate. Please run as administrator."
            exit 1
        }
    }
}

# ── Pre-flight checks ────────────────────────────────────────────────────────

Write-Banner
Request-Elevation

$TotalSteps = 5
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ScriptDir) { $ScriptDir = Get-Location }

# ── Step 1: Verify source files ──────────────────────────────────────────────

Write-Step 1 $TotalSteps "Verifying source files..."

$RequiredFiles = @(
    (Join-Path $ScriptDir $TsfDllName),
    (Join-Path $ScriptDir $ServerExeName)
)

foreach ($f in $RequiredFiles) {
    if (-not (Test-Path $f)) {
        Write-Err "Required file not found: $f"
        Write-Host "  Run package.bat first to prepare distribution files."
        exit 1
    }
}
Write-OK "All required files found."

$DictBin = Join-Path $ScriptDir "data\pinyin.dict.bin"
if (-not (Test-Path $DictBin)) {
    Write-Warn "pinyin.dict.bin not found. IME will not function without a dictionary."
}

# ── Step 2: Stop existing server ────────────────────────────────────────

Write-Step 2 $TotalSteps "Stopping existing server (if running)..."
$existing = Get-Process -Name "cxxime-server" -ErrorAction SilentlyContinue
if ($existing) {
    Stop-Process -Name "cxxime-server" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    Write-OK "Server stopped."
} else {
    Write-OK "No running server found."
}

# ── Step 3: Unregister existing TSF DLL ──────────────────────────────────────

Write-Step 3 $TotalSteps "Cleaning previous installation..."
$existingDll = Join-Path $DataDir $TsfDllName
if (Test-Path $existingDll) {
    try {
        Start-Process regsvr32 -ArgumentList "/u /s `"$existingDll`"" -Wait -NoNewWindow -ErrorAction SilentlyContinue
        Write-OK "Previous TSF DLL unregistered."
    } catch {
        Write-Warn "Could not unregister previous DLL (may be in use)."
    }
} else {
    Write-OK "No previous installation found."
}

# ── Step 4: Install files ────────────────────────────────────────────────────

Write-Step 4 $TotalSteps "Installing files to $DataDir..."

try {
    if (-not (Test-Path $DataDir)) {
        New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
    }

    # Core binaries
    Copy-Item (Join-Path $ScriptDir $TsfDllName) $DataDir -Force
    Copy-Item (Join-Path $ScriptDir $ServerExeName) $DataDir -Force

    # Config + themes
    $srcDataDir = Join-Path $ScriptDir "data"
    if (Test-Path (Join-Path $srcDataDir "default.json")) {
        Copy-Item (Join-Path $srcDataDir "default.json") $DataDir -Force
    }
    if (Test-Path (Join-Path $srcDataDir "themes.json")) {
        Copy-Item (Join-Path $srcDataDir "themes.json") $DataDir -Force
    }

    # Dictionary files (binary runtime + source)
    foreach ($name in @("pinyin", "wubi86")) {
        foreach ($ext in @("dict.bin", "dict.idx", "spellings.bin", "dict.db")) {
            $src = Join-Path $srcDataDir "$name.$ext"
            if (Test-Path $src) {
                Copy-Item $src $DataDir -Force
            }
        }
    }

    Write-OK "Files installed."
} catch {
    Write-Err "Failed to install files: $_"
    exit 1
}

# ── Step 5: Register TSF DLL and start server ────────────────────────────────

Write-Step 5 $TotalSteps "Registering TSF text service and starting server..."
$TsfDllPath = Join-Path $DataDir $TsfDllName

# Register
try {
    $proc = Start-Process regsvr32 -ArgumentList "/s `"$TsfDllPath`"" -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        throw "regsvr32 exited with code $($proc.ExitCode)"
    }
    Write-OK "TSF DLL registered."
} catch {
    Write-Err "Failed to register TSF DLL: $_"
    Write-Host "  The IME will not appear in the input method list." -ForegroundColor Red
}

# Auto-start
$ServerPath = Join-Path $DataDir $ServerExeName
$RegPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
try {
    Set-ItemProperty -Path $RegPath -Name "CxxIMEServer" -Value "`"$ServerPath`"" -Force
    Write-OK "Auto-start registered."
} catch {
    Write-Warn "Could not set auto-start: $_"
}

# Start server
try {
    Start-Process $ServerPath -WindowStyle Hidden
    Start-Sleep -Milliseconds 500
    $proc = Get-Process -Name "cxxime-server" -ErrorAction SilentlyContinue
    if ($proc) {
        Write-OK "Server started (PID: $($proc.Id))."
    } else {
        Write-Warn "Server process started but may have exited immediately."
        Write-Host "  Check Windows Event Viewer for errors." -ForegroundColor DarkYellow
    }
} catch {
    Write-Warn "Could not start server: $_"
}

# ── Done ──────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Green
Write-Host "  ║       Installation Complete!          ║" -ForegroundColor Green
Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  Data directory: $DataDir" -ForegroundColor White
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor White
Write-Host "    1. Log off and log back in (or restart)" -ForegroundColor Gray
Write-Host "    2. Press Ctrl+Space or Win+Space to switch input method" -ForegroundColor Gray
Write-Host "    3. Type pinyin letters to start composing" -ForegroundColor Gray
Write-Host ""
Write-Host "  To uninstall: Run uninstall.ps1 or uninstall.bat as administrator" -ForegroundColor Gray
Write-Host ""

if (-not $Silent) {
    Read-Host "Press Enter to exit"
}
