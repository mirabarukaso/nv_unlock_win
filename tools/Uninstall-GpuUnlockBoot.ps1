# SPDX-License-Identifier: GPL-2.0-only
# Remove an ESP install (Replace or FirmwareEntry).
# Replace: restore Microsoft bootmgfw.efi and drop the Boot Guard task.
# FirmwareEntry: delete the firmware NVRAM boot entry and the Boot Order guard;
#   leave/restore official bootmgfw.efi.
# ESP on-disk directory stays EFI\turing-unlock (loader path / magic).
# -Purge also deletes that directory (loader, backups, firmware blobs) and
# EFI\Microsoft\Boot\bootmgfw_ms.efi.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Purge
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-Administrator

$Esp = Get-EspRoot
$MsBoot = Join-Path $Esp $script:MsBootRel
$backup = Join-Path $Esp $script:MsBackupRel
$backupAlt = Join-Path $Esp $script:MsBackupAltRel
$espDir = Join-Path $Esp $script:DirRel
$fwDir = Join-Path $espDir 'firmware'
$state = Get-InstallState $Esp
$mode = Get-GpuUnlockInstallMode $state

Write-Host "Uninstall mode: $mode"

Unregister-GpuUnlockProtectTasks
Write-Host 'Removed GpuUnlock scheduled tasks (Boot Guard / Boot Order Guard) if present.'

$entryId = $null
if ($state -and $state.firmwareEntryId) {
    $entryId = [string]$state.firmwareEntryId
}
if ($mode -eq 'FirmwareEntry' -or $entryId) {
    if ($PSCmdlet.ShouldProcess('firmware entry', 'Remove GPU Unlock firmware boot entry')) {
        Remove-GpuUnlockFirmwareEntry -EntryId $entryId
    }
}

$source = $null
foreach ($candidate in @($backup, $backupAlt)) {
    if ((Test-Path -LiteralPath $candidate) -and (Test-PeEfiApplication $candidate) -and -not (Test-WrapperFile $candidate)) {
        $source = $candidate
        break
    }
}

if (Test-WrapperFile $MsBoot) {
    if (-not $source) {
        throw "No original bootmgfw backup found under $Esp. Do not delete the wrapper until a Microsoft bootmgfw.efi is restored from installation media."
    }
    if ($PSCmdlet.ShouldProcess($MsBoot, "Restore from $source")) {
        Copy-Item -LiteralPath $source -Destination $MsBoot -Force
        Write-Host "Restored Windows Boot Manager from $source"
    }
} elseif ($source) {
    Write-Host "bootmgfw.efi is already a non-wrapper PE; left in place (backup at $source)."
} else {
    Write-Host 'No bootmgfw_ms.efi backup found; bootmgfw.efi left as-is.'
}

if ($state -and $state.replacedFallback -and $source) {
    $fallback = Join-Path $Esp $script:FallbackRel
    if (Test-WrapperFile $fallback) {
        Copy-Item -LiteralPath $source -Destination $fallback -Force
        Write-Host "Restored $fallback"
    }
}

if ($Purge) {
    if (Test-Path -LiteralPath $fwDir) {
        $fwFiles = @(Get-ChildItem -LiteralPath $fwDir -File -Force -ErrorAction SilentlyContinue)
        if ($fwFiles.Count -gt 0) {
            Write-Host "ESP firmware to delete ($($fwFiles.Count)):"
            foreach ($f in $fwFiles) {
                Write-Host ("  {0}  ({1} bytes)" -f $f.Name, $f.Length)
            }
        } else {
            Write-Host "No files under $fwDir"
        }
    } else {
        Write-Host "No ESP firmware directory ($fwDir)"
    }

    if ($PSCmdlet.ShouldProcess($espDir, "Remove GPU Unlock ESP directory $script:DirRel (loader, backups, firmware)")) {
        if (Test-Path -LiteralPath $espDir) {
            Remove-Item -LiteralPath $espDir -Recurse -Force
        }
        if (Test-Path -LiteralPath $espDir) {
            throw "Failed to remove $espDir (firmware or backups may still be on the ESP)."
        }
        Write-Host "Removed $espDir"
    }

    if (Test-Path -LiteralPath $backupAlt) {
        if ($PSCmdlet.ShouldProcess($backupAlt, 'Remove Microsoft\Boot bootmgfw_ms.efi copy')) {
            Remove-Item -LiteralPath $backupAlt -Force
            Write-Host "Removed $backupAlt"
        }
    }

    $pd = $script:ProgramData
    if (Test-Path -LiteralPath $pd) {
        if ($PSCmdlet.ShouldProcess($pd, 'Remove ProgramData\GpuUnlock logs')) {
            Remove-Item -LiteralPath $pd -Recurse -Force
            Write-Host "Removed $pd"
        }
    }
} else {
    Write-Host "Kept $script:DirRel (loader, bootmgfw backups, firmware blobs). Use -Purge to delete them."
}

Write-Host 'Uninstall complete.'
