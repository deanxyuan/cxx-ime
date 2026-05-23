# CxxIME Uninstaller
# Usage: Right-click -> Run with PowerShell, or: powershell -ExecutionPolicy Bypass -File uninstall.ps1
# Requires Administrator privileges.

param(
    [string]$InstallDir = "$env:ProgramFiles\CxxIME",
    [switch]$Silent
)

$ErrorActionPreference = "Stop"
$ProductName = "CxxIME"
$TsfDllName = "cxxime_tsf.dll"
$ServerExeName = "cxxime-server.exe"

# ── Helper functions ──────────────────────────────────────────────────────────

function Write-Banner {
    if (-not $Silent) {
        Write-Host ""
        Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Cyan
        Write-Host "  ║         CxxIME Uninstaller           ║" -ForegroundColor Cyan
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
        if ($InstallDir -ne "$env:ProgramFiles\CxxIME") {
            $args += " -InstallDir `"$InstallDir`""
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

# ── Pre-flight ────────────────────────────────────────────────────────────────

Write-Banner
Request-Elevation

$TotalSteps = 5
$HadErrors = $false

# ── Confirmation ──────────────────────────────────────────────────────────────

if (-not $Silent) {
    Write-Host "  This will completely remove $ProductName from your system." -ForegroundColor White
    Write-Host "  Install directory: $InstallDir" -ForegroundColor Gray
    Write-Host ""
    $confirm = Read-Host "  Continue? (Y/N)"
    if ($confirm -notin @("Y", "y", "yes", "Yes")) {
        Write-Host "  Cancelled." -ForegroundColor Gray
        exit 0
    }
    Write-Host ""
}

# ── Step 1: Stop server ──────────────────────────────────────────────────────

Write-Step 1 $TotalSteps "Stopping server..."
$existing = Get-Process -Name "cxxime-server" -ErrorAction SilentlyContinue
if ($existing) {
    try {
        Stop-Process -Name "cxxime-server" -Force -ErrorAction Stop
        Start-Sleep -Milliseconds 500
        Write-OK "Server stopped."
    } catch {
        Write-Warn "Could not stop server: $_"
        $HadErrors = $true
    }
} else {
    Write-OK "No running server found."
}

# ── Step 2: Unregister TSF DLL ───────────────────────────────────────────────

Write-Step 2 $TotalSteps "Unregistering TSF text service..."
$TsfDllPath = Join-Path $InstallDir "bin" $TsfDllName
if (-not (Test-Path $TsfDllPath)) {
    $TsfDllPath = Join-Path $InstallDir $TsfDllName
}

if (Test-Path $TsfDllPath) {
    try {
        $proc = Start-Process regsvr32 -ArgumentList "/u /s `"$TsfDllPath`"" -Wait -PassThru -NoNewWindow
        if ($proc.ExitCode -eq 0) {
            Write-OK "TSF DLL unregistered."
        } else {
            Write-Warn "regsvr32 returned exit code $($proc.ExitCode). DLL may still be loaded."
            $HadErrors = $true
        }
    } catch {
        Write-Warn "Could not unregister TSF DLL: $_"
        $HadErrors = $true
    }
} else {
    Write-OK "TSF DLL not found (already removed or different location)."
}

# ── Step 3: Remove auto-start ────────────────────────────────────────────────

Write-Step 3 $TotalSteps "Removing auto-start entry..."
$RegPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
try {
    $prop = Get-ItemProperty -Path $RegPath -Name "CxxIMEServer" -ErrorAction SilentlyContinue
    if ($prop) {
        Remove-ItemProperty -Path $RegPath -Name "CxxIMEServer" -Force
        Write-OK "Auto-start entry removed."
    } else {
        Write-OK "No auto-start entry found."
    }
} catch {
    Write-Warn "Could not remove auto-start entry: $_"
    $HadErrors = $true
}

# ── Step 4: Remove files ─────────────────────────────────────────────────────

Write-Step 4 $TotalSteps "Removing files..."
if (Test-Path $InstallDir) {
    try {
        # Give a moment for any lingering handles to release
        Start-Sleep -Milliseconds 300
        Remove-Item $InstallDir -Recurse -Force -ErrorAction Stop
        Write-OK "Installation directory removed."
    } catch {
        Write-Warn "Could not remove all files: $_"
        Write-Host "  Some files may be in use. Restart and try again." -ForegroundColor DarkYellow
        $HadErrors = $true
    }
} else {
    Write-OK "Installation directory not found (already removed)."
}

# ── Step 5: Clean up registry (TSF profile) ──────────────────────────────────

Write-Step 5 $TotalSteps "Cleaning TSF registry entries..."
$TsfTipPath = "HKLM:\SOFTWARE\Microsoft\CTF\TIP"
$ClsidGuid = "{6A4B85B0-3D02-4B3A-9E69-715725DA7706}"

try {
    # Remove the TIP profile entry
    $tipKey = Join-Path $TsfTipPath $ClsidGuid
    if (Test-Path $tipKey) {
        Remove-Item $tipKey -Recurse -Force -ErrorAction SilentlyContinue
        Write-OK "TSF TIP registry entry removed."
    } else {
        Write-OK "No TSF TIP entry found."
    }
} catch {
    Write-Warn "Could not clean TSF registry: $_"
    $HadErrors = $true
}

# ── Done ──────────────────────────────────────────────────────────────────────

Write-Host ""
if ($HadErrors) {
    Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor DarkYellow
    Write-Host "  ║   Uninstall completed with warnings   ║" -ForegroundColor DarkYellow
    Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor DarkYellow
    Write-Host ""
    Write-Host "  Some steps had warnings. You may need to:" -ForegroundColor White
    Write-Host "    - Restart your computer to fully unload the TSF DLL" -ForegroundColor Gray
    Write-Host "    - Manually delete: $InstallDir" -ForegroundColor Gray
} else {
    Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Green
    Write-Host "  ║      Uninstall Complete!              ║" -ForegroundColor Green
    Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Green
}

Write-Host ""
Write-Host "  Please log off and log back in (or restart) for changes to take effect." -ForegroundColor White
Write-Host ""

if (-not $Silent) {
    Read-Host "Press Enter to exit"
}
