# SPDX-License-Identifier: GPL-2.0-only
# Copy a GPU Unlock loader and its firmware onto a USB volume.
# Pass -DriveLetter H, or omit it and pick a letter from the list.
# One USB device with several letters is listed once per letter.
# Format the stick as FAT32 in File Explorer first. This script never
# formats a disk. The internal EFI system partition is not modified.
# Turing can still be installed onto the ESP with Install-GpuUnlockBoot.ps1.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$DriveLetter,
    [ValidateSet('Turing', 'Ampere', 'Ada')]
    [string[]]$Arch = @('Turing', 'Ampere', 'Ada'),
    [ValidateSet('Turing', 'Ampere', 'Ada')]
    [string]$BootArch,
    [string]$LoaderDir,
    [string]$FirmwareRoot,
    [int]$TimeoutSec = 20,
    [ValidateSet('skip', 'inject')]
    [string]$DefaultAction = 'skip'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-Administrator

$ArchNames = @{ turing = 'Turing'; ampere = 'Ampere'; ada = 'Ada' }
$Arch = @($Arch | ForEach-Object { $ArchNames[$_.ToLowerInvariant()] } | Select-Object -Unique)
if ($BootArch) {
    $BootArch = $ArchNames[$BootArch.ToLowerInvariant()]
}
if (-not $BootArch) {
    if ($Arch.Count -eq 1) {
        $BootArch = $Arch[0]
    } else {
        throw 'Pass -BootArch Turing, Ampere, or Ada. That loader becomes EFI\BOOT\BOOTX64.EFI.'
    }
}
if ($Arch -notcontains $BootArch) {
    throw "-BootArch $BootArch is not in -Arch ($($Arch -join ', '))."
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $LoaderDir) {
    $LoaderDir = Join-Path $RepoRoot 'build'
}
if (-not $FirmwareRoot) {
    $FirmwareRoot = Join-Path $RepoRoot 'assets\firmware'
}

$Required = @{
    Turing = @(
        'booter_load_tu102.bin',
        'v67-tu102-610.43.03.bin',
        'gsp_tu10x.bin',
        'gsp_boot_tu102.bin',
        'gsp_boot_desc_tu102.bin',
        'sec2_bl_tu102.bin'
    )
    Ampere = @(
        'booter_load_ga102.bin',
        'v67-ga102-610.43.03.bin',
        'gsp_ga10x.bin',
        'gsp_boot_ga102.bin',
        'gsp_boot_desc_ga102.bin'
    )
    Ada = @(
        'booter_load_ad102.bin',
        'v67-ad102-610.43.03-four-plm.bin',
        'gsp_ga10x.bin',
        'gsp_boot_ad102.bin',
        'gsp_boot_desc_ad102.bin'
    )
}

$Loaders = @{}
$FirmwareDirs = @{}
foreach ($name in $Arch) {
    $loader = Join-Path $LoaderDir "UnlockLoader-$name.efi"
    if (-not (Test-PeEfiApplication $loader)) {
        throw "Not an x64 EFI application: $loader. Build it with tools\Build-GpuUnlockEfi.ps1 -Arch $name."
    }
    if (-not (Test-WrapperFile $loader)) {
        throw "Loader is missing magic $($script:TuMagic): $loader"
    }
    $fwDir = Join-Path $FirmwareRoot $name
    foreach ($file in $Required[$name]) {
        $path = Join-Path $fwDir $file
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Firmware file missing: $path. Run tools\Build-GpuUnlockFirmware.ps1 -Arch $name."
        }
    }
    $Loaders[$name] = (Resolve-Path -LiteralPath $loader).Path
    $FirmwareDirs[$name] = (Resolve-Path -LiteralPath $fwDir).Path
}

if (($Arch -contains 'Ampere') -and ($Arch -contains 'Ada')) {
    $amp = Get-FileSha256 (Join-Path $FirmwareDirs['Ampere'] 'gsp_ga10x.bin')
    $ada = Get-FileSha256 (Join-Path $FirmwareDirs['Ada'] 'gsp_ga10x.bin')
    if ($amp -ne $ada) {
        throw "Ampere and Ada gsp_ga10x.bin hashes differ ($amp vs $ada). Refusing to copy them onto one stick."
    }
}

