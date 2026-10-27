# SPDX-License-Identifier: GPL-2.0-only
# Shared helpers for Prepare-GpuUnlock.ps1 and Build-GpuUnlockEfi.ps1.

$script:RepoUefi = Split-Path -Parent $PSScriptRoot
$script:Pkg = Join-Path $script:RepoUefi 'GpuUnlockPkg'
$script:NasmDownload = 'https://www.nasm.us/pub/nasm/releasebuilds/3.02/win64/nasm-3.02-win64.zip'
$script:NasmHome = 'https://www.nasm.us/'
$script:Edk2Repo = 'https://github.com/tianocore/edk2.git'
# 202408 has no CompilerIntrinsicsLib and a dead Zeex/subhook URL.
# 202411 has the library, but *_ms.c typedefs size_t as 32-bit on x64 (C2371).
# 202511+ uses unsigned __int64. Pin the stable tag this tree builds with.
$script:Edk2Tag = 'edk2-stable202608'
$script:LoaderInfRel = 'GpuUnlockPkg\Application\Loader\Loader.inf'
$script:ArchNames = @('Turing', 'Ampere', 'Ada')
# EDK2 INF BASE_NAME and the ESP copy under EFI\turing-unlock\.
$script:Edk2BaseName = 'TuringUnlockLoader'
# Host build output in uefi\build\ (arch is already in the suffix).
$script:HostEfiStem = 'UnlockLoader'

$script:ExpectedDirs = @(
    'GpuUnlockPkg',
    'GpuUnlockPkg\Application\Loader',
    'GpuUnlockPkg\Arch',
    'GpuUnlockPkg\Arch\Turing',
    'GpuUnlockPkg\Arch\Ampere',
    'GpuUnlockPkg\Arch\Ada',
    'GpuUnlockPkg\Include',
    'tools',
    'assets',
    'bench',
    'firmware',
    'firmware\turing',
    'firmware\ampere',
    'firmware\ada'
)

$script:ExpectedFiles = @(
    'GpuUnlockPkg\GpuUnlockPkg.dsc',
    'GpuUnlockPkg\GpuUnlockPkg.dec',
    'GpuUnlockPkg\Application\Loader\Loader.inf',
    'GpuUnlockPkg\Include\TuCompat.h',
    'GpuUnlockPkg\Include\TuArch.h',
    'GpuUnlockPkg\Include\TuConfig.h',
    'GpuUnlockPkg\Include\TuPci.h',
    'GpuUnlockPkg\Include\TuUnlock.h',
    'GpuUnlockPkg\Include\TuChainload.h',
    'GpuUnlockPkg\Include\TuFs.h',
    'GpuUnlockPkg\Include\TuMenu.h',
    'tools\Lib.ps1',
    'tools\Prepare-GpuUnlock.ps1',
    'tools\Build-GpuUnlockEfi.ps1',
    'tools\Clear-GpuUnlock.ps1',
    'tools\Build-GpuUnlockFirmware.ps1',
    'tools\Install-GpuUnlockBoot.ps1',
    'tools\Protect-GpuUnlockBoot.ps1',
    'tools\Uninstall-GpuUnlockBoot.ps1',
    'tools\Copy-GpuUnlockLog.ps1',
    'tools\New-GpuUnlockUsb.ps1',
    'tools\Common.ps1',
    'assets\config.ini',
    'firmware\README.md',
    'firmware\zstio.py',
    'firmware\turing\fetch_artifacts.py',
    'firmware\turing\generate_v67.py',
    'firmware\turing\extract_boot_assets.py',
    'firmware\ampere\fetch_artifacts.py',
    'firmware\ampere\generate_v67.py',
    'firmware\ampere\extract_boot_assets.py',
    'firmware\ada\fetch_artifacts.py',
    'firmware\ada\generate_v67.py',
    'firmware\ada\extract_boot_assets.py',
    'bench\gemm_fp16.py',
    'VERSION',
    'LICENSE',
    'NOTICE'
)

$script:CommonLoaderSources = @(
    'Loader.c',
    'Log.c',
    'Fs.c',
    'Config.c',
    'Pci.c',
    'Menu.c',
    'Chainload.c',
    'Arch.c'
)

