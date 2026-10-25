# SPDX-License-Identifier: GPL-2.0-only
# Check host tools and source layout, then copy/link GpuUnlockPkg into EDK2.
# Optional: clone EDK2, download NASM, stage firmware blobs from assets\firmware\<Arch>.
[CmdletBinding()]
param(
    [string]$Edk2Path = $env:EDK2_PATH,
    [string]$NasmPrefix = $env:NASM_PREFIX,
    [ValidateSet('Turing', 'Ampere', 'Ada')]
    [string]$Arch = 'Turing',
    [switch]$FetchEdk2,
    [switch]$FetchNasm
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Lib.ps1')

$ArchInfo = Resolve-GpuUnlockArch -Arch $Arch
Write-Host "GPU Unlock prepare  (repo $($script:RepoUefi))"
Write-Host "GPU arch: $($ArchInfo.Name)  (GPU_UNLOCK_ARCH=$($ArchInfo.Define); compile this arch only)"
Write-Host ''

Test-SourceLayout -ArchInfo $ArchInfo
Write-Host ''

$failed = $false
Write-Host 'Host tool check:'

$os = [Environment]::OSVersion.VersionString
if ($env:PROCESSOR_ARCHITECTURE -match '64') {
    Write-ToolLine 'ok' 'Windows' $os
} else {
    Write-ToolLine 'fail' 'Windows' "need x64, got $env:PROCESSOR_ARCHITECTURE"
    $failed = $true
}

$vs = Find-VsInstall
if ($vs -and $vs.HasVcTools) {
    $label = $vs.DisplayName
    if ($vs.Version) { $label = "$label $($vs.Version)" }
    Write-ToolLine 'ok' 'VS C++' "$label  $($vs.Path)"
    if ($vs.Version -and $vs.Version -notmatch '^17\.') {
        Write-ToolLine 'warn' 'VS version' "EDK2 -t VS2022 expects Visual Studio 2022 (17.x); found $($vs.Version)"
    }
} elseif ($vs) {
    Write-ToolLine 'fail' 'VS C++' "$($vs.Path) (MSVC v143 / C++ tools missing)"
    $failed = $true
} else {
    Write-ToolLine 'fail' 'VS C++' 'Visual Studio with MSVC x64 tools not found (need VS2022 Desktop development with C++).'
    $failed = $true
}

$python = Find-Python
if ($python -and (Test-PythonVersion $python.Version)) {
    Write-ToolLine 'ok' 'Python' "$($python.Version)  $($python.Exe)"
} elseif ($python) {
    Write-ToolLine 'fail' 'Python' "$($python.Version) found; need 3.6+"
    $failed = $true
} else {
    Write-ToolLine 'fail' 'Python' 'Python 3.6+ not found. Install from https://www.python.org/downloads/ and enable PATH.'
    $failed = $true
}

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($git) {
    Write-ToolLine 'ok' 'git' $git.Source
} else {
    Write-ToolLine 'warn' 'git' 'optional after EDK2 is already cloned; required for -FetchEdk2'
}

$iasl = Get-Command iasl.exe -ErrorAction SilentlyContinue
if ($iasl) {
    Write-ToolLine 'ok' 'iasl' $iasl.Source
} else {
    Write-ToolLine 'warn' 'iasl' 'optional; not needed for UnlockLoader-<Arch>.efi'
}

$nasmExe = Find-NasmExe -Hint $NasmPrefix
if (-not $nasmExe -and $FetchNasm) {
    $nasmExe = Get-NasmZip
}
if ($nasmExe) {
    $nasmVer = Test-NasmVersion $nasmExe
    if ($nasmVer.Ok) {
        Write-ToolLine 'ok' 'NASM' "$nasmExe  $($nasmVer.Text)"
    } else {
        Write-ToolLine 'fail' 'NASM' "$nasmExe  $($nasmVer.Text) (need 2.15.05 or later)"
        $failed = $true
    }
} else {
    Write-ToolLine 'fail' 'NASM' "nasm.exe not found. Re-run with -FetchNasm or download $($script:NasmDownload)"
    Write-Host "         Expected path: $(Join-Path $script:RepoUefi 'nasm-3.02\nasm.exe')" -ForegroundColor Red
    Write-Host "         Site: $($script:NasmHome)" -ForegroundColor Red
    $failed = $true
}

$resolvedEdk2 = Resolve-Edk2Path -Hint $Edk2Path
if (-not $resolvedEdk2 -and $FetchEdk2) {
    $resolvedEdk2 = Get-Edk2Clone
}
if ($resolvedEdk2) {
    Write-ToolLine 'ok' 'EDK2' $resolvedEdk2
} else {
    Write-ToolLine 'fail' 'EDK2' "edksetup.bat not found. Re-run with -FetchEdk2 or pass -Edk2Path / set EDK2_PATH."
    $failed = $true
}

if ($failed) {
    Write-Host ''
    Write-Host 'Prepare failed. Fix the fail items, then rerun.' -ForegroundColor Red
    Write-Host "  NASM:  powershell -File .\tools\Prepare-GpuUnlock.ps1 -FetchNasm"
    Write-Host "  EDK2:  powershell -File .\tools\Prepare-GpuUnlock.ps1 -FetchEdk2"
    throw 'Prepare incomplete.'
}

Write-Host ''
Write-Host 'EDK2 tree check:'
Test-Edk2Tree -Edk2Path $resolvedEdk2

Write-Host ''
Write-Host 'Preparing workspace:'
Initialize-Edk2Conf -Edk2Path $resolvedEdk2
Install-Edk2PackageLink -Edk2Path $resolvedEdk2
Install-FirmwareStage -ArchInfo $ArchInfo
Initialize-BuildDir

Write-Host ''
Write-Host 'Prepare complete.'
Write-Host "  Arch    : $($ArchInfo.Name)"
Write-Host "  Package : $($script:Pkg)"
Write-Host "  EDK2    : $resolvedEdk2"
Write-Host "  NASM    : $nasmExe"
Write-Host 'Build with:'
Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1 -Arch $($ArchInfo.Name)"
