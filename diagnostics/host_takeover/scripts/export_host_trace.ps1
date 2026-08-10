# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Export host diagnostics into at most two JSONL files for manual test handoff.

param(
    [string]$BuildId = "cxxime-host-takeover-20260725-b",
    [string]$LogsDir = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$SchemaVersion = 2

if (-not $LogsDir) {
    $LogsDir = Join-Path $env:USERPROFILE "cxxime\logs"
}
if (-not $OutputDir) {
    $OutputDir = [Environment]::GetFolderPath("Desktop")
    if (-not $OutputDir) {
        $OutputDir = $env:USERPROFILE
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function New-HostEvent {
    param(
        [string]$Event,
        [string]$Component,
        [hashtable]$Fields = @{}
    )

    $record = [ordered]@{
        schema_version = $SchemaVersion
        build_id = $BuildId
        arch = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
        event = $Event
        seq = 0
        timestamp_100ns = [DateTime]::UtcNow.ToFileTimeUtc()
        pid = $PID
        tid = 0
        process = "powershell.exe"
        component = $Component
    }
    foreach ($key in $Fields.Keys) {
        $record[$key] = $Fields[$key]
    }
    return [pscustomobject]$record
}

function Read-HostRecords {
    param([string]$Directory)

    $records = New-Object System.Collections.Generic.List[object]
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        return $records
    }

    $files = Get-ChildItem -LiteralPath $Directory -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "host-*.jsonl" -or
                       $_.Name -like "host-*.jsonl.1" }
    foreach ($file in $files) {
        foreach ($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8) {
            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }
            try {
                $record = $line | ConvertFrom-Json
            } catch {
                continue
            }
            if ($record.schema_version -ne $SchemaVersion -or
                $record.build_id -ne $BuildId) {
                continue
            }
            Add-Member -InputObject $record -NotePropertyName export_source -NotePropertyValue $file.Name
            $records.Add($record)
        }
    }
    return $records
}

function Add-DotaModuleStatus {
    param([System.Collections.Generic.List[object]]$Records)

    $processes = @(Get-Process -Name "dota2" -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) {
        $Records.Add((New-HostEvent -Event "runtime.component_status" -Component "exporter" -Fields @{
            name = "dota2.exe"
            result = "process_not_running"
        }))
        return
    }

    foreach ($process in $processes) {
        try {
            $modules = @($process.Modules)
            foreach ($moduleName in @("cxxime_tsf_x64.dll", "cxxime_tsf_x86.dll", "cxxime.ime")) {
                $module = $modules | Where-Object {
                    $_.ModuleName.ToLowerInvariant() -eq $moduleName
                } | Select-Object -First 1
                $fields = @{
                    name = $moduleName
                    host_pid = $process.Id
                    result = if ($module) { "loaded" } else { "not_loaded" }
                }
                if ($module) {
                    $fields.module_path = $module.FileName
                    $fields.file_version = $module.FileVersionInfo.FileVersion
                    try {
                        $fields.sha256 = (Get-FileHash -LiteralPath $module.FileName -Algorithm SHA256).Hash
                    } catch {
                        $fields.sha256 = "unavailable"
                    }
                }
                $Records.Add((New-HostEvent -Event "runtime.component_status" -Component "exporter" -Fields $fields))
            }
        } catch {
            $Records.Add((New-HostEvent -Event "runtime.component_status" -Component "exporter" -Fields @{
                name = "dota2_modules"
                host_pid = $process.Id
                result = "access_denied"
                reason = $_.Exception.GetType().Name
            }))
        }
    }
}

function Add-HostSummary {
    param([System.Collections.Generic.List[object]]$Records)

    $eventCounts = [ordered]@{}
    foreach ($record in $Records) {
        $eventName = [string]$record.event
        if (-not $eventCounts.Contains($eventName)) {
            $eventCounts[$eventName] = 0
        }
        $eventCounts[$eventName]++
    }

    $ownerConflicts = 0
    $routes = @($Records | Where-Object { $_.event -eq "key.route" -and $_.input_id }) |
        Group-Object -Property input_id
    foreach ($routeGroup in $routes) {
        $owners = @($routeGroup.Group | ForEach-Object { $_.owner } |
            Where-Object { $_ } | Sort-Object -Unique)
        if ($owners.Count -gt 1) {
            $ownerConflicts++
        }
    }

    $Records.Add((New-HostEvent -Event "trace.summary" -Component "exporter" -Fields @{
        record_count = $Records.Count
        event_counts = $eventCounts
        owner_conflicts = $ownerConflicts
        result = "exported"
    }))
}

function Write-JsonLines {
    param(
        [string]$Path,
        [object[]]$Records
    )

    $orderedRecords = @($Records | Sort-Object `
        @{ Expression = { [Int64]$_.timestamp_100ns } },
        @{ Expression = { [Int64]$_.pid } },
        @{ Expression = { [Int64]$_.seq } })
    $lines = @($orderedRecords | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 12 })
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllLines($Path, [string[]]$lines, $utf8NoBom)
}

$records = Read-HostRecords -Directory $LogsDir
$runtimeRecords = New-Object System.Collections.Generic.List[object]
$probeRecords = New-Object System.Collections.Generic.List[object]
foreach ($record in $records) {
    if ($record.component -eq "probe") {
        $probeRecords.Add($record)
    } else {
        $runtimeRecords.Add($record)
    }
}

Add-DotaModuleStatus -Records $runtimeRecords
Add-HostSummary -Records $runtimeRecords

$runtimePath = Join-Path $OutputDir "cxxime-host-runtime-$BuildId.jsonl"
Write-JsonLines -Path $runtimePath -Records $runtimeRecords.ToArray()
Write-Host "Runtime trace: $runtimePath"

if ($probeRecords.Count -gt 0) {
    Add-HostSummary -Records $probeRecords
    $probePath = Join-Path $OutputDir "cxxime-host-probe-$BuildId.jsonl"
    Write-JsonLines -Path $probePath -Records $probeRecords.ToArray()
    Write-Host "Probe trace:   $probePath"
} else {
    Write-Host "Probe trace:   no matching Probe events found"
}
