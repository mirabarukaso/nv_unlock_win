# SPDX-License-Identifier: GPL-2.0-only
# Re-apply the wrapper if the loader, Windows Update, or Startup Repair
# restored Microsoft bootmgfw.efi. -RegisterTask rewrites the Boot Guard task
# (1 min after startup) and then runs a protect pass.
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

if ($RegisterTask) {
    Register-GpuUnlockProtectTask
}

try {
    $Esp = Wait-EspRoot
} catch {
    Write-ProtectLog "ESP not available: $_"
    exit 0
}

function Update-InstallStateSafe {
    param($Esp, $State, $WrapperHash, $Backup)
    if (-not $State) { return }
    try {
        $State | Add-Member -NotePropertyName wrapperSha256 -NotePropertyValue $WrapperHash -Force
        if ($Backup) {
            $State | Add-Member -NotePropertyName originalSha256 -NotePropertyValue (Get-FileSha256 $Backup) -Force
        }
        $State | Add-Member -NotePropertyName lastProtectUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
        Save-InstallState $Esp $State
    } catch {
        Write-ProtectLog "install-state.json not updated: $_"
    }
}

$state = $null
try {
    $state = Get-InstallState $Esp
} catch {
    Write-ProtectLog "install-state.json unread: $_"
}

$wrapperSrc = Join-Path $Esp $script:EspLoaderRel
$MsBoot = Join-Path $Esp $script:MsBootRel
$backup = Join-Path $Esp $script:MsBackupRel

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
