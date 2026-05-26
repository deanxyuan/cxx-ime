# CxxIME Uninstaller
# Usage: Right-click -> Run with PowerShell, or: powershell -ExecutionPolicy Bypass -File uninstall.ps1
# Requires Administrator privileges.

param(
    [string]$DataDir = "$env:USERPROFILE\cxxime",
    [switch]$Silent
)

$ErrorActionPreference = "Stop"
$ProductName = "CxxIME"
$TsfDllName = "cxxime_tsf.dll"
$ServerExeName = "cxxime-server.exe"
# CLSID from tsf/src/globals.h — must match c_clsidTextService
$TsfClsid = "{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

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

# ── Pre-flight ────────────────────────────────────────────────────────────────

Write-Banner
Request-Elevation

$TotalSteps = 6
$HadErrors = $false

# ── Confirmation ──────────────────────────────────────────────────────────────

if (-not $Silent) {
    Write-Host "  This will completely remove $ProductName from your system." -ForegroundColor White
    Write-Host "  Data directory: $DataDir" -ForegroundColor Gray
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
$TsfDllPath = Join-Path $DataDir $TsfDllName
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
    Write-OK "TSF DLL not found (already removed)."
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

# ── Step 4: Clean TSF COM registration ──────────────────────────────────────

Write-Step 4 $TotalSteps "Cleaning TSF COM registration..."
$ClsidPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$TsfClsid"
try {
    if (Test-Path $ClsidPath) {
        Remove-Item $ClsidPath -Recurse -Force
        Write-OK "CLSID registration removed."
    } else {
        Write-OK "No CLSID registration found."
    }
} catch {
    Write-Warn "Could not clean CLSID: $_"
    $HadErrors = $true
}

# ── Step 5: Clean TSF TIP profile ──────────────────────────────────────────

Write-Step 5 $TotalSteps "Cleaning TSF TIP registry entries..."
$TipPath = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\$TsfClsid"
try {
    if (Test-Path $TipPath) {
        Remove-Item $TipPath -Recurse -Force
        Write-OK "TSF TIP entry removed."
    } else {
        Write-OK "No TSF TIP entry found."
    }
} catch {
    Write-Warn "Could not clean TSF TIP: $_"
    $HadErrors = $true
}

# ── Step 6: Remove data files ────────────────────────────────────────────────

Write-Step 6 $TotalSteps "Removing data files..."
if (Test-Path $DataDir) {
    try {
        Start-Sleep -Milliseconds 300
        Remove-Item $DataDir -Recurse -Force -ErrorAction Stop
        Write-OK "Data directory removed."
    } catch {
        Write-Warn "Could not remove all files: $_"
        Write-Host "  Some files may be in use. Restart and try again." -ForegroundColor DarkYellow
        $HadErrors = $true
    }
} else {
    Write-OK "Data directory not found (already removed)."
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
    Write-Host "    - Manually delete: $DataDir" -ForegroundColor Gray
} else {
    Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Green
    Write-Host "  ║      Uninstall Complete!              ║" -ForegroundColor Green
    Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Green
}

Write-Host ""
Write-Host "  Log off and log back in (or restart) for changes to take effect." -ForegroundColor White
Write-Host ""

if (-not $Silent) {
    Read-Host "Press Enter to exit"
}
