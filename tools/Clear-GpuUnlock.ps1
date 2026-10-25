# SPDX-License-Identifier: GPL-2.0-only
# Remove host EFI copies and EDK2 Build trees. Does not touch ESP, EDK2/NASM clones,
# or source. Optional -Firmware drops staged blobs under assets\firmware.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$Edk2Path = $env:EDK2_PATH,
    [ValidateSet('Turing', 'Ampere', 'Ada')]
    [string]$Arch,
    [switch]$Firmware
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Lib.ps1')

$script:ClearFailed = $false

function Remove-ClearPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-ToolLine 'ok' $Label 'already absent'
        return
    }
    if (-not $PSCmdlet.ShouldProcess($Path, 'Remove')) {
        Write-ToolLine 'ok' $Label "would remove $Path"
        return
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer) {
        cmd /c "rmdir /s /q `"$Path`"" | Out-Null
        if (Test-Path -LiteralPath $Path) {
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item -LiteralPath $Path -Force
    }
    if (Test-Path -LiteralPath $Path) {
        Write-ToolLine 'fail' $Label "still present: $Path"
        $script:ClearFailed = $true
    } else {
        Write-ToolLine 'ok' $Label "removed $Path"
    }
}

$scope = if ($Arch) { $Arch } else { 'all arches' }
Write-Host "GPU Unlock clear  (repo $($script:RepoUefi))"
Write-Host "Scope: $scope"
Write-Host ''

$buildDir = Join-Path $script:RepoUefi 'build'
Write-Host 'Host build output:'
if ($Arch) {
    $ArchInfo = Resolve-GpuUnlockArch -Arch $Arch
    $efiNames = @(
        (Get-GpuUnlockHostEfiName $ArchInfo),
        "$($script:Edk2BaseName)-$($ArchInfo.Name).efi",
        "$($script:Edk2BaseName).efi",
        "$($script:HostEfiStem).efi"
    ) | Select-Object -Unique
    foreach ($name in $efiNames) {
        Remove-ClearPath -Path (Join-Path $buildDir $name) -Label $name
    }
} elseif (Test-Path -LiteralPath $buildDir) {
    Get-ChildItem -LiteralPath $buildDir -Force | ForEach-Object {
        Remove-ClearPath -Path $_.FullName -Label $_.Name
    }
    if (-not (Get-ChildItem -LiteralPath $buildDir -Force -ErrorAction SilentlyContinue)) {
        Write-ToolLine 'ok' 'build directory' $buildDir
    }
} else {
    Write-ToolLine 'ok' 'build directory' 'already absent'
}

Write-Host ''
Write-Host 'EDK2 Build trees:'
$resolvedEdk2 = Resolve-Edk2Path -Hint $Edk2Path
if (-not $resolvedEdk2) {
    Write-ToolLine 'ok' 'EDK2' 'not found; skipped Build trees'
} else {
    $names = @()
    if ($Arch) {
        $names = @((Resolve-GpuUnlockArch -Arch $Arch).OutputRel)
    } else {
        $names = @(
            'Build\GpuUnlock',
            'Build\GpuUnlockAmpere',
            'Build\GpuUnlockAda',
            'Build\TuringUnlock'
        )
    }
    foreach ($rel in $names) {
        $leaf = Split-Path -Leaf $rel
        Remove-ClearPath -Path (Join-Path $resolvedEdk2 $rel) -Label $leaf
    }
}

$cmdPath = Join-Path $env:TEMP 'gpu-unlock-edk2-build.cmd'
Write-Host ''
Write-Host 'Temp:'
Remove-ClearPath -Path $cmdPath -Label 'edk2 build cmd'

if ($Firmware) {
    Write-Host ''
    Write-Host 'Staged firmware:'
    $fwDir = Join-Path $script:RepoUefi 'assets\firmware'
    $dirs = @($fwDir)
    if ($Arch) {
        $info = Resolve-GpuUnlockArch -Arch $Arch
        $blobs = Get-ArchFirmwareBlobs $info
        $dirs = @($fwDir, (Get-StagedFirmwareDir $info))
    } else {
        $blobs = @($script:FirmwareBlobs)
        foreach ($name in $script:ArchNames) {
            $info = Resolve-GpuUnlockArch -Arch $name
            $blobs += Get-ArchFirmwareBlobs $info
            $dirs += Get-StagedFirmwareDir $info
        }
        $blobs = @($blobs | Select-Object -Unique)
        $dirs = @($dirs | Select-Object -Unique)
    }
    $blobs += 'NVIDIA-LICENSE'
    foreach ($dir in $dirs) {
        foreach ($name in $blobs) {
            Remove-ClearPath -Path (Join-Path $dir $name) -Label "$(Split-Path -Leaf $dir)\$name"
        }
    }
}

Write-Host ''
if ($script:ClearFailed) {
    throw 'Clear incomplete.'
}
Write-Host 'Clear complete.'
Write-Host 'Rebuild with:'
$rebuildArch = if ($Arch) { $Arch } else { 'Turing' }
Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1 -Arch $rebuildArch"
