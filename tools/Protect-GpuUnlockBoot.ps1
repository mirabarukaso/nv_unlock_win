# SPDX-License-Identifier: GPL-2.0-only
# Protect pass for an ESP install.
# Replace mode: re-apply the wrapper if the loader, Windows Update, or Startup
#   Repair restored Microsoft bootmgfw.efi.
# FirmwareEntry mode: never touch bootmgfw.efi; keep the firmware boot entry
#   first in {fwbootmgr} displayorder.
# -RegisterTask rewrites the matching scheduled task and then runs a protect pass.
[CmdletBinding()]
param(
    [switch]$RegisterTask
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

if (-not (Test-Administrator)) {
    Write-ProtectLog 'Protect skipped: not elevated.'
    exit 0
}

try {
    $Esp = Wait-EspRoot
} catch {
    Write-ProtectLog "ESP not available: $_"
    exit 0
}

$state = $null
try {
    $state = Get-InstallState $Esp
} catch {
    Write-ProtectLog "install-state.json unread: $_"
}

$mode = Get-GpuUnlockInstallMode $state

if ($RegisterTask) {
    if ($mode -eq 'FirmwareEntry') {
        Register-GpuUnlockBootOrderTask
    } else {
        Register-GpuUnlockProtectTask
    }
}

function Update-InstallStateSafe {
    param($Esp, $State, $WrapperHash, $Backup, [string]$FirmwareEntryId)
    if (-not $State) { return }
    try {
        if ($WrapperHash) {
            $State | Add-Member -NotePropertyName wrapperSha256 -NotePropertyValue $WrapperHash -Force
        }
        if ($Backup) {
            $State | Add-Member -NotePropertyName originalSha256 -NotePropertyValue (Get-FileSha256 $Backup) -Force
        }
        if ($FirmwareEntryId) {
            $State | Add-Member -NotePropertyName firmwareEntryId -NotePropertyValue $FirmwareEntryId -Force
        }
        $State | Add-Member -NotePropertyName lastProtectUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
        Save-InstallState $Esp $State
    } catch {
        Write-ProtectLog "install-state.json not updated: $_"
    }
}

$wrapperSrc = Join-Path $Esp $script:EspLoaderRel
$MsBoot = Join-Path $Esp $script:MsBootRel
$backup = Join-Path $Esp $script:MsBackupRel

if ($mode -eq 'FirmwareEntry') {
    Write-ProtectLog 'Protect mode=FirmwareEntry (BootOrder only; bootmgfw.efi left alone).'
    Write-ProtectLog 'Note: Windows Update may still rewrite Microsoft bootmgfw.efi; this path only keeps the unlock entry first.'

    if (-not (Test-Path -LiteralPath $wrapperSrc)) {
        Write-ProtectLog "Protect skipped: $($script:EspLoaderRel) missing."
        exit 0
    }
    if (-not (Test-WrapperFile $wrapperSrc)) {
        Write-ProtectLog 'Protect skipped: stored loader lost its magic; refusing to point BootOrder at it.'
        exit 1
    }

    if (Test-WrapperFile $MsBoot) {
        Write-ProtectLog 'WARNING: bootmgfw.efi is still the unlock wrapper. FirmwareEntry expects Microsoft''s file there. Restore from bootmgfw_ms.efi.'
        try {
            Restore-OfficialBootmgfwIfWrapper -EspRoot $Esp | Out-Null
        } catch {
            Write-ProtectLog "Could not restore official bootmgfw.efi: $_"
        }
    }

    $preferred = $null
    if ($state -and $state.firmwareEntryId) {
        $preferred = [string]$state.firmwareEntryId
    }

    try {
        $id = Find-GpuUnlockFirmwareEntryId -PreferredId $preferred
        if (-not $id) {
            Write-ProtectLog 'Firmware entry missing; recreating.'
            $id = Install-GpuUnlockFirmwareEntry -ExistingId $null
            Update-InstallStateSafe -Esp $Esp -State $state -WrapperHash (Get-FileSha256 $wrapperSrc) -FirmwareEntryId $id
            Write-ProtectLog "Firmware entry recreated: $id"
            exit 0
        }

        $order = @(Get-FwBootmgrDisplayOrder)
        if ($order.Count -gt 0 -and $order[0] -eq $id) {
            Write-ProtectLog "OK: firmware entry $id is first in displayorder."
            Update-InstallStateSafe -Esp $Esp -State $state -WrapperHash (Get-FileSha256 $wrapperSrc) -FirmwareEntryId $id
            exit 0
        }

        Write-ProtectLog "Firmware entry $id is not first (order=$($order -join ',')). Moving to addfirst."
        Set-GpuUnlockFirmwareEntryFirst -EntryId $id
        Update-InstallStateSafe -Esp $Esp -State $state -WrapperHash (Get-FileSha256 $wrapperSrc) -FirmwareEntryId $id
        Write-ProtectLog "BootOrder restored: $id first."
        exit 0
    } catch {
        Write-ProtectLog "FirmwareEntry protect failed: $($_.Exception.Message)"
        Write-ProtectLog $_.ScriptStackTrace
        exit 1
    }
}

Write-ProtectLog 'Protect mode=Replace (re-wrap bootmgfw.efi when needed).'

if (-not (Test-Path -LiteralPath $wrapperSrc)) {
    Write-ProtectLog "Protect skipped: $($script:EspLoaderRel) missing."
    exit 0
}
if (-not (Test-WrapperFile $wrapperSrc)) {
    Write-ProtectLog 'Protect skipped: stored loader lost its magic; refusing to copy it.'
    exit 1
}

$msLen = (Get-Item -LiteralPath $MsBoot).Length
$wrapLen = (Get-Item -LiteralPath $wrapperSrc).Length
Write-ProtectLog "bootmgfw.efi $($msLen) bytes; stored wrapper $($wrapLen) bytes"

$currentIsWrapper = Test-WrapperFile $MsBoot
$currentHash = Get-FileSha256 $MsBoot
$wrapperHash = Get-FileSha256 $wrapperSrc

if ($currentIsWrapper -and $currentHash -eq $wrapperHash) {
    Write-ProtectLog 'OK: bootmgfw.efi is the current wrapper.'
    exit 0
}

try {
    if ($currentIsWrapper -and $currentHash -ne $wrapperHash) {
        Write-ProtectLog "bootmgfw.efi is an older wrapper. Refreshing from $($script:EspLoaderRel)."
        Copy-Item -LiteralPath $wrapperSrc -Destination $MsBoot -Force
        Write-ProtectLog 'Wrapper copied onto bootmgfw.efi.'
        Update-InstallStateSafe -Esp $Esp -State $state -WrapperHash $wrapperHash
        Write-ProtectLog 'Wrapper restored.'
        exit 0
    }

    if ((Test-PeEfiApplication $MsBoot) -and -not $currentIsWrapper) {
        Write-ProtectLog 'Microsoft (or unknown) bootmgfw detected. Saving it as the new original, then restoring the wrapper.'
        New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
        Copy-Item -LiteralPath $MsBoot -Destination $backup -Force
        Copy-Item -LiteralPath $MsBoot -Destination (Join-Path $Esp $script:MsBackupAltRel) -Force
        $stamp = Join-Path $Esp ("EFI\turing-unlock\backup\{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Force -Path $stamp | Out-Null
        Copy-Item -LiteralPath $MsBoot -Destination (Join-Path $stamp 'bootmgfw.efi') -Force
        Copy-Item -LiteralPath $wrapperSrc -Destination $MsBoot -Force
        Write-ProtectLog 'Wrapper copied onto bootmgfw.efi.'
        Update-InstallStateSafe -Esp $Esp -State $state -WrapperHash $wrapperHash -Backup $backup
        Write-ProtectLog 'Wrapper restored.'
        exit 0
    }
} catch {
    Write-ProtectLog "Protect failed: $($_.Exception.Message)"
    Write-ProtectLog $_.ScriptStackTrace
    exit 1
}

Write-ProtectLog "Unexpected bootmgfw.efi at $MsBoot; left untouched."
exit 1