$script:ArchCatalog = @{
    Turing = @{
        Name           = 'Turing'
        Define         = 'TURING'
        Implemented    = $true
        OutputRel      = 'Build\GpuUnlock'
        InfRel         = 'GpuUnlockPkg\Application\Loader\Loader.inf'
        Firmware       = $true
        KernelPortable = $true
        FirmwareBlobs  = @(
            'booter_load_tu102.bin',
            'v67-tu102-610.43.03.bin',
            'gsp_tu10x.bin',
            'gsp_boot_tu102.bin',
            'gsp_boot_desc_tu102.bin',
            'sec2_bl_tu102.bin'
        )
        Dirs           = @(
            'GpuUnlockPkg\Arch\Turing',
            'GpuUnlockPkg\Arch\Turing\Portable'
        )
        Files          = @(
            'GpuUnlockPkg\Arch\Turing\Unlock.c',
            'GpuUnlockPkg\Arch\Turing\TuringIds.c',
            'GpuUnlockPkg\Arch\Turing\boot.h',
            'GpuUnlockPkg\Arch\Turing\diagnostic.h',
            'GpuUnlockPkg\Arch\Turing\gfw_eval.h',
            'GpuUnlockPkg\Arch\Turing\layout.h',
            'GpuUnlockPkg\Arch\Turing\prep.h',
            'GpuUnlockPkg\Arch\Turing\vbios.h',
            'GpuUnlockPkg\Arch\Turing\Portable\boot.c',
            'GpuUnlockPkg\Arch\Turing\Portable\gfw_eval.c',
            'GpuUnlockPkg\Arch\Turing\Portable\layout.c',
            'GpuUnlockPkg\Arch\Turing\Portable\prep.c',
            'GpuUnlockPkg\Arch\Turing\Portable\vbios.c'
        )
        InfSources     = @(
            '../../Arch/Turing/TuringIds.c',
            '../../Arch/Turing/Unlock.c',
            '../../Arch/Turing/Portable/boot.c',
            '../../Arch/Turing/Portable/gfw_eval.c',
            '../../Arch/Turing/Portable/layout.c',
            '../../Arch/Turing/Portable/prep.c',
            '../../Arch/Turing/Portable/vbios.c'
        )
    }
    Ampere = @{
        Name           = 'Ampere'
        Define         = 'AMPERE'
        Implemented    = $true
        OutputRel      = 'Build\GpuUnlockAmpere'
        InfRel         = 'GpuUnlockPkg\Application\Loader\LoaderAmpere.inf'
        Firmware       = $true
        KernelPortable = $false
        FirmwareBlobs  = @(
            'booter_load_ga102.bin',
            'v67-ga102-610.43.03.bin',
            'gsp_ga10x.bin',
            'gsp_boot_ga102.bin',
            'gsp_boot_desc_ga102.bin'
        )
        Dirs           = @(
            'GpuUnlockPkg\Arch\Ampere',
            'GpuUnlockPkg\Arch\Ampere\Portable'
        )
        Files          = @(
            'GpuUnlockPkg\Arch\Ampere\Ampere.c',
            'GpuUnlockPkg\Arch\Ampere\Unlock.c',
            'GpuUnlockPkg\Arch\Ampere\diagnostic.h',
            'GpuUnlockPkg\Arch\Ampere\gfw_eval.h',
            'GpuUnlockPkg\Arch\Ampere\layout.h',
            'GpuUnlockPkg\Arch\Ampere\prep.h',
            'GpuUnlockPkg\Arch\Ampere\vbios.h',
            'GpuUnlockPkg\Arch\Ampere\Portable\gfw_eval.c',
            'GpuUnlockPkg\Arch\Ampere\Portable\layout.c',
            'GpuUnlockPkg\Arch\Ampere\Portable\prep.c',
            'GpuUnlockPkg\Arch\Ampere\Portable\vbios.c',
            'GpuUnlockPkg\Application\Loader\LoaderAmpere.inf'
        )
        InfSources     = @(
            '../../Arch/Ampere/Ampere.c',
            '../../Arch/Ampere/Unlock.c',
            '../../Arch/Ampere/Portable/gfw_eval.c',
            '../../Arch/Ampere/Portable/layout.c',
            '../../Arch/Ampere/Portable/prep.c',
            '../../Arch/Ampere/Portable/vbios.c'
        )
    }
    Ada = @{
        Name           = 'Ada'
        Define         = 'ADA'
        Implemented    = $true
        OutputRel      = 'Build\GpuUnlockAda'
        InfRel         = 'GpuUnlockPkg\Application\Loader\LoaderAda.inf'
        Firmware       = $true
        KernelPortable = $false
        FirmwareBlobs  = @(
            'booter_load_ad102.bin',
            'v67-ad102-610.43.03-four-plm.bin',
            'gsp_ga10x.bin',
            'gsp_boot_ad102.bin',
            'gsp_boot_desc_ad102.bin'
        )
        Dirs           = @(
            'GpuUnlockPkg\Arch\Ada',
            'GpuUnlockPkg\Arch\Ada\Portable'
        )
        Files          = @(
            'GpuUnlockPkg\Arch\Ada\Ada.c',
            'GpuUnlockPkg\Arch\Ada\Unlock.c',
            'GpuUnlockPkg\Arch\Ada\arch.h',
            'GpuUnlockPkg\Arch\Ada\diagnostic.h',
            'GpuUnlockPkg\Arch\Ada\gfw_eval.h',
            'GpuUnlockPkg\Arch\Ada\layout.h',
            'GpuUnlockPkg\Arch\Ada\prep.h',
            'GpuUnlockPkg\Arch\Ada\vbios.h',
            'GpuUnlockPkg\Arch\Ada\Portable\gfw_eval.c',
            'GpuUnlockPkg\Arch\Ada\Portable\layout.c',
            'GpuUnlockPkg\Arch\Ada\Portable\prep.c',
            'GpuUnlockPkg\Arch\Ada\Portable\vbios.c',
            'GpuUnlockPkg\Application\Loader\LoaderAda.inf'
        )
        InfSources     = @(
            '../../Arch/Ada/Ada.c',
            '../../Arch/Ada/Unlock.c',
            '../../Arch/Ada/Portable/gfw_eval.c',
            '../../Arch/Ada/Portable/layout.c',
            '../../Arch/Ada/Portable/prep.c',
            '../../Arch/Ada/Portable/vbios.c'
        )
    }
}

$script:FirmwareBlobs = @(
    'booter_load_tu102.bin',
    'v67-tu102-610.43.03.bin',
    'gsp_tu10x.bin',
    'gsp_boot_tu102.bin',
    'gsp_boot_desc_tu102.bin',
    'sec2_bl_tu102.bin'
)

