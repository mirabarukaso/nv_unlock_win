# SPDX-License-Identifier: GPL-2.0-only
$script:TuMagic = 'TURING_UNLOCK_LOADER_v1'
$script:EspGptType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$script:StateRel = 'EFI\turing-unlock\install-state.json'
$script:DirRel = 'EFI\turing-unlock'
$script:MsBootRel = 'EFI\Microsoft\Boot\bootmgfw.efi'
$script:MsBackupRel = 'EFI\turing-unlock\bootmgfw_ms.efi'
$script:MsBackupAltRel = 'EFI\Microsoft\Boot\bootmgfw_ms.efi'
$script:FallbackRel = 'EFI\Boot\bootx64.efi'
$script:EspLoaderRel = 'EFI\turing-unlock\TuringUnlockLoader.efi'
$script:TaskName = 'GpuUnlock Boot Guard'
$script:LegacyTaskName = 'TuringUnlock Boot Guard'
$script:ProgramData = Join-Path $env:ProgramData 'GpuUnlock'

function Test-Administrator {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-Administrator {
    if (-not (Test-Administrator)) {
        throw 'This script must run from an elevated PowerShell (Run as administrator).'
    }
}

function Get-FileSha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-PeEfiApplication([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $fs = [IO.File]::Open($Path, 'Open', 'Read', 'Read')
    try {
        $buf = New-Object byte[] 0x200
        $n = $fs.Read($buf, 0, $buf.Length)
        if ($n -lt 0x40) { return $false }
        if ($buf[0] -ne 0x4D -or $buf[1] -ne 0x5A) { return $false }
        $pe = [BitConverter]::ToInt32($buf, 0x3C)
        if ($pe -lt 0 -or $pe + 0x5C -gt $fs.Length) { return $false }
        $fs.Position = $pe
        $peHdr = New-Object byte[] 0x70
        if ($fs.Read($peHdr, 0, $peHdr.Length) -lt 0x5C) { return $false }
        if ($peHdr[0] -ne 0x50 -or $peHdr[1] -ne 0x45) { return $false }
        $machine = [BitConverter]::ToUInt16($peHdr, 4)
        if ($machine -ne 0x8664) { return $false }
        $optMagic = [BitConverter]::ToUInt16($peHdr, 24)
        if ($optMagic -ne 0x20B) { return $false }
        $subsystem = [BitConverter]::ToUInt16($peHdr, 24 + 68)
        return ($subsystem -eq 10 -or $subsystem -eq 11)
    } finally {
        $fs.Close()
    }
}

function Test-WrapperFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $item = Get-Item -LiteralPath $Path
    # Host UnlockLoader is ~75 KiB; Microsoft bootmgfw is typically >1 MiB.
    if ($item.Length -lt 4096 -or $item.Length -gt 256KB) { return $false }
    $bytes = [IO.File]::ReadAllBytes($Path)
    $needle = [Text.Encoding]::ASCII.GetBytes($script:TuMagic)
    $limit = $bytes.Length - $needle.Length
    for ($i = 0; $i -le $limit; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) { $ok = $false; break }
        }
        if ($ok) { return $true }
    }
    return $false
}

function Get-EspRoot {
    $existing = Get-PSDrive -PSProvider FileSystem | Where-Object {
        Test-Path (Join-Path $_.Root $script:MsBootRel)
    } | Select-Object -First 1
    if ($existing) {
        return $existing.Root.TrimEnd('\')
    }

    $part = Get-Partition | Where-Object { $_.GptType -eq $script:EspGptType } | Select-Object -First 1
    if (-not $part) {
        throw 'EFI system partition was not found.'
    }
    if ($part.DriveLetter) {
        return "$($part.DriveLetter):"
    }

    foreach ($letter in @('S', 'T', 'U', 'V', 'Y', 'Z')) {
        if (-not (Test-Path "${letter}:\")) {
            try {
                Set-Partition -InputObject $part -NewDriveLetter $letter
                return "${letter}:"
            } catch {
            }
        }
    }

    $vol = "\\?\$((Get-Volume -Partition $part).UniqueId.TrimEnd('\'))"
    if (Test-Path (Join-Path $vol $script:MsBootRel)) {
        return $vol
    }
    throw 'Could not assign a drive letter to the EFI system partition.'
}

function Get-InstallState([string]$EspRoot) {
    $path = Join-Path $EspRoot $script:StateRel
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Save-InstallState([string]$EspRoot, $State) {
    $dir = Join-Path $EspRoot $script:DirRel
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $json = $State | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText((Join-Path $EspRoot $script:StateRel), $json, [Text.UTF8Encoding]::new($false))
}

function Get-SecureBootEnabled {
    try {
        return [bool](Confirm-SecureBootUEFI)
    } catch {
        return $false
    }
}

function Write-ProtectLog([string]$Message) {
    New-Item -ItemType Directory -Force -Path $script:ProgramData | Out-Null
    $line = '{0:o} {1}' -f (Get-Date).ToUniversalTime(), $Message
    Add-Content -LiteralPath (Join-Path $script:ProgramData 'protect.log') -Value $line
    Write-Host $line
}

function Wait-EspRoot {
    param([int]$Tries = 18, [int]$DelaySec = 5)
    $last = $null
    for ($i = 0; $i -lt $Tries; $i++) {
        try {
            return Get-EspRoot
        } catch {
            $last = $_
            Write-ProtectLog "ESP not ready ($($i + 1)/$Tries): $_"
            Start-Sleep -Seconds $DelaySec
        }
    }
    throw "EFI system partition was not found after $Tries tries. $last"
}

function Register-GpuUnlockProtectTask {
    $protect = Join-Path $PSScriptRoot 'Protect-GpuUnlockBoot.ps1'
    if (-not (Test-Path -LiteralPath $protect)) {
        throw "Protect script missing: $protect"
    }
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$protect`""
    $startup = New-ScheduledTaskTrigger -AtStartup
    $startup.Delay = 'PT1M'
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 10)
    Unregister-ScheduledTask -TaskName $script:LegacyTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $script:TaskName -Action $action -Trigger $startup -Principal $principal -Settings $settings -Force | Out-Null
    Write-Host "Scheduled task '$script:TaskName' registered (SYSTEM, 1 min after startup)."
    try {
        Start-ScheduledTask -TaskName $script:TaskName
        Write-Host "Started '$script:TaskName' once now."
    } catch {
        Write-Warning "Task registered but could not start immediately: $_"
    }
}
