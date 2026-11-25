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
$script:BootOrderTaskName = 'GpuUnlock Boot Order Guard'
$script:FirmwareEntryDescription = 'GPU Unlock'
$script:ProgramData = Join-Path $env:ProgramData 'GpuUnlock'
$script:InstallModes = @('Replace', 'FirmwareEntry')

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

function Invoke-BcdEdit {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )
    $out = & bcdedit.exe @Arguments 2>&1
    $text = ($out | ForEach-Object { "$_" }) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "bcdedit $($Arguments -join ' ') failed ($LASTEXITCODE): $text"
    }
    return $text
}

function Get-GpuUnlockInstallMode([object]$State) {
    if ($State -and $State.installMode) {
        $mode = [string]$State.installMode
        if ($script:InstallModes -contains $mode) {
            return $mode
        }
    }
    if ($State -and $State.firmwareEntryId) {
        return 'FirmwareEntry'
    }
    return 'Replace'
}

function Unregister-GpuUnlockProtectTasks {
    foreach ($name in @($script:TaskName, $script:LegacyTaskName, $script:BootOrderTaskName)) {
        Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
    }
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
    Unregister-ScheduledTask -TaskName $script:BootOrderTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $script:TaskName -Action $action -Trigger $startup -Principal $principal -Settings $settings -Force | Out-Null
    Write-Host "Scheduled task '$script:TaskName' registered (SYSTEM, 1 min after startup)."
    try {
        Start-ScheduledTask -TaskName $script:TaskName
        Write-Host "Started '$script:TaskName' once now."
    } catch {
        Write-Warning "Task registered but could not start immediately: $_"
    }
}

function Register-GpuUnlockBootOrderTask {
    $protect = Join-Path $PSScriptRoot 'Protect-GpuUnlockBoot.ps1'
    if (-not (Test-Path -LiteralPath $protect)) {
        throw "Protect script missing: $protect"
    }
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$protect`""
    $startup = New-ScheduledTaskTrigger -AtStartup
    $startup.Delay = 'PT1M'
    $hourly = New-ScheduledTaskTrigger -Once -At ((Get-Date).AddMinutes(5)) -RepetitionInterval (New-TimeSpan -Hours 1) -RepetitionDuration (New-TimeSpan -Days 3650)
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 10)
    Unregister-ScheduledTask -TaskName $script:TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $script:LegacyTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $script:BootOrderTaskName -Action $action -Trigger @($startup, $hourly) -Principal $principal -Settings $settings -Force | Out-Null
    Write-Host "Scheduled task '$script:BootOrderTaskName' registered (SYSTEM, startup + hourly)."
    try {
        Start-ScheduledTask -TaskName $script:BootOrderTaskName
        Write-Host "Started '$script:BootOrderTaskName' once now."
    } catch {
        Write-Warning "Task registered but could not start immediately: $_"
    }
}

function Find-GpuUnlockFirmwareEntryId {
    param([string]$PreferredId)
    if ($PreferredId) {
        try {
            $probe = Invoke-BcdEdit -Arguments @('/enum', $PreferredId)
            if ($probe -match 'TuringUnlockLoader\.efi' -or
                $probe -match [regex]::Escape('\' + ($script:EspLoaderRel -replace '/', '\'))) {
                return $PreferredId
            }
        } catch {
        }
    }

    $enum = Invoke-BcdEdit -Arguments @('/enum', 'firmware')
    $blocks = [regex]::Split($enum, '(?=^Identifier\s+)', [System.Text.RegularExpressions.RegexOptions]::Multiline)
    foreach ($block in $blocks) {
        if ($block -notmatch 'TuringUnlockLoader\.efi') {
            continue
        }
        if ($block -match 'Identifier\s+(\{[0-9a-fA-F-]+\})') {
            return $Matches[1]
        }
    }
    return $null
}

function Get-FwBootmgrDisplayOrder {
    $enum = Invoke-BcdEdit -Arguments @('/enum', '{fwbootmgr}')
    foreach ($line in ($enum -split "`n")) {
        if ($line -match '^\s*displayorder\s+(.+)$') {
            $raw = $Matches[1].Trim()
            return @([regex]::Matches($raw, '\{[0-9a-fA-F-]+\}') | ForEach-Object { $_.Value })
        }
    }
    return @()
}

function Set-GpuUnlockFirmwareEntryFirst {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EntryId
    )
    [void](Invoke-BcdEdit -Arguments @('/set', '{fwbootmgr}', 'displayorder', $EntryId, '/addfirst'))
}

