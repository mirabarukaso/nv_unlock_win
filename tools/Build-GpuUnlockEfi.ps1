# SPDX-License-Identifier: GPL-2.0-only
# Prepare (tools + layout + EDK2 package link) then build UnlockLoader-<Arch>.efi.
[CmdletBinding()]
param(
    [string]$Edk2Path = $env:EDK2_PATH,
    [string]$NasmPrefix = $env:NASM_PREFIX,
    [ValidateSet('DEBUG', 'RELEASE')]
    [string]$Target = 'DEBUG',
    [string]$ToolChain = 'VS2022',
    [ValidateSet('Turing', 'Ampere', 'Ada')]
    [string]$Arch = 'Turing',
    [switch]$SkipPrepare,
    [switch]$FetchEdk2,
    [switch]$FetchNasm
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Lib.ps1')

$ArchInfo = Resolve-GpuUnlockArch -Arch $Arch
Write-Host "Building $($ArchInfo.Name) only (GPU_UNLOCK_ARCH=$($ArchInfo.Define))"

if (-not $SkipPrepare) {
    $prep = Join-Path $PSScriptRoot 'Prepare-GpuUnlock.ps1'
    $prepArgs = @{ Arch = $ArchInfo.Name }
    if ($Edk2Path) { $prepArgs.Edk2Path = $Edk2Path }
    if ($NasmPrefix) { $prepArgs.NasmPrefix = $NasmPrefix }
    if ($FetchEdk2) { $prepArgs.FetchEdk2 = $true }
    if ($FetchNasm) { $prepArgs.FetchNasm = $true }
    & $prep @prepArgs
}

$Edk2Path = Resolve-Edk2Path -Hint $Edk2Path
if (-not $Edk2Path) {
    throw 'EDK2 not found after prepare. Pass -Edk2Path or -FetchEdk2.'
}
$NasmExe = Find-NasmExe -Hint $NasmPrefix
if (-not $NasmExe) {
    throw 'nasm.exe not found after prepare. Pass -NasmPrefix or -FetchNasm.'
}
$NasmPrefixResolved = Get-NasmPrefix $NasmExe
$Python = Find-Python
if (-not $Python) {
    throw 'Python 3 not found after prepare.'
}

Install-Edk2PackageLink -Edk2Path $Edk2Path

$EdkToolsBin = Get-EdkToolsBin -Edk2Path $Edk2Path
if (-not $EdkToolsBin) {
    throw "BaseTools Bin Win32 is missing GenFw.exe under $Edk2Path\BaseTools\Bin\Win32. Build BaseTools once: from an x86 Native Tools prompt run edksetup.bat Rebuild. A missing submodule is a separate failure."
}

$pythonCmd = $Python.Command
if ($pythonCmd -ne 'py -3' -and $pythonCmd -match '\s' -and $pythonCmd -notmatch '^".*"$') {
    $pythonCmd = '"' + $pythonCmd + '"'
}
# Pre-set EDK_TOOLS_BIN. edk2 toolsetup.bat sets it inside a parenthesized
# block and then tests %EDK_TOOLS_BIN% without delayed expansion, so an unset
# variable prints "Cannot find BaseTools Bin Win32" even when the directory exists.
$BuildScript = @"
@echo off
set "NASM_PREFIX=$NasmPrefixResolved"
set "EDK_TOOLS_BIN=$EdkToolsBin"
set PATH=%NASM_PREFIX%;%EDK_TOOLS_BIN%;%PATH%
set PYTHON_COMMAND=$pythonCmd
call edksetup.bat $ToolChain
if errorlevel 1 call edksetup.bat
set PACKAGES_PATH=$Edk2Path
where nasm
where GenFw
build -a X64 -t $ToolChain -b $Target -p GpuUnlockPkg\GpuUnlockPkg.dsc -D GPU_UNLOCK_ARCH=$($ArchInfo.Define)
"@
$CmdPath = Join-Path $env:TEMP 'gpu-unlock-edk2-build.cmd'
Set-Content -Path $CmdPath -Value $BuildScript -Encoding ASCII

Write-Host "NASM_PREFIX=$NasmPrefixResolved"
Write-Host "EDK_TOOLS_BIN=$EdkToolsBin"
Push-Location $Edk2Path
try {
    $env:NASM_PREFIX = $NasmPrefixResolved
    $env:EDK_TOOLS_BIN = $EdkToolsBin
    cmd /c $CmdPath
    if ($LASTEXITCODE -ne 0) {
        throw "edk2 build failed with exit $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$OutRoot = Join-Path $Edk2Path $ArchInfo.OutputRel
$Edk2Efi = "$($script:Edk2BaseName).efi"
$Built = Join-Path $OutRoot "X64\$Target`_$ToolChain\X64\$Edk2Efi"
$Alt = Get-ChildItem -Path $OutRoot -Recurse -Filter $Edk2Efi -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (Test-Path $Built) {
    $Out = $Built
} elseif ($Alt) {
    $Out = $Alt.FullName
} else {
    throw "Build reported success but $Edk2Efi was not found under $OutRoot."
}

$DestDir = Join-Path $script:RepoUefi 'build'
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
$DestArch = Join-Path $DestDir (Get-GpuUnlockHostEfiName $ArchInfo)
Copy-Item -Force $Out $DestArch
# EDK2 BASE_NAME stays TuringUnlockLoader (ESP / install). Host copies are
# UnlockLoader-<Arch>.efi. Drop leftover unsuffixed and old-prefixed names.
foreach ($legacyName in @(
        $Edk2Efi,
        "$($script:HostEfiStem).efi",
        "$($script:Edk2BaseName)-$($ArchInfo.Name).efi"
    )) {
    $legacy = Join-Path $DestDir $legacyName
    if ((Test-Path -LiteralPath $legacy) -and ($legacy -ne $DestArch)) {
        Remove-Item -LiteralPath $legacy -Force
    }
}
Write-Host "Built $($ArchInfo.Name): $DestArch"
Write-Host "Install with (admin):"
Write-Host "  powershell -ExecutionPolicy Bypass -File `"$(Join-Path $script:RepoUefi 'tools\Install-GpuUnlockBoot.ps1')`" -LoaderEfi `"$DestArch`""
