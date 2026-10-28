# SPDX-License-Identifier: GPL-2.0-only
# Remove the drive letter from the internal EFI system partition.
# Files on that partition are left as they are. USB disks are left alone.
[CmdletBinding(SupportsShouldProcess = $true)]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-Administrator

function Test-EspVolume([string]$Root) {
    $root = $Root.TrimEnd('\')
    if (Test-Path -LiteralPath (Join-Path $root 'Windows\System32')) {
        return $false
    }
    return (Test-Path -LiteralPath (Join-Path $root 'EFI\Microsoft\Boot')) -or
           (Test-Path -LiteralPath (Join-Path $root 'EFI\turing-unlock'))
}

$parts = @(Get-Partition -ErrorAction SilentlyContinue | Where-Object { $_.GptType -eq $script:EspGptType })
if ($parts.Count -eq 0) {
    throw 'No EFI system partition was found.'
}

$removed = 0
$hidden = 0
foreach ($part in $parts) {
    $disk = Get-Disk -Number $part.DiskNumber
    if ($disk.BusType -eq 'USB') {
        Write-Host "Skip USB disk $($disk.Number) partition $($part.PartitionNumber)."
        continue
    }
    $paths = @(Get-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -ErrorAction SilentlyContinue)
    $letters = @($paths | Where-Object { $_ -match '^[A-Za-z]:\\$' })
    if ($letters.Count -eq 0) {
        Write-Host "ESP disk $($disk.Number) partition $($part.PartitionNumber) has no drive letter."
        $hidden++
        continue
    }
    foreach ($access in $letters) {
        $letter = $access.Substring(0, 1).ToUpperInvariant()
        if ($letter -eq 'C') {
            throw "Refusing to remove C: from disk $($disk.Number) partition $($part.PartitionNumber)."
        }
        if (-not (Test-EspVolume "${letter}:")) {
            Write-Host "Skip ${letter}: on disk $($disk.Number). It has no EFI directory."
            continue
        }
        $target = "disk $($disk.Number) partition $($part.PartitionNumber) (${letter}:)"
        if (-not $PSCmdlet.ShouldProcess($target, 'remove the EFI system partition drive letter')) {
            continue
        }
        & mountvol.exe "${letter}:" /D
        if ($LASTEXITCODE -ne 0) {
            throw "mountvol ${letter}: /D failed with exit $LASTEXITCODE."
        }
        Write-Host "Removed ${letter}: from the EFI system partition."
        $removed++
    }
}

if (($removed -eq 0) -and ($hidden -gt 0)) {
    Write-Host 'The internal EFI system partition is already hidden.'
}
