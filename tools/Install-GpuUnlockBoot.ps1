# SPDX-License-Identifier: GPL-2.0-only
# Install the GPU Unlock loader on the internal ESP.
# -InstallMode Replace (default): replace bootmgfw.efi with the wrapper + Boot Guard.
# -InstallMode FirmwareEntry: leave Microsoft's bootmgfw.efi alone; add a firmware
#   NVRAM boot entry that points at EFI\turing-unlock\TuringUnlockLoader.efi.
# Default boot choice is SKIP inject. Original bootmgfw is kept for rollback / mode detect.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string]$LoaderEfi,
    [string]$FirmwareDir,
    [int]$TimeoutSec = 20,
    [ValidateSet('skip', 'inject')]
    [string]$DefaultAction = 'skip',
    [ValidateSet('Replace', 'FirmwareEntry')]
    [string]$InstallMode = 'Replace',
    [switch]$AlsoFallback,
    [switch]$SkipScheduledTask
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-Administrator

$LoaderEfi = (Resolve-Path -LiteralPath $LoaderEfi).Path
if (-not (Test-PeEfiApplication $LoaderEfi)) {
    throw "Not an x64 EFI application: $LoaderEfi"
}
if (-not (Test-WrapperFile $LoaderEfi)) {
    throw "Loader is missing magic $script:TuMagic. Build UnlockLoader-<Arch>.efi first."
}

$Esp = Get-EspRoot
Write-Host "ESP: $Esp"
Write-Host "InstallMode: $InstallMode"

$MsBoot = Join-Path $Esp $script:MsBootRel
if (-not (Test-Path -LiteralPath $MsBoot)) {
    throw "Windows Boot Manager not found: $MsBoot"
}

$prevState = Get-InstallState $Esp
$prevMode = Get-GpuUnlockInstallMode $prevState
$backup = Join-Path $Esp $script:MsBackupRel
$backupAlt = Join-Path $Esp $script:MsBackupAltRel
$backupStampDir = Join-Path $Esp "EFI\turing-unlock\backup\$(Get-Date -Format 'yyyyMMdd-HHmmss')"
$destLoader = Join-Path $Esp $script:EspLoaderRel
$replacedFallback = $false
$firmwareEntryId = $null

New-Item -ItemType Directory -Force -Path (Join-Path $Esp $script:DirRel) | Out-Null