$script:KernelPortableMap = @{
    'boot.c'     = 'GpuUnlockPkg\Arch\Turing\Portable\boot.c'
    'gfw_eval.c' = 'GpuUnlockPkg\Arch\Turing\Portable\gfw_eval.c'
    'layout.c'   = 'GpuUnlockPkg\Arch\Turing\Portable\layout.c'
    'prep.c'     = 'GpuUnlockPkg\Arch\Turing\Portable\prep.c'
    'vbios.c'    = 'GpuUnlockPkg\Arch\Turing\Portable\vbios.c'
}

function Write-ToolLine([string]$Status, [string]$Name, [string]$Detail) {
    $color = switch ($Status) {
        'ok'   { 'Green' }
        'warn' { 'Yellow' }
        default { 'Red' }
    }
    Write-Host ("  [{0,-4}] {1,-28} {2}" -f $Status, $Name, $Detail) -ForegroundColor $color
}

function Test-SamePath([string]$A, [string]$B) {
    if (-not $A -or -not $B) { return $false }
    $ra = [IO.Path]::GetFullPath($A).TrimEnd('\', '/')
    $rb = [IO.Path]::GetFullPath($B).TrimEnd('\', '/')
    return $ra.Equals($rb, [StringComparison]::OrdinalIgnoreCase)
}

function Get-ReparseTarget([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item) { return $null }
    if ($item.LinkType -in @('Junction', 'SymbolicLink')) {
        $t = $item.Target
        if ($t -is [array]) { $t = $t[0] }
        if ($t) { return [string]$t }
    }
    return $null
}

function Resolve-GpuUnlockArch {
    param(
        [ValidateSet('Turing', 'Ampere', 'Ada')]
        [string]$Arch = 'Turing'
    )
    $entry = $script:ArchCatalog[$Arch]
    if (-not $entry) {
        throw "Unknown GPU arch '$Arch'. Use Turing, Ampere, or Ada."
    }
    return [pscustomobject]@{
        Name           = [string]$entry.Name
        Define         = [string]$entry.Define
        Implemented    = [bool]$entry.Implemented
        OutputRel      = [string]$entry.OutputRel
        InfRel         = [string]$entry.InfRel
        Firmware       = [bool]$entry.Firmware
        KernelPortable = [bool]$entry.KernelPortable
        FirmwareBlobs  = $(if ($entry.FirmwareBlobs) { @($entry.FirmwareBlobs) } else { $null })
        Dirs           = @($entry.Dirs)
        Files          = @($entry.Files)
        InfSources     = @($entry.InfSources)
    }
}

function Get-GpuUnlockHostEfiName {
    param(
        [Parameter(Mandatory = $true)]
        $ArchInfo
    )
    return "$($script:HostEfiStem)-$($ArchInfo.Name).efi"
}

function Get-ArchFirmwareBlobs {
    param($ArchInfo)
    if ($ArchInfo -and $ArchInfo.FirmwareBlobs -and $ArchInfo.FirmwareBlobs.Count -gt 0) {
        return @($ArchInfo.FirmwareBlobs)
    }
    return @($script:FirmwareBlobs)
}

function Get-StagedFirmwareDir {
    param($ArchInfo)
    return (Join-Path $script:RepoUefi "assets\firmware\$($ArchInfo.Name)")
}

function Get-InfSources {
    param(
        [string]$InfPath,
        [string]$ArchDefine
    )
    $in = $false
    $active = $true
    $sources = New-Object System.Collections.Generic.List[string]
    foreach ($line in Get-Content -LiteralPath $InfPath) {
        $t = $line.Trim()
        if ($t -match '^\[Sources\]') { $in = $true; continue }
        if ($t.StartsWith('[')) { $in = $false; continue }
        if (-not $in) { continue }
        if ($t -match '^!if\s+\$\(GPU_UNLOCK_ARCH\)\s*==\s*(\w+)') {
            $active = ($Matches[1] -eq $ArchDefine)
            continue
        }
        if ($t -match '^!elseif\s+\$\(GPU_UNLOCK_ARCH\)\s*==\s*(\w+)') {
            $active = ($Matches[1] -eq $ArchDefine)
            continue
        }
        if ($t -match '^!else\b') {
            $active = -not $active
            continue
        }
        if ($t -match '^!endif') {
            $active = $true
            continue
        }
        if ($t.StartsWith('!')) { continue }
        if (-not $active) { continue }
        if (-not $t -or $t.StartsWith('#') -or $t.StartsWith(';')) { continue }
        $src = ($t.Split('#')[0].Split(';')[0]).Trim()
        if ($src) { $sources.Add($src) }
    }
    return $sources
}

function Find-NasmExe {
    param([string]$Hint)
    $candidates = @()
    if ($Hint) {
        $trimmed = $Hint.TrimEnd('\', '/')
        $candidates += $Hint
        $candidates += (Join-Path $trimmed 'nasm.exe')
        $candidates += (Join-Path $trimmed 'nasm')
    }
    $candidates += @(
        (Join-Path $script:RepoUefi 'nasm-3.02\nasm.exe'),
        (Join-Path $script:RepoUefi 'nasm\nasm.exe')
    )
    Get-ChildItem -Path $script:RepoUefi -Directory -Filter 'nasm-*' -ErrorAction SilentlyContinue |
        ForEach-Object { $candidates += (Join-Path $_.FullName 'nasm.exe') }
    foreach ($dir in @(
            'C:\nasm',
            'C:\Program Files\NASM',
            'C:\Program Files (x86)\NASM',
            (Join-Path $env:LOCALAPPDATA 'bin\NASM'),
            (Join-Path $env:LOCALAPPDATA 'nasm')
        )) {
        $candidates += (Join-Path $dir 'nasm.exe')
    }
    $onPath = Get-Command nasm.exe -ErrorAction SilentlyContinue
    if ($onPath) {
        $candidates += $onPath.Source
    }
    foreach ($item in $candidates) {
        if ($item -and (Test-Path -LiteralPath $item) -and -not (Test-Path -LiteralPath $item -PathType Container)) {
            return (Resolve-Path -LiteralPath $item).Path
        }
        if ($item -and (Test-Path -LiteralPath (Join-Path $item 'nasm.exe'))) {
            return (Resolve-Path -LiteralPath (Join-Path $item 'nasm.exe')).Path
        }
    }
    return $null
}

function Get-NasmPrefix([string]$NasmExe) {
    $dir = Split-Path -Parent $NasmExe
    if (-not $dir.EndsWith('\')) {
        $dir += '\'
    }
    return $dir
}

function Test-NasmVersion([string]$NasmExe) {
    $ver = ''
    try { $ver = (& $NasmExe -v 2>&1 | Select-Object -First 1) } catch { }
    $ok = $false
    if ($ver -match 'NASM version (\d+)\.(\d+)') {
        $maj = [int]$Matches[1]
        $min = [int]$Matches[2]
        $ok = ($maj -gt 2) -or ($maj -eq 2 -and $min -ge 15)
    }
    return [pscustomobject]@{ Ok = $ok; Text = [string]$ver }
}

function Find-Python {
    foreach ($cmd in @('py', 'python', 'python3')) {
        $found = Get-Command $cmd -ErrorAction SilentlyContinue
        if (-not $found) { continue }
        try {
            if ($cmd -eq 'py') {
                $ver = & py -3 -c "import sys; print(sys.version.split()[0])" 2>$null
                $exe = & py -3 -c "import sys; print(sys.executable)" 2>$null
                if ($LASTEXITCODE -eq 0 -and $exe) {
                    return [pscustomobject]@{ Command = 'py -3'; Exe = $exe.Trim(); Version = $ver.Trim() }
                }
            } else {
                $ver = & $found.Source -c "import sys; print(sys.version.split()[0])" 2>$null
                if ($LASTEXITCODE -eq 0 -and $ver) {
                    $major = [int]($ver.Trim().Split('.')[0])
                    if ($major -ge 3) {
                        return [pscustomobject]@{ Command = $found.Source; Exe = $found.Source; Version = $ver.Trim() }
                    }
                }
            }
        } catch {
        }
    }
    return $null
}

function Test-PythonVersion([string]$Version) {
    $parts = $Version.Split('.')
    if ($parts.Count -lt 2) { return $false }
    $maj = [int]$parts[0]
    $min = [int]$parts[1]
    return ($maj -gt 3) -or ($maj -eq 3 -and $min -ge 6)
}

function Find-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        return $null
    }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $ver = $null
    $name = $null
    if ($path) {
        $ver = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
        $name = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName
        return [pscustomobject]@{
            Path       = $path.Trim()
            Version    = if ($ver) { $ver.Trim() } else { '' }
            DisplayName = if ($name) { $name.Trim() } else { 'Visual Studio' }
            HasVcTools = $true
        }
    }
    $path = & $vswhere -latest -products * -property installationPath
    if ($path) {
        $ver = & $vswhere -latest -products * -property installationVersion
        $name = & $vswhere -latest -products * -property displayName
        return [pscustomobject]@{
            Path       = $path.Trim()
            Version    = if ($ver) { $ver.Trim() } else { '' }
            DisplayName = if ($name) { $name.Trim() } else { 'Visual Studio' }
            HasVcTools = $false
        }
    }
    return $null
}

function Resolve-Edk2Path {
    param([string]$Hint)
    if ($Hint -and (Test-Path (Join-Path $Hint 'edksetup.bat'))) {
        return (Resolve-Path $Hint).Path
    }
    foreach ($candidate in @(
            (Join-Path $script:RepoUefi 'edk2'),
            'C:\edk2',
            'C:\src\edk2',
            (Join-Path $env:USERPROFILE 'edk2'),
            (Join-Path $env:USERPROFILE 'src\edk2')
        )) {
        if (Test-Path (Join-Path $candidate 'edksetup.bat')) {
            return (Resolve-Path $candidate).Path
        }
    }
    return $null
}

function Test-ArchTree {
    param($ArchInfo, [switch]$Selected)
    $failed = $false
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($rel in $ArchInfo.Dirs) {
        $path = Join-Path $script:RepoUefi $rel
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            $missing.Add($rel)
        }
    }
    foreach ($rel in $ArchInfo.Files) {
        $path = Join-Path $script:RepoUefi $rel
        if (-not (Test-Path -LiteralPath $path)) {
            $missing.Add($rel)
        }
    }
    $state = if ($ArchInfo.Implemented) { 'implemented' } else { 'stub' }
    if ($Selected) {
        if ($missing.Count -gt 0) {
            Write-ToolLine 'fail' $ArchInfo.Name "selected; missing $($missing -join ', ')"
            foreach ($rel in $missing) {
                Write-ToolLine 'fail' $rel 'missing'
            }
            $failed = $true
        } elseif (-not $ArchInfo.Implemented) {
            Write-ToolLine 'warn' $ArchInfo.Name "selected, $state (inject returns EOPNOTSUPP)"
        } else {
            Write-ToolLine 'ok' $ArchInfo.Name "selected, $state, $($ArchInfo.Files.Count) files"
        }
    } else {
        if ($missing.Count -gt 0) {
            Write-ToolLine 'warn' $ArchInfo.Name "not compiled; missing $($missing -join ', ')"
        } else {
            Write-ToolLine 'ok' $ArchInfo.Name "not compiled ($state present)"
        }
    }
    return (-not $failed)
}

function Test-AsciiMetaFiles {
    $failed = $false
    $files = Get-ChildItem -Path $script:Pkg -Recurse -Include *.inf, *.dsc, *.dec -File -ErrorAction SilentlyContinue
    foreach ($f in $files) {
        $bytes = [IO.File]::ReadAllBytes($f.FullName)
        $offset = -1
        for ($i = 0; $i -lt $bytes.Length; $i++) {
            if ($bytes[$i] -gt 127) {
                $offset = $i
                break
            }
        }
        if ($offset -ge 0) {
            $rel = $f.FullName.Substring($script:RepoUefi.Length).TrimStart('\')
            Write-ToolLine 'fail' $rel "non-ASCII at offset $offset (EDK2 reads INF as the Windows ANSI code page)"
            $failed = $true
        }
    }
    if (-not $failed) {
        Write-ToolLine 'ok' 'INF/DSC/DEC encoding' 'ASCII'
    }
    return (-not $failed)
}

function Test-SourceLayout {
    param($ArchInfo)
    $failed = $false
    Write-Host 'Source layout check:'

    foreach ($rel in $script:ExpectedDirs) {
        $path = Join-Path $script:RepoUefi $rel
        if (Test-Path -LiteralPath $path -PathType Container) {
            Write-ToolLine 'ok' $rel 'directory'
        } else {
            Write-ToolLine 'fail' $rel 'missing directory'
            $failed = $true
        }
    }

    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($rel in $script:ExpectedFiles) {
        $path = Join-Path $script:RepoUefi $rel
        if (-not (Test-Path -LiteralPath $path)) {
            $missing.Add($rel)
        }
    }
    if ($missing.Count -eq 0) {
        Write-ToolLine 'ok' 'common files' "$($script:ExpectedFiles.Count) present"
    } else {
        foreach ($rel in $missing) {
            Write-ToolLine 'fail' $rel 'missing'
        }
        $failed = $true
    }

    if (-not (Test-AsciiMetaFiles)) {
        $failed = $true
    }

    Write-Host 'GPU architectures:'
    foreach ($name in $script:ArchNames) {
        $info = Resolve-GpuUnlockArch -Arch $name
        $selected = ($info.Name -eq $ArchInfo.Name)
        if (-not (Test-ArchTree -ArchInfo $info -Selected:$selected)) {
            $failed = $true
        }
    }

    $inf = Join-Path $script:RepoUefi $ArchInfo.InfRel
    if (Test-Path -LiteralPath $inf) {
        $infText = Get-Content -LiteralPath $inf -Raw
        if ($infText -notmatch "BASE_NAME\s*=\s*$($script:Edk2BaseName)") {
            Write-ToolLine 'fail' "$($ArchInfo.Name) INF BASE_NAME" "must stay $($script:Edk2BaseName) (ESP / install scripts)"
            $failed = $true
        } else {
            Write-ToolLine 'ok' "$($ArchInfo.Name) INF BASE_NAME" $script:Edk2BaseName
        }
        $flag = "TU_BUILD_ARCH_$($ArchInfo.Define)"
        if ($infText -notmatch [regex]::Escape($flag)) {
            Write-ToolLine 'fail' "$($ArchInfo.Name) INF" "missing /$flag"
            $failed = $true
        }
        $infDir = Split-Path -Parent $inf
        $sources = Get-InfSources -InfPath $inf -ArchDefine $ArchInfo.Define
        $srcMissing = New-Object System.Collections.Generic.List[string]
        foreach ($src in $sources) {
            $full = [IO.Path]::GetFullPath((Join-Path $infDir $src))
            if (-not (Test-Path -LiteralPath $full)) {
                $srcMissing.Add($src)
            }
        }
        $unexpected = @($sources | Where-Object {
            ($script:CommonLoaderSources -notcontains $_) -and ($ArchInfo.InfSources -notcontains $_)
        })
        $otherArch = @()
        foreach ($name in $script:ArchNames) {
            if ($name -eq $ArchInfo.Name) { continue }
            $otherArch += $script:ArchCatalog[$name].InfSources
        }
        $leaked = @($sources | Where-Object { $otherArch -contains $_ })
        if ($srcMissing.Count -eq 0 -and $leaked.Count -eq 0 -and $sources.Count -gt 0) {
            Write-ToolLine 'ok' 'Loader.inf [Sources]' "$($sources.Count) files for $($ArchInfo.Name)"
        } else {
            if ($sources.Count -eq 0) {
                Write-ToolLine 'fail' 'Loader.inf [Sources]' 'empty after arch filter'
                $failed = $true
            }
            foreach ($src in $srcMissing) {
                Write-ToolLine 'fail' "INF source $src" 'missing'
                $failed = $true
            }
            foreach ($src in $leaked) {
                Write-ToolLine 'fail' "INF source $src" "belongs to another arch; not compiled for $($ArchInfo.Name)"
                $failed = $true
            }
        }
        if ($unexpected.Count -gt 0 -and $leaked.Count -eq 0) {
            foreach ($src in $unexpected) {
                Write-ToolLine 'warn' "INF source $src" 'not in the arch catalog'
            }
        }
    }

    $legacy = Join-Path $script:RepoUefi 'TuringUnlockPkg'
    if (Test-Path $legacy) {
        Write-ToolLine 'warn' 'TuringUnlockPkg' 'legacy tree still present; Prepare ignores it'
    }

    if (-not (Test-VersionAlignment)) {
        $failed = $true
    }

    $parent = Split-Path -Parent $script:RepoUefi
    $kernel = Join-Path $parent 'kernel\turing_unlock'
    if ($ArchInfo.KernelPortable) {
        if (Test-Path -LiteralPath $kernel) {
            Write-ToolLine 'ok' 'Linux module' $kernel
            foreach ($name in $script:KernelPortableMap.Keys) {
                $uefiRel = $script:KernelPortableMap[$name]
                $uefiPath = Join-Path $script:RepoUefi $uefiRel
                if (-not (Test-Path -LiteralPath $uefiPath)) {
                    Write-ToolLine 'fail' $uefiRel "missing UEFI port of kernel\$name"
                    $failed = $true
                }
            }
        } else {
            Write-ToolLine 'warn' 'Linux module' 'parent kernel\turing_unlock not found (ok for a standalone uefi tree)'
        }
    } else {
        Write-ToolLine 'ok' 'Linux module' "not required for $($ArchInfo.Name)"
    }

    $magicH = Join-Path $script:RepoUefi 'GpuUnlockPkg\Include\TuConfig.h'
    if (Test-Path -LiteralPath $magicH) {
        $h = Get-Content -LiteralPath $magicH -Raw
        if ($h -notmatch 'TURING_UNLOCK_LOADER_v1') {
            Write-ToolLine 'fail' 'TU_LOADER_MAGIC' 'must stay TURING_UNLOCK_LOADER_v1'
            $failed = $true
        } else {
            Write-ToolLine 'ok' 'TU_LOADER_MAGIC' 'TURING_UNLOCK_LOADER_v1'
        }
        if ($h -notmatch 'TU_BUILD_ARCH_NAME') {
            Write-ToolLine 'fail' 'TU_BUILD_ARCH_NAME' 'missing from TuConfig.h'
            $failed = $true
        }
    }

    if ($failed) {
        throw 'Source layout incomplete. Restore the missing files listed above.'
    }
}

function Test-VersionAlignment {
    $verPath = Join-Path $script:RepoUefi 'VERSION'
    if (-not (Test-Path -LiteralPath $verPath)) {
        Write-ToolLine 'fail' 'VERSION' 'missing'
        return $false
    }
    $ver = (Get-Content -LiteralPath $verPath -TotalCount 1).Trim()
    $mismatches = New-Object System.Collections.Generic.List[string]

    $cfg = Join-Path $script:RepoUefi 'GpuUnlockPkg\Include\TuConfig.h'
    if (Test-Path -LiteralPath $cfg) {
        $raw = Get-Content -LiteralPath $cfg -Raw
        if ($raw -notmatch ('TU_LOADER_VERSION\s+"' + [regex]::Escape($ver) + '"')) {
            $mismatches.Add("TuConfig.h TU_LOADER_VERSION != $ver")
        }
    }

    $inf = Join-Path $script:RepoUefi $script:LoaderInfRel
    if (Test-Path -LiteralPath $inf) {
        $raw = Get-Content -LiteralPath $inf -Raw
        if ($raw -notmatch ('VERSION_STRING\s*=\s*' + [regex]::Escape($ver))) {
            $mismatches.Add("Loader.inf VERSION_STRING != $ver")
        }
    }

    $dec = Join-Path $script:RepoUefi 'GpuUnlockPkg\GpuUnlockPkg.dec'
    if (Test-Path -LiteralPath $dec) {
        $raw = Get-Content -LiteralPath $dec -Raw
        if ($raw -notmatch ('PACKAGE_VERSION\s*=\s*' + [regex]::Escape($ver))) {
            $mismatches.Add("GpuUnlockPkg.dec PACKAGE_VERSION != $ver")
        }
    }

    if ($mismatches.Count -eq 0) {
        Write-ToolLine 'ok' 'version alignment' $ver
    } else {
        foreach ($m in $mismatches) {
            Write-ToolLine 'fail' 'version' $m
        }
        return $false
    }

    $dsc = Join-Path $script:RepoUefi 'GpuUnlockPkg\GpuUnlockPkg.dsc'
    if (Test-Path -LiteralPath $dsc) {
        $raw = Get-Content -LiteralPath $dsc -Raw
        if ($raw -match 'PLATFORM_VERSION\s*=\s*([0-9.]+)') {
            $pv = $Matches[1]
            if ($ver.StartsWith($pv)) {
                Write-ToolLine 'ok' 'DSC PLATFORM_VERSION' $pv
            } else {
                Write-ToolLine 'warn' 'DSC PLATFORM_VERSION' "$pv (VERSION is $ver)"
            }
        }
    }
    return $true
}

function Get-EdkToolsBin {
    param([string]$Edk2Path)
    $win32 = Join-Path $Edk2Path 'BaseTools\Bin\Win32'
    $genFw = Join-Path $win32 'GenFw.exe'
    if (Test-Path -LiteralPath $genFw) {
        return (Resolve-Path -LiteralPath $win32).Path
    }
    return $null
}

function Test-Edk2Tree {
    param([string]$Edk2Path)
    $failed = $false
    $checks = @(
        @{ Rel = 'edksetup.bat'; Name = 'edksetup.bat' },
        @{ Rel = 'MdePkg\MdePkg.dec'; Name = 'MdePkg' },
        @{ Rel = 'MdePkg\MdeLibs.dsc.inc'; Name = 'MdeLibs.dsc.inc' },
        @{ Rel = 'MdeModulePkg\MdeModulePkg.dec'; Name = 'MdeModulePkg' },
        @{ Rel = 'MdePkg\Library\CompilerIntrinsicsLib\CompilerIntrinsicsLib.inf'; Name = 'CompilerIntrinsicsLib' },
        @{ Rel = 'BaseTools\toolsetup.bat'; Name = 'BaseTools' }
    )
    foreach ($c in $checks) {
        $p = Join-Path $Edk2Path $c.Rel
        if (Test-Path -LiteralPath $p) {
            Write-ToolLine 'ok' $c.Name $p
        } else {
            Write-ToolLine 'fail' $c.Name "missing $p"
            $failed = $true
        }
    }
    $win32 = Get-EdkToolsBin -Edk2Path $Edk2Path
    if ($win32) {
        Write-ToolLine 'ok' 'BaseTools Bin Win32' $win32
    } else {
        Write-ToolLine 'fail' 'BaseTools Bin Win32' 'missing GenFw.exe; build BaseTools once (edksetup.bat Rebuild from an x86 Native Tools prompt)'
        $failed = $true
    }
    if ($failed) {
        throw 'EDK2 tree is incomplete. Re-clone with submodules or pass a full tree via -Edk2Path.'
    }
}

function Initialize-Edk2Conf {
    param([string]$Edk2Path)
    $confDir = Join-Path $Edk2Path 'Conf'
    $templateDir = Join-Path $Edk2Path 'BaseTools\Conf'
    New-Item -ItemType Directory -Force -Path $confDir | Out-Null
    $copied = 0
    foreach ($name in @('target', 'tools_def', 'build_rule')) {
        $dst = Join-Path $confDir "$name.txt"
        $src = Join-Path $templateDir "$name.template"
        if (Test-Path -LiteralPath $dst) {
            continue
        }
        if (Test-Path -LiteralPath $src) {
            Copy-Item -LiteralPath $src -Destination $dst -Force
            $copied++
        }
    }
    if ($copied -gt 0) {
        Write-ToolLine 'ok' 'EDK2 Conf' "copied $copied template(s) into $confDir"
    } elseif (Test-Path (Join-Path $confDir 'target.txt')) {
        Write-ToolLine 'ok' 'EDK2 Conf' (Join-Path $confDir 'target.txt')
    } else {
        Write-ToolLine 'warn' 'EDK2 Conf' 'templates missing; first build will run edksetup.bat'
    }
}

function Copy-PackageMirror {
    param([string]$Src, [string]$Dst)
    Write-Host "Mirroring package $Src -> $Dst"
    New-Item -ItemType Directory -Force -Path $Dst | Out-Null
    $robocopy = Get-Command robocopy.exe -ErrorAction SilentlyContinue
    if ($robocopy) {
        & robocopy.exe $Src $Dst /E /IS /IT /NFL /NDL /NJH /NJS /nc /ns /np /XD .git | Out-Null
        if ($LASTEXITCODE -ge 8) {
            throw "robocopy package mirror failed with exit $LASTEXITCODE"
        }
    } else {
        Copy-Item -Path (Join-Path $Src '*') -Destination $Dst -Recurse -Force
    }
}

function Remove-StaleEdk2Link {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $target = Get-ReparseTarget $Path
    if ($target) {
        cmd /c rmdir "$Path" 2>$null | Out-Null
        if (Test-Path -LiteralPath $Path) {
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        }
        return
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer) {
        Remove-Item -LiteralPath $Path -Force
    }
}

function Install-Edk2PackageLink {
    param([string]$Edk2Path)
    if (-not $Edk2Path) {
        throw 'EDK2 path is empty; cannot link GpuUnlockPkg.'
    }
    if (-not (Test-Path -LiteralPath $script:Pkg)) {
        throw "GpuUnlockPkg missing at $($script:Pkg)"
    }

    $legacy = Join-Path $Edk2Path 'TuringUnlockPkg'
    if (Test-Path -LiteralPath $legacy) {
        $legacyTarget = Get-ReparseTarget $legacy
        if ($legacyTarget) {
            Remove-StaleEdk2Link $legacy
            Write-ToolLine 'ok' 'legacy junction' "removed $legacy"
        } else {
            Write-ToolLine 'warn' 'TuringUnlockPkg' "left $legacy in place (not a junction)"
        }
    }

    $link = Join-Path $Edk2Path 'GpuUnlockPkg'
    $existingTarget = $null
    if (Test-Path -LiteralPath $link) {
        $existingTarget = Get-ReparseTarget $link
    }

    if ($existingTarget -and (Test-SamePath $existingTarget $script:Pkg)) {
        Write-ToolLine 'ok' 'package junction' "$link -> $($script:Pkg)"
    } else {
        if (Test-Path -LiteralPath $link) {
            if ($existingTarget) {
                Remove-StaleEdk2Link $link
            } elseif ((Get-ReparseTarget $link) -eq $null -and (Get-Item $link).PSIsContainer) {
                # Real directory from a previous copy fallback. Refresh below.
            } else {
                Remove-StaleEdk2Link $link
            }
        }

        $needCopy = $false
        if (-not (Test-Path -LiteralPath $link)) {
            Write-Host "Preparing EDK2 package link: $link -> $($script:Pkg)"
            cmd /c mklink /J "$link" "$($script:Pkg)" | Out-Null
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $link)) {
                $needCopy = $true
            } else {
                Write-ToolLine 'ok' 'package junction' $link
            }
        } else {
            $needCopy = $true
        }

        if ($needCopy) {
            Write-Host 'Junction unavailable, copying package into EDK2 tree.'
            Copy-PackageMirror -Src $script:Pkg -Dst $link
            Write-ToolLine 'ok' 'package copy' $link
        }
    }

    $dsc = Join-Path $link 'GpuUnlockPkg.dsc'
    $inf = Join-Path $link 'Application\Loader\Loader.inf'
    if (-not (Test-Path -LiteralPath $dsc)) {
        throw "Prepared package is missing GpuUnlockPkg.dsc at $dsc"
    }
    if (-not (Test-Path -LiteralPath $inf)) {
        throw "Prepared package is missing Loader.inf at $inf"
    }
}