function Get-DirBytes([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return [int64]0 }
    $sum = (Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum).Sum
    if ($null -eq $sum) { return [int64]0 }
    return [int64]$sum
}

function Get-PayloadBytes {
    $dest = @{}
    foreach ($name in $Arch) {
        $dest["UnlockLoader-$name.efi"] = [int64](Get-Item -LiteralPath $Loaders[$name]).Length
        foreach ($file in $Required[$name]) {
            $dest[$file] = [int64](Get-Item -LiteralPath (Join-Path $FirmwareDirs[$name] $file)).Length
        }
    }
    $dest['BOOTX64.EFI'] = [int64](Get-Item -LiteralPath $Loaders[$BootArch]).Length
    $need = [int64]8192
    foreach ($value in $dest.Values) { $need += $value }
    return $need
}

function ConvertTo-DriveLetter([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $t = $Text.Trim().TrimEnd('\').TrimEnd(':')
    if ($t.Length -ne 1 -or $t -notmatch '^[A-Za-z]$') {
        throw "DriveLetter must be one letter, for example -DriveLetter H."
    }
    return $t.ToUpperInvariant()
}

function Get-UsbRows {
    # A generic List[object] cannot be returned from a function on Windows
    # PowerShell 5.1: "@($list)" throws "Argument types do not match".
    $rows = @()
    $disks = @(Get-Disk | Where-Object { $_.BusType -eq 'USB' -and -not $_.IsBoot -and -not $_.IsSystem })
    foreach ($disk in $disks) {
        $parts = @(Get-Partition -DiskNumber $disk.Number -ErrorAction SilentlyContinue | Where-Object {
            $letter = [string]$_.DriveLetter
            -not $_.IsBoot -and -not $_.IsSystem -and $letter -ne 'C'
        })
        if ($parts.Count -eq 0) {
            $rows += [pscustomobject]@{
                Disk = $disk; Partition = $null; Letter = ''; FileSystem = ''; Label = ''; Size = [int64]$disk.Size; Free = [int64]0
            }
            continue
        }
        foreach ($part in $parts) {
            $vol = $null
            try { $vol = Get-Volume -Partition $part } catch { }
            $letter = ''
            $asText = [string]$part.DriveLetter
            if ($asText -and $asText -ne "`0") { $letter = $asText }
            $fs = ''
            $label = ''
            $size = [int64]$part.Size
            $free = [int64]0
            if ($vol) {
                $fs = [string]$vol.FileSystem
                $label = [string]$vol.FileSystemLabel
                if ($vol.Size) { $size = [int64]$vol.Size }
                if ($vol.SizeRemaining) { $free = [int64]$vol.SizeRemaining }
            }
            $rows += [pscustomobject]@{
                Disk = $disk; Partition = $part; Letter = $letter; FileSystem = $fs; Label = $label; Size = $size; Free = $free
            }
        }
    }
    return @($rows)
}

function Get-DiskVolumeSummary($Rows, [uint32]$Number) {
    $bits = @()
    foreach ($row in @($Rows | Where-Object { $_.Disk.Number -eq $Number })) {
        if (-not $row.Partition) {
            $bits += 'no partitions'
            continue
        }
        $letter = if ($row.Letter) { "$($row.Letter):" } else { "part $($row.Partition.PartitionNumber) (no letter)" }
        $fs = if ($row.FileSystem) { $row.FileSystem } else { 'unformatted' }
        $label = if ($row.Label) { $row.Label } else { '' }
        $bits += ("{0} {1} {2}" -f $letter, $fs, $label).Trim()
    }
    if ($bits.Count -eq 0) { return 'no volumes' }
    return ($bits -join '; ')
}

function Read-MenuChoice([int]$Count) {
    while ($true) {
        $prompt = if ($Count -eq 1) { 'Press Enter to use 1, or type 1' } else { "Type a number from 1 to $Count and press Enter" }
        $answer = Read-Host $prompt
        if ([string]::IsNullOrWhiteSpace($answer) -and $Count -eq 1) { return 1 }
        $n = 0
        if ([int]::TryParse(([string]$answer).Trim(), [ref]$n) -and $n -ge 1 -and $n -le $Count) { return $n }
        Write-Host "Type a number from 1 to $Count."
    }
}

function Set-PartitionLetter($Partition) {
    if ($Partition.DriveLetter) {
        return $Partition
    }
    foreach ($letter in @('D','E','F','G','H','I','J','K','L','M','N','P','Q','R','W')) {
        if (-not (Get-PSDrive -Name $letter -ErrorAction SilentlyContinue)) {
            Set-Partition -DiskNumber $Partition.DiskNumber -PartitionNumber $Partition.PartitionNumber -NewDriveLetter $letter
            return Get-Partition -DiskNumber $Partition.DiskNumber -PartitionNumber $Partition.PartitionNumber
        }
    }
    throw "Disk $($Partition.DiskNumber) partition $($Partition.PartitionNumber) has no free drive letter."
}

function Write-UsbPayload([string]$Root) {
    $bootDir = Join-Path $Root 'EFI\BOOT'
    $payloadDir = Join-Path $Root 'EFI\turing-unlock'
    $fwDest = Join-Path $payloadDir 'firmware'
    New-Item -ItemType Directory -Force -Path $bootDir, $payloadDir | Out-Null
    if (Test-Path -LiteralPath $fwDest) {
        Remove-Item -LiteralPath $fwDest -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $fwDest | Out-Null
    Get-ChildItem -LiteralPath $payloadDir -Filter 'UnlockLoader-*.efi' -File -ErrorAction SilentlyContinue |
        Remove-Item -Force

    Copy-Item -LiteralPath $Loaders[$BootArch] -Destination (Join-Path $bootDir 'BOOTX64.EFI') -Force
    foreach ($name in $Arch) {
        Copy-Item -LiteralPath $Loaders[$name] -Destination (Join-Path $payloadDir "UnlockLoader-$name.efi") -Force
        foreach ($file in $Required[$name]) {
            Copy-Item -LiteralPath (Join-Path $FirmwareDirs[$name] $file) -Destination (Join-Path $fwDest $file) -Force
        }
        $license = Join-Path $FirmwareDirs[$name] 'NVIDIA-LICENSE'
        if (Test-Path -LiteralPath $license) {
            Copy-Item -LiteralPath $license -Destination (Join-Path $fwDest 'NVIDIA-LICENSE') -Force
        }
    }

    $config = @"
default=$DefaultAction
timeout_sec=$TimeoutSec
verbose=1
targets=all
"@
    [IO.File]::WriteAllText((Join-Path $payloadDir 'config.ini'), $config, [Text.UTF8Encoding]::new($false))
    # Presence of this file selects USB chainload. An ESP install is recognized
    # by bootmgfw_ms.efi instead, and this marker is not written there.
    [IO.File]::WriteAllText((Join-Path $payloadDir 'usb-boot'), "usb-boot`n", [Text.UTF8Encoding]::new($false))
}

$need = Get-PayloadBytes
$letterWanted = ConvertTo-DriveLetter $DriveLetter
$rows = @(Get-UsbRows)
if ($rows.Count -eq 0) {
    throw 'No USB disk found. Plug in the stick and run the script again, or pass -DriveLetter.'
}
Write-Host ("Architectures: {0}. Boot loader: {1}. Default action: {2}." -f ($Arch -join ', '), $BootArch, $DefaultAction)
Write-Host ("Payload about {0:N0} MB." -f ($need / 1MB))
Write-Host 'The internal EFI system partition is not modified.'

$fatHint = 'Format the USB volume as FAT32 in File Explorer (right-click the drive, Format, FAT32), then run this script again. This script does not format disks. Explorer omits FAT32 when the volume is larger than 32 GB; create a 32 GB or smaller volume yourself before formatting it.'
$fatRows = @($rows | Where-Object { $_.FileSystem -eq 'FAT32' -and $_.Partition })
$chosen = $null
if ($letterWanted) {
    $owner = @($rows | Where-Object { $_.Letter -eq $letterWanted })
    if ($owner.Count -eq 0) {
        throw "No USB volume is mounted as ${letterWanted}:."
    }
    $chosen = $owner[0]
    if ($chosen.FileSystem -ne 'FAT32') {
        $summary = Get-DiskVolumeSummary $rows $chosen.Disk.Number
        throw "${letterWanted}: is $($chosen.FileSystem), not FAT32. Volumes on this disk: $summary. $fatHint"
    }
} else {
    if ($fatRows.Count -eq 0) {
        Write-Host ''
        Write-Host 'No FAT32 volume on a USB disk.'
        foreach ($group in @($rows | Group-Object { $_.Disk.Number })) {
            $one = $group.Group[0].Disk
            Write-Host ("  {0}: {1}" -f $one.FriendlyName, (Get-DiskVolumeSummary $rows $one.Number))
        }
        throw $fatHint
    }
    Write-Host ''
    Write-Host 'USB volumes. Each letter is its own choice. Only FAT32 is listed.'
    Write-Host 'This script only copies files. Format the stick in File Explorer first.'
    Write-Host ''
    for ($i = 0; $i -lt $fatRows.Count; $i++) {
        $row = $fatRows[$i]
        $shown = if ($row.Letter) { "$($row.Letter):" } else { '(no letter)' }
        $label = if ($row.Label) { $row.Label } else { '-' }
        Write-Host ("  [{0}]  {1}  FAT32  {2}  {3:N1} GB  free {4:N0} MB" -f ($i + 1), $shown, $label, ($row.Size / 1GB), ($row.Free / 1MB))
        Write-Host ("       {0}" -f $row.Disk.FriendlyName)
        Write-Host ("       letters on this disk: {0}" -f (Get-DiskVolumeSummary $rows $row.Disk.Number))
    }
    $pick = Read-MenuChoice $fatRows.Count
    $chosen = $fatRows[$pick - 1]
}
$partition = Set-PartitionLetter $chosen.Partition
$root = "$($partition.DriveLetter):"
$volume = Get-Volume -DriveLetter $partition.DriveLetter
$replaced = Get-DirBytes (Join-Path $root 'EFI\turing-unlock\firmware')
$bootEfi = Join-Path $root 'EFI\BOOT\BOOTX64.EFI'
if (Test-Path -LiteralPath $bootEfi) {
    $replaced += [int64](Get-Item -LiteralPath $bootEfi).Length
}
$oldLoaders = Join-Path $root 'EFI\turing-unlock'
if (Test-Path -LiteralPath $oldLoaders) {
    $oldSum = (Get-ChildItem -LiteralPath $oldLoaders -Filter 'UnlockLoader-*.efi' -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum).Sum
    if ($null -ne $oldSum) { $replaced += [int64]$oldSum }
}
$free = [int64]$volume.SizeRemaining + $replaced
Write-Host ("Copy onto {0}  FAT32 label={1} free {2:N0} MB, payload {3:N0} MB." -f $root, $volume.FileSystemLabel, ($free / 1MB), ($need / 1MB))
Write-Host ("Other letters on this disk stay: {0}" -f (Get-DiskVolumeSummary $rows $chosen.Disk.Number))
if ($free -lt $need) {
    throw "$root has $([math]::Round($free / 1MB)) MB available and the payload needs $([math]::Round($need / 1MB)) MB."
}
if (-not $PSCmdlet.ShouldProcess($root, 'copy the GPU Unlock loader onto the existing FAT32 volume')) {
    return
}

Write-UsbPayload $root
Write-Host "USB ready at $root"
Write-Host "  EFI\BOOT\BOOTX64.EFI = UnlockLoader-$BootArch.efi"
Write-Host "  EFI\turing-unlock\usb-boot"
Write-Host "  EFI\turing-unlock\firmware\ = $($Arch -join ', ') blobs"
Write-Host 'Boot this disk from the firmware boot menu. Secure Boot must be off.'
Write-Host 'After inject, skip, or emergency the loader lists EFI loaders on NVMe and SATA.'
Write-Host 'Turing can still be installed on the internal ESP with tools\Install-GpuUnlockBoot.ps1.'
