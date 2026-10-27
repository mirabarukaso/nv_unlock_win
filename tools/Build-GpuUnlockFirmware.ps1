# SPDX-License-Identifier: GPL-2.0-only
# Fetch pinned NVIDIA inputs and generate per-arch firmware blobs into
# assets\firmware\<Arch>\. Run from the uefi directory. Does not need the
# parent Linux module tree.
[CmdletBinding()]
param(
    [ValidateSet('Turing', 'Ampere', 'Ada')]
    [string]$Arch = 'Turing',
    [string]$CacheDir,
    [string]$OutputDir,
    [switch]$Offline
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Lib.ps1')

$ArchInfo = Resolve-GpuUnlockArch -Arch $Arch
$ArchKey = $ArchInfo.Name.ToLowerInvariant()
$FwDir = Join-Path $script:RepoUefi "firmware\$ArchKey"
if (-not (Test-Path -LiteralPath (Join-Path $FwDir 'fetch_artifacts.py'))) {
    throw "Missing firmware scripts for $Arch at $FwDir"
}

if (-not $CacheDir) {
    $CacheDir = Join-Path $script:RepoUefi '.cache\firmware'
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $script:RepoUefi "assets\firmware\$($ArchInfo.Name)"
}

$Inputs = Join-Path $CacheDir 'inputs'
$SrcRoot = Join-Path $CacheDir 'src'
New-Item -ItemType Directory -Force -Path $Inputs, $SrcRoot, $OutputDir | Out-Null

$python = Find-Python
if (-not $python) {
    throw 'Python 3.6+ not found. Install from https://www.python.org/downloads/ and enable PATH.'
}

Write-Host "GPU Unlock firmware  (repo $($script:RepoUefi))"
Write-Host "Arch    : $($ArchInfo.Name)"
Write-Host "Scripts : $FwDir"
Write-Host "Cache   : $CacheDir"
Write-Host "Output  : $OutputDir"
Write-Host ''

# PowerShell 5.1 turns a native stderr stream into a terminating error when
# $ErrorActionPreference is Stop, so a missing module never reached pip.
$zstPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $python.Exe -c 'import zstandard' 2>$null | Out-Null
$zstOk = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $zstPreference
if (-not $zstOk) {
    Write-Host 'Installing Python package zstandard (needed to read nvidia-utils .pkg.tar.zst on Windows)...'
    $ErrorActionPreference = 'Continue'
    & $python.Exe -m pip install --user zstandard
    $pipOk = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = 'Stop'
    if (-not $pipOk) {
        throw 'pip install zstandard failed. Install zstd.exe or: python -m pip install zstandard'
    }
}

$fetch = Join-Path $FwDir 'fetch_artifacts.py'
$fetchArgs = @($fetch, $Inputs)
if ($Offline) { $fetchArgs += '--offline' }
Write-Host "Fetching pinned inputs..."
& $python.Exe @fetchArgs
if ($LASTEXITCODE -ne 0) {
    throw "fetch_artifacts.py failed for $Arch"
}

$nvSource = Get-ChildItem -LiteralPath $Inputs -Filter 'NVIDIA-kernel-module-source-*.tar.xz' | Select-Object -First 1
$nvUtils = Get-ChildItem -LiteralPath $Inputs -Filter 'nvidia-utils-*.pkg.tar.zst' | Select-Object -First 1
if (-not $nvSource -or -not $nvUtils) {
    throw "Pinned NVIDIA inputs missing under $Inputs"
}

$srcMarker = Join-Path $SrcRoot 'NVIDIA-kernel-module-source-610.43.03\src\nvidia\generated'
if (-not (Test-Path -LiteralPath $srcMarker)) {
    Write-Host "Extracting $($nvSource.Name) (Python tarfile, no xz.exe required)..."
    $extractPy = @"
import sys, tarfile
from pathlib import Path
src, dest = Path(sys.argv[1]), Path(sys.argv[2])
dest.mkdir(parents=True, exist_ok=True)
with tarfile.open(src, 'r:xz') as archive:
    try:
        archive.extractall(dest, filter='data')
    except TypeError:
        archive.extractall(dest)
"@
    & $python.Exe -c $extractPy $nvSource.FullName $SrcRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract $($nvSource.Name)"
    }
}

$env:PYTHONPATH = Join-Path $script:RepoUefi 'firmware'
$gen = Join-Path $FwDir 'generate_v67.py'
Write-Host "Generating V67 / GSP from $($nvUtils.Name)..."
& $python.Exe $gen $nvUtils.FullName $OutputDir
if ($LASTEXITCODE -ne 0) {
    throw "generate_v67.py failed for $Arch"
}

$boot = Join-Path $FwDir 'extract_boot_assets.py'
$sourceTree = Join-Path $SrcRoot 'NVIDIA-kernel-module-source-610.43.03'
Write-Host "Extracting Booter / GSP boot assets..."
$bootArgs = @($boot, $sourceTree, $OutputDir)
if ($ArchInfo.Name -eq 'Ada') {
    $bootArgs += @('--chip', 'ad102')
}
& $python.Exe @bootArgs
if ($LASTEXITCODE -ne 0) {
    throw "extract_boot_assets.py failed for $Arch"
}

$blobs = Get-ArchFirmwareBlobs $ArchInfo
$missing = @()
foreach ($name in $blobs) {
    $path = Join-Path $OutputDir $name
    if (Test-Path -LiteralPath $path) {
        $item = Get-Item -LiteralPath $path
        Write-ToolLine 'ok' $name "$($item.Length) bytes"
    } else {
        Write-ToolLine 'fail' $name 'missing'
        $missing += $name
    }
}
if ($missing.Count -gt 0) {
    throw "Firmware generate incomplete: $($missing -join ', ')"
}

Write-Host ''
Write-Host "Firmware complete: $OutputDir"
Write-Host 'Install with (admin):'
Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\Install-GpuUnlockBoot.ps1 -LoaderEfi .\build\UnlockLoader-$($ArchInfo.Name).efi -FirmwareDir `"$OutputDir`""
