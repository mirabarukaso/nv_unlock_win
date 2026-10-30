# SPDX-License-Identifier: GPL-2.0-only
# Copy the loader log off the EFI system partition into this tree's logs\.
# Mounting the ESP requires an elevated PowerShell. Run this by hand after a boot.
# An ESP drive letter that was already present is left mounted. A letter assigned
# by this script is removed after the copy.
[CmdletBinding()]
param(
    [string]$Destination,
    [string]$EspRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-Administrator

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Destination) {
    $Destination = Join-Path $RepoRoot 'logs'
} else {
    $Destination = $PSCmdlet.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination)
}

$assignedLetter = $null
try {
    if (-not $EspRoot) {
        $before = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { $_.Name })
        $EspRoot = Get-EspRoot
        if ($EspRoot -match '^[A-Za-z]:$') {
            $letter = $EspRoot.Substring(0, 1).ToUpperInvariant()
            if ($before -notcontains $letter) {
                $assignedLetter = $letter
            }
        }
    }
    $EspRoot = $EspRoot.TrimEnd('\')
    Write-Host "ESP: $EspRoot"

    $srcDir = Join-Path $EspRoot 'EFI\turing-unlock'
    if (-not (Test-Path -LiteralPath $srcDir)) {
        throw "Loader directory not found: $srcDir"
    }

    $logs = @(Get-ChildItem -LiteralPath $srcDir -File -Force -Filter '*.log' |
        Sort-Object LastWriteTime -Descending)
    if ($logs.Count -eq 0) {
        throw "No *.log under $srcDir. The loader writes unlock-YYYYMMDD-HHMMSS.log on the way into Windows."
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $newest = $logs[0]
    foreach ($log in $logs) {
        $stamp = $log.LastWriteTime.ToString('yyyyMMdd-HHmmss')
        $stampedName = '{0}-{1}{2}' -f $log.BaseName, $stamp, $log.Extension
        $stamped = Join-Path $Destination $stampedName
        Copy-Item -LiteralPath $log.FullName -Destination $stamped -Force
        $copied = Get-Item -LiteralPath $stamped
        $copied.LastWriteTime = $log.LastWriteTime
        $mark = ''
        if ($log.FullName -eq $newest.FullName) {
            $latest = Join-Path $Destination $log.Name
            Copy-Item -LiteralPath $log.FullName -Destination $latest -Force
            (Get-Item -LiteralPath $latest).LastWriteTime = $log.LastWriteTime
            $mark = '  (latest)'
            Write-Host ("latest -> {0}" -f $latest)
        }
        Write-Host ("{0}  {1} bytes  {2:yyyy-MM-dd HH:mm:ss}{3}" -f $log.Name, $log.Length, $log.LastWriteTime, $mark)
        Write-Host ("  {0}" -f $stamped)
    }
    Write-Host "Logs directory: $Destination"
} finally {
    if ($assignedLetter) {
        & mountvol.exe "${assignedLetter}:" /D
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Removed ESP drive letter ${assignedLetter}: assigned by this script."
        } else {
            Write-Warning "ESP was mounted as ${assignedLetter}: but mountvol /D failed with exit $LASTEXITCODE."
        }
    }
}