function Get-FirmwareSearchRoots {
    param($ArchInfo)
    $dest = Join-Path $script:RepoUefi 'assets\firmware'
    $roots = @(
        $dest,
        (Join-Path $dest 'Turing'),
        (Join-Path $dest 'Ampere'),
        (Join-Path $dest 'Ada')
    )
    if ($ArchInfo) {
        $roots = @(
            (Join-Path $dest $ArchInfo.Name),
            $dest
        )
    }
    return $roots
}

function Install-FirmwareStage {
    param($ArchInfo)
    if ($ArchInfo -and -not $ArchInfo.Firmware) {
        Write-ToolLine 'ok' 'firmware blobs' "not used for $($ArchInfo.Name)"
        return
    }
    $blobs = Get-ArchFirmwareBlobs $ArchInfo
    $dest = Get-StagedFirmwareDir $ArchInfo
    $found = @{}
    foreach ($root in (Get-FirmwareSearchRoots -ArchInfo $ArchInfo)) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($name in $blobs) {
            if ($found.ContainsKey($name)) { continue }
            $candidate = Join-Path $root $name
            if (Test-Path -LiteralPath $candidate) {
                $found[$name] = $candidate
            }
        }
    }
    if ($found.Count -eq 0) {
        $hint = "none staged; run tools\\Build-GpuUnlockFirmware.ps1 -Arch $($ArchInfo.Name) (VM skip-inject does not need them)"
        Write-ToolLine 'warn' 'firmware blobs' $hint
        return
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    foreach ($name in $found.Keys) {
        $src = $found[$name]
        $dst = Join-Path $dest $name
        if (Test-SamePath $src $dst) { continue }
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    $have = @($blobs | Where-Object { Test-Path -LiteralPath (Join-Path $dest $_) }).Count
    $total = $blobs.Count
    if ($have -eq $total) {
        Write-ToolLine 'ok' 'firmware blobs' "$have/$total in $dest"
    } else {
        $missing = @($blobs | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dest $_)) }) -join ', '
        Write-ToolLine 'warn' 'firmware blobs' "$have/$total staged; missing $missing"
    }
}

