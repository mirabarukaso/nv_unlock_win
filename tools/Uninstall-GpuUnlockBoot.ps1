# SPDX-License-Identifier: GPL-2.0-only
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
$state = Get-InstallState $Esp

$source = $null
foreach ($candidate in @($backup, $backupAlt)) {
    if ((Test-Path -LiteralPath $candidate) -and (Test-PeEfiApplication $candidate) -and -not (Test-WrapperFile $candidate)) {
        $source = $candidate
        break
    }
}
if (-not $source) {
    throw "No original bootmgfw backup found under $Esp. Do not delete the wrapper until a Microsoft bootmgfw.efi is restored from installation media."
}

if ($PSCmdlet.ShouldProcess($MsBoot, "Restore from $source")) {
    Copy-Item -LiteralPath $source -Destination $MsBoot -Force
}

if ($state -and $state.replacedFallback) {
    $fallback = Join-Path $Esp $script:FallbackRel
    if (Test-WrapperFile $fallback) {
        Copy-Item -LiteralPath $source -Destination $fallback -Force
        Write-Host "Restored $fallback"
    }
}

foreach ($name in @($script:TaskName, $script:LegacyTaskName)) {
    Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
}

if ($Purge) {
    $dir = Join-Path $Esp $script:DirRel
    if ($PSCmdlet.ShouldProcess($dir, 'Remove turing-unlock ESP directory')) {
        Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Host 'Kept EFI\turing-unlock backups. Use -Purge to delete them.'
}

Write-Host "Uninstall complete. Windows Boot Manager restored from $source"