function Install-GpuUnlockFirmwareEntry {
    param(
        [string]$ExistingId
    )
    $path = '\' + ($script:EspLoaderRel -replace '/', '\')
    $id = Find-GpuUnlockFirmwareEntryId -PreferredId $ExistingId
    if (-not $id) {
        $copyOut = Invoke-BcdEdit -Arguments @('/copy', '{bootmgr}', '/d', $script:FirmwareEntryDescription)
        if ($copyOut -notmatch '(\{[0-9a-fA-F-]+\})') {
            throw "bcdedit /copy did not return a GUID: $copyOut"
        }
        $id = $Matches[1]
        Write-Host "Created firmware boot entry $id ($script:FirmwareEntryDescription)."
    } else {
        Write-Host "Reusing firmware boot entry $id."
    }

    [void](Invoke-BcdEdit -Arguments @('/set', $id, 'path', $path))
    Set-GpuUnlockFirmwareEntryFirst -EntryId $id
    try {
        [void](Invoke-BcdEdit -Arguments @('/set', '{fwbootmgr}', 'bootsequence', $id))
    } catch {
        Write-Warning "Could not set one-shot bootsequence: $_"
    }
    return $id
}

function Remove-GpuUnlockFirmwareEntry {
    param([string]$EntryId)
    $id = Find-GpuUnlockFirmwareEntryId -PreferredId $EntryId
    if (-not $id) {
        Write-Host 'No GPU Unlock firmware boot entry found.'
        return
    }
    try {
        [void](Invoke-BcdEdit -Arguments @('/delete', $id, '/f'))
        Write-Host "Deleted firmware boot entry $id."
    } catch {
        Write-Warning "Could not delete firmware boot entry ${id}: $_"
    }
}

function Restore-OfficialBootmgfwIfWrapper {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EspRoot
    )
    $MsBoot = Join-Path $EspRoot $script:MsBootRel
    $backup = Join-Path $EspRoot $script:MsBackupRel
    $backupAlt = Join-Path $EspRoot $script:MsBackupAltRel
    if (-not (Test-Path -LiteralPath $MsBoot)) {
        throw "Windows Boot Manager not found: $MsBoot"
    }
    if (-not (Test-WrapperFile $MsBoot)) {
        return $false
    }
    $source = $null
    foreach ($candidate in @($backup, $backupAlt)) {
        if ((Test-Path -LiteralPath $candidate) -and (Test-PeEfiApplication $candidate) -and -not (Test-WrapperFile $candidate)) {
            $source = $candidate
            break
        }
    }
    if (-not $source) {
        throw "bootmgfw.efi is the unlock wrapper, but no Microsoft backup was found under $EspRoot. Restore Windows boot files before FirmwareEntry install."
    }
    Copy-Item -LiteralPath $source -Destination $MsBoot -Force
    Write-Host "Restored official bootmgfw.efi from $source for FirmwareEntry mode."
    return $true
}

function Ensure-BootmgfwBackup {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EspRoot,
        [Parameter(Mandatory = $true)]
        [string]$BackupStampDir
    )
    $MsBoot = Join-Path $EspRoot $script:MsBootRel
    $backup = Join-Path $EspRoot $script:MsBackupRel
    $backupAlt = Join-Path $EspRoot $script:MsBackupAltRel
    if (Test-WrapperFile $MsBoot) {
        throw 'Refusing to back up bootmgfw.efi while it is still the unlock wrapper. Restore the Microsoft file first.'
    }
    if (-not (Test-PeEfiApplication $MsBoot)) {
        throw "Refusing to back up ${MsBoot}: it is not a PE EFI image."
    }
    New-Item -ItemType Directory -Force -Path $BackupStampDir | Out-Null
    Copy-Item -LiteralPath $MsBoot -Destination (Join-Path $BackupStampDir 'bootmgfw.efi') -Force
    Copy-Item -LiteralPath $MsBoot -Destination $backup -Force
    Copy-Item -LiteralPath $MsBoot -Destination $backupAlt -Force
}

function Write-FirmwareEntryInstallWarnings {
    Write-Host ''
    Write-Warning @'
FirmwareEntry does NOT stop Windows Update / Secure-Boot-Update from rewriting \EFI\Microsoft\Boot\bootmgfw.efi.
That rewrite no longer replaces the unlock loader (it lives under EFI\turing-unlock\), but a new Microsoft bootmgr build can still change PCR4 / BitLocker, and BIOS or feature updates can still reorder BootOrder.
'@
    Write-Warning 'Suspend BitLocker for one reboot before the first trial after this layout change (or after a boot-manager update): Suspend-BitLocker -MountPoint C: -RebootCount 1'
    if (Get-SecureBootEnabled) {
        Write-Warning 'Secure Boot is ON. The release EFI is unsigned. Enroll a self-signed cert into firmware db and sign TuringUnlockLoader.efi, or firmware will reject it. See README self-signing notes.'
    }
}
