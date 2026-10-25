# SPDX-License-Identifier: GPL-2.0-only
# Replace Windows Boot Manager with the GPU Unlock wrapper.
# Default boot choice is SKIP inject. Original bootmgfw is kept for rollback.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string]$LoaderEfi,
    [string]$FirmwareDir,
    [int]$TimeoutSec = 20,
    [ValidateSet('skip', 'inject')]
    [string]$DefaultAction = 'skip',
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

$MsBoot = Join-Path $Esp $script:MsBootRel
if (-not (Test-Path -LiteralPath $MsBoot)) {
    throw "Windows Boot Manager not found: $MsBoot"
}

if (Get-SecureBootEnabled) {
    Write-Warning 'Secure Boot is ON. Firmware will reject this unsigned wrapper. Turn it off before reboot.'
}

$alreadyWrapper = Test-WrapperFile $MsBoot
$state = Get-InstallState $Esp
$backup = Join-Path $Esp $script:MsBackupRel
$backupAlt = Join-Path $Esp $script:MsBackupAltRel
$backupStampDir = Join-Path $Esp "EFI\turing-unlock\backup\$(Get-Date -Format 'yyyyMMdd-HHmmss')"

New-Item -ItemType Directory -Force -Path (Join-Path $Esp $script:DirRel) | Out-Null
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

$destLoader = Join-Path $Esp $script:EspLoaderRel
if ($PSCmdlet.ShouldProcess($MsBoot, 'Install wrapper as bootmgfw.efi')) {
    Copy-Item -LiteralPath $LoaderEfi -Destination $destLoader -Force
    Copy-Item -LiteralPath $LoaderEfi -Destination $MsBoot -Force
}

$replacedFallback = $false
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

$config = @"
default=$DefaultAction
timeout_sec=$TimeoutSec
verbose=1
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
    version          = '0.1.0'
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
Save-InstallState $Esp $newState

if (-not $SkipScheduledTask) {
    try {
        Register-GpuUnlockProtectTask
    } catch {
        Write-Warning "Scheduled task was not registered: $_. Run Protect-GpuUnlockBoot.ps1 -RegisterTask after Windows updates."
    }
}

Write-Host ''
Write-Host 'Install complete.'
Write-Host "  Wrapper : $MsBoot"
Write-Host "  Original: $backup"
Write-Host 'Reboot. Default menu action is SKIP. Press I to inject when a GPU of the built arch is present.'
Write-Host 'Rollback: Uninstall-GpuUnlockBoot.ps1  (add -Purge to delete EFI\turing-unlock including firmware blobs)'