if ($InstallMode -eq 'FirmwareEntry') {
    if (Get-SecureBootEnabled) {
        Write-Warning 'Secure Boot is ON. Firmware will reject an unsigned loader. Self-sign TuringUnlockLoader.efi and enroll the cert into firmware db before reboot (see README).'
    }

    if ($AlsoFallback) {
        Write-Warning '-AlsoFallback is ignored in FirmwareEntry mode (bootx64.efi is left alone).'
    }

    if ($PSCmdlet.ShouldProcess($MsBoot, 'Ensure official bootmgfw.efi for FirmwareEntry')) {
        Restore-OfficialBootmgfwIfWrapper -EspRoot $Esp | Out-Null
        Ensure-BootmgfwBackup -EspRoot $Esp -BackupStampDir $backupStampDir
    }

    if ($PSCmdlet.ShouldProcess($destLoader, 'Install loader under EFI\turing-unlock\')) {
        Copy-Item -LiteralPath $LoaderEfi -Destination $destLoader -Force
    }

    if ($PSCmdlet.ShouldProcess('{fwbootmgr}', 'Create or update GPU Unlock firmware boot entry')) {
        $existingId = $null
        if ($prevState -and $prevState.firmwareEntryId) {
            $existingId = [string]$prevState.firmwareEntryId
        }
        $firmwareEntryId = Install-GpuUnlockFirmwareEntry -ExistingId $existingId
    }

    Write-FirmwareEntryInstallWarnings
} else {
    if (Get-SecureBootEnabled) {
        Write-Warning 'Secure Boot is ON. Firmware will reject this unsigned wrapper. Turn it off before reboot, or use -InstallMode FirmwareEntry with a self-signed loader.'
    }

    if ($prevMode -eq 'FirmwareEntry' -and $prevState -and $prevState.firmwareEntryId) {
        Write-Host 'Previous install was FirmwareEntry; removing that firmware boot entry before Replace mode.'
        if ($PSCmdlet.ShouldProcess([string]$prevState.firmwareEntryId, 'Remove previous FirmwareEntry')) {
            Remove-GpuUnlockFirmwareEntry -EntryId ([string]$prevState.firmwareEntryId)
        }
    }

    $alreadyWrapper = Test-WrapperFile $MsBoot
    New-Item -ItemType Directory -Force -Path $backupStampDir | Out-Null

    if (-not $alreadyWrapper) {
        if (-not (Test-PeEfiApplication $MsBoot)) {
            throw "Refusing to replace ${MsBoot}: it is not a PE EFI image."
        }
        if ($PSCmdlet.ShouldProcess($MsBoot, 'Backup original bootmgfw.efi')) {
            Copy-Item -LiteralPath $MsBoot -Destination (Join-Path $backupStampDir 'bootmgfw.efi') -Force
            Copy-Item -LiteralPath $MsBoot -Destination $backup -Force
            Copy-Item -LiteralPath $MsBoot -Destination $backupAlt -Force
        }
    } else {
        Write-Host 'bootmgfw.efi is already the wrapper. Original backup will be kept.'
        if (-not (Test-Path -LiteralPath $backup)) {
            throw 'Wrapper is installed but bootmgfw_ms.efi backup is missing. Restore Windows boot files first.'
        }
    }

    if ($PSCmdlet.ShouldProcess($MsBoot, 'Install wrapper as bootmgfw.efi')) {
        Copy-Item -LiteralPath $LoaderEfi -Destination $destLoader -Force
        Copy-Item -LiteralPath $LoaderEfi -Destination $MsBoot -Force
    }

    $fallback = Join-Path $Esp $script:FallbackRel
    if (Test-Path -LiteralPath $fallback) {
        $replaceFb = $AlsoFallback
        if (-not $AlsoFallback -and -not (Test-WrapperFile $fallback)) {
            $origHash = Get-FileSha256 $backup
            $fbHash = Get-FileSha256 $fallback
            if ($origHash -and $origHash -eq $fbHash) {
                $replaceFb = $true
            }
        }
        if ($replaceFb) {
            Copy-Item -LiteralPath $fallback -Destination (Join-Path $backupStampDir 'bootx64.efi') -Force
            Copy-Item -LiteralPath $LoaderEfi -Destination $fallback -Force
            $replacedFallback = $true
            Write-Host "Also replaced $fallback"
        }
    }

    Write-Host ''
    Write-Warning 'Replace mode: Windows Update / Secure-Boot-Update can overwrite bootmgfw.efi and remove the loader until Boot Guard runs again. Fast reboots can miss unlock. Prefer USB, or -InstallMode FirmwareEntry when Secure Boot must stay on.'
}

$config = @"
default=$DefaultAction
timeout_sec=$TimeoutSec
verbose=0
targets=all
"@
[IO.File]::WriteAllText((Join-Path $Esp 'EFI\turing-unlock\config.ini'), $config, [Text.UTF8Encoding]::new($false))

if (-not $FirmwareDir) {
    $repoUefi = Split-Path -Parent $PSScriptRoot
    $leaf = Split-Path -Leaf $LoaderEfi
    if ($leaf -match 'UnlockLoader-(Turing|Ampere|Ada)\.efi$') {
        $candidate = Join-Path $repoUefi "assets\firmware\$($Matches[1])"
        $bins = @(Get-ChildItem -LiteralPath $candidate -Filter '*.bin' -ErrorAction SilentlyContinue)
        if ($bins.Count -gt 0) {
            $FirmwareDir = $candidate
            Write-Host "Using staged firmware $FirmwareDir"
        }
    }
}

if ($FirmwareDir -and (Test-Path -LiteralPath $FirmwareDir)) {
    $fwDest = Join-Path $Esp 'EFI\turing-unlock\firmware'
    New-Item -ItemType Directory -Force -Path $fwDest | Out-Null
    Copy-Item -Path (Join-Path $FirmwareDir '*') -Destination $fwDest -Force
    Write-Host "Firmware copied to $fwDest"
} else {
    Write-Host 'No firmware staged. Real-hardware inject needs tools\Build-GpuUnlockFirmware.ps1 then Install -FirmwareDir, or blobs already under EFI\turing-unlock\firmware\.'
}

$newState = [ordered]@{
    version          = '0.2.0'
    installMode      = $InstallMode
    installedUtc     = [DateTime]::UtcNow.ToString('o')
    wrapperSha256    = Get-FileSha256 $destLoader
    originalSha256   = Get-FileSha256 $backup
    replacedFallback = $replacedFallback
    timeoutSec       = $TimeoutSec
    defaultAction    = $DefaultAction
    paths            = @{
        wrapper      = $script:EspLoaderRel
        bootmgfw     = $script:MsBootRel
        original     = $script:MsBackupRel
        originalAlt  = $script:MsBackupAltRel
    }
}
if ($InstallMode -eq 'FirmwareEntry' -and $firmwareEntryId) {
    $newState.firmwareEntryId = $firmwareEntryId
}
Save-InstallState $Esp $newState

if (-not $SkipScheduledTask) {
    try {
        if ($InstallMode -eq 'FirmwareEntry') {
            Register-GpuUnlockBootOrderTask
        } else {
            Register-GpuUnlockProtectTask
        }
    } catch {
        Write-Warning "Scheduled task was not registered: $_. Run Protect-GpuUnlockBoot.ps1 -RegisterTask after Windows updates."
    }
}

Write-Host ''
Write-Host 'Install complete.'
if ($InstallMode -eq 'FirmwareEntry') {
    Write-Host "  Mode    : FirmwareEntry (own firmware boot entry)"
    Write-Host "  Wrapper : $destLoader"
    Write-Host "  bootmgfw: left as Microsoft's file at $MsBoot"
    if ($firmwareEntryId) {
        Write-Host "  Entry   : $firmwareEntryId (displayorder addfirst)"
    }
    Write-Host 'Reboot into the GPU Unlock firmware entry. Default menu action is SKIP.'
} else {
    Write-Host "  Mode    : Replace (wrapper as bootmgfw.efi)"
    Write-Host "  Wrapper : $MsBoot"
    Write-Host "  Original: $backup"
    Write-Host 'Reboot. Default menu action is SKIP. Press I to inject when a GPU of the built arch is present.'
}
Write-Host 'Rollback: Uninstall-GpuUnlockBoot.ps1  (add -Purge to delete EFI\turing-unlock including firmware blobs)'
Write-Host 'Preferred path for most users: USB stick via New-GpuUnlockUsb.ps1 (does not rewrite the internal ESP).'