function Initialize-BuildDir {
    $dir = Join-Path $script:RepoUefi 'build'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Write-ToolLine 'ok' 'build directory' $dir
}

function Get-NasmZip {
    $destDir = Join-Path $script:RepoUefi 'nasm-3.02'
    $exe = Join-Path $destDir 'nasm.exe'
    if (Test-Path $exe) {
        return $exe
    }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    $zip = Join-Path $env:TEMP 'nasm-3.02-win64.zip'
    Write-Host "Downloading NASM from $($script:NasmDownload)"
    Invoke-WebRequest -Uri $script:NasmDownload -OutFile $zip
    $extract = Join-Path $env:TEMP 'nasm-3.02-extract'
    if (Test-Path $extract) {
        Remove-Item -Recurse -Force $extract
    }
    Expand-Archive -Path $zip -DestinationPath $extract -Force
    $found = Get-ChildItem -Path $extract -Recurse -Filter 'nasm.exe' | Select-Object -First 1
    if (-not $found) {
        throw 'NASM zip downloaded but nasm.exe was not inside it.'
    }
    Copy-Item -Force $found.FullName $exe
    Get-ChildItem $found.Directory.FullName | ForEach-Object {
        if ($_.Name -ne 'nasm.exe') {
            Copy-Item -Force $_.FullName (Join-Path $destDir $_.Name) -ErrorAction SilentlyContinue
        }
    }
    Write-ToolLine 'ok' 'NASM fetch' $exe
    return $exe
}

function Get-Edk2Clone {
    $dest = Join-Path $script:RepoUefi 'edk2'
    if (Test-Path (Join-Path $dest 'edksetup.bat')) {
        return (Resolve-Path $dest).Path
    }
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) {
        throw 'git.exe not found; cannot clone EDK2. Install Git or pass -Edk2Path.'
    }
    Write-Host "Cloning EDK2 $($script:Edk2Tag) into $dest"
    & git clone --depth 1 --branch $script:Edk2Tag $script:Edk2Repo $dest
    if ($LASTEXITCODE -ne 0) {
        throw "git clone EDK2 failed with exit $LASTEXITCODE"
    }
    Push-Location $dest
    try {
        & git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) {
            throw "git submodule update failed with exit $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
    Write-ToolLine 'ok' 'EDK2 clone' $dest
    return (Resolve-Path $dest).Path
}
