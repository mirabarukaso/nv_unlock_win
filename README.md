> [!IMPORTANT]
> Secure Boot was off for both runs.
>
> - **TITAN RTX** (TU102, `10de:1e02`), Windows 10, driver 617.42. Loader printed `TU102 FEAT UNLOCKED` with `SS0=88888888` `SS1=8`. Windows started. FP16 GEMM at 8192 stayed 99.3 → 99.1 TFLOPS. This card is already factory-full.
> - **RTX 3070 Ti** (GA104, `10de:2482`), Windows 10, driver 617.42. Loader printed `GA102 FEAT UNLOCKED` with `SS0=88888888` `SS1=8`. Windows started. The display stays black for about a minute during boot. Wait. FP16→FP32 GEMM at 8192 rose from 48.5 to 86.3 TFLOPS (+78%).
>

[中文README](README_zh.md)

# GPU Unlock UEFI loader (Windows)

Unsigned EFI wrapper that replaces `\EFI\Microsoft\Boot\bootmgfw.efi`, optionally runs a Turing / Ampere / Ada unlock, then chain-loads the original Windows Boot Manager.

Linux module sources this tree ports:

| Arch | Upstream |
| --- | --- |
| Turing | https://github.com/nlzy/turing-unlock |
| Ampere | https://github.com/Zagitta/gpu-unlock |
| Ada | https://github.com/JustAResearcher/gpu-unlock-ada |

Version: `0.2.6` (menu title `GPU Unlock Loader 0.2.6 (<Arch>)`). License: GPL-2.0-only, see [LICENSE](LICENSE) and [NOTICE](NOTICE).

`0.2.4` answers the build notes in [#1](https://github.com/mirabarukaso/nv_unlock_win/issues/1): the EDK2 pin is `edk2-stable202608`, `assets\config.ini` is in the tree, and the loader writes a `handoff` line to `unlock.log` before Windows. A 2080 Ti retest is still required. This TITAN RTX cannot show whether the SS write takes effect, or whether the driver accepts that state only because the device ID is `10de:1e02`.

`0.2.5` adds a USB boot path that leaves the internal ESP alone, and corrects the Ampere SEC2 completion check. `tools\New-GpuUnlockUsb.ps1` formats a USB disk and copies one or more arch loaders plus their firmware. Before chainload the loader logs `boot mode=esp` when that volume holds a signed `bootmgfw` backup (the existing `Install-GpuUnlockBoot.ps1` layout), `boot mode=usb` when `\EFI\turing-unlock\usb-boot` is present, and `boot mode=unmarked` otherwise. USB mode connects mass-storage controllers and loads Windows Boot Manager from another FAT volume, using that volume's device path. Turing can still be installed on the ESP.

`0.2.6` keeps that ESP path: a signed `bootmgfw` backup on the loader volume still restores that file and starts Windows from that volume only. USB mode (the `usb-boot` marker, and no signed backup) connects NVMe and SATA controllers, then lists EFI loaders on other FAT volumes and waits for a number. The list can contain Windows Boot Manager, a distro `shimx64.efi` or `grubx64.efi`, and `systemd-bootx64.efi`. `\EFI\BOOT\bootx64.efi` is included only when that volume has none of those. Inject, skip, and emergency all reach this list; inject still prints the feature registers and waits first. The selected file is started with that volume's own device path. The scan does not write `bootmgfw.efi`. `New-GpuUnlockUsb.ps1` copies onto a FAT32 volume and does not format any disk. Format the stick in File Explorer first. Pass `-DriveLetter H`, or omit it and pick a letter from the list the script prints. One stick with several letters is one choice per letter.

Ampere now follows the Linux GA10x transaction: SEC2 success is feature PLM opening to `0xffffffff`, not a later halt. On loader 0.2.4 a GA104 RTX 3070 Ti (`10de:2482`) opened PLM with CPUCTL still `0`. The extra 5-second halt poll returned `-110`, SS0/SS1 stayed at stock, and the error-path FLR let Windows start with an unchanged GEMM. Ada still waits for halt, then checks the four AD102 permission words. WPR_CFG `0x1fa7cc` must be `0xfffff0ff`, and WPR `0x1fa7c4` plus feature PLM must be `0xffffffff`, before the selectors are written. FBPA `0x9a0148` is logged only. The same four words are sampled before firmware and again after FLR. The V67 DMA copy is compared with the file immediately before SEC2 starts. A failed GPU is logged as `unlock 10de:<id> result=<errno>`, and `inject finished status=` is no longer `0` when that happens. Chainload still continues. The 3070 Ti retest below used that transaction. Its log banner still says `version=0.2.4` (the EFI was built before the 0.2.5 version string); the payload line is the fixed one, with no halt timeout.

---

## Contents

1. [What it does](#what-it-does)
2. [Prerequisites](#prerequisites)
3. [Download the toolchain](#download-the-toolchain)
4. [Prepare](#prepare)
5. [Build](#build)
6. [Firmware blobs](#firmware-blobs)
7. [Clear](#clear)
8. [Hyper-V guest](#hyper-v-guest)
9. [Install](#install)
10. [Boot menu](#boot-menu)
11. [Loader log](#loader-log)
12. [Uninstall and recovery](#uninstall-and-recovery)
13. [Rebuild the ESP from Windows setup / USB](#rebuild-the-esp-from-windows-setup--usb)
14. [Benchmark](#benchmark)
15. [Inject on real hardware](#inject-on-real-hardware)
16. [Troubleshooting](#troubleshooting)
17. [License](#license)
18. [Layout](#layout)

---

## What it does

Firmware still boots `\EFI\Microsoft\Boot\bootmgfw.efi`. BIOS BootOrder does not change.

1. The install script backs up the original `bootmgfw.efi` as `bootmgfw_ms.efi`, then writes this loader to the official path.
2. The boot menu defaults to **SKIP inject** (20 second timeout). During the countdown it **reads** PLM/SS0/SS1 (no writes) and prints them. `I` injects (including readout-only IDs of the **built** arch, such as TITAN RTX). After inject it re-reads the feat regs, waits 20 seconds or any key, then boots. `R` chain-loads Windows immediately. Before chainload, skip and inject both record one `handoff` line and write `\EFI\turing-unlock\unlock.log` in a single file update. That line is the register state Windows inherits: PLM/SS0/SS1/readout, WPR2, boot, GSP/SEC CPUCTL, PCI command, and BAR0.
3. Inject writes MMIO only when a NVIDIA device of the **built** arch is present **and** the firmware blobs exist on the volume that started the loader (the ESP, or the USB stick).
4. After skip or inject, the loader copies the **signed original** back onto `\EFI\Microsoft\Boot\bootmgfw.efi` (the BCD / Code Integrity path) and `LoadImage`s that file. Chain-loading a renamed `bootmgfw_ms.efi` while the official name is still the unsigned wrapper yields `0xc0000428`.
5. Scheduled task `GpuUnlock Boot Guard` (SYSTEM) runs **once, 1 minute after startup**. The loader always puts the signed Microsoft `bootmgfw.efi` back before Windows starts, so **this** boot can go straight into Windows with no menu. The task pastes the wrapper back once Windows is up. Windows Update or another rollback during a session costs at most **one** reboot without the menu; the following boot shows it again. Hourly repetition is unnecessary for that. The task's action is `-File` on `tools\Protect-GpuUnlockBoot.ps1` **inside this repo**. Keep that tree on disk after install; deleting it leaves the task pointing at a missing script.

UEFI GOP fonts usually cannot draw CJK, so the menu is English.

**Secure Boot must be off.** The wrapper is unsigned; firmware will not load it with SB on. Turning SB off only lets firmware load the wrapper. Windows still checks the Authenticode hash of `bootmgfw.efi`, so that official filename must be the Microsoft binary before StartImage.

---

## Prerequisites

Build on a **Windows machine with Visual Studio** (it does not have to be the target VM):

| Item | Requirement |
| --- | --- |
| OS | Windows 10/11 x64 |
| Visual Studio 2022 | workload **Desktop development with C++**, MSVC v143 x64 |
| Python | 3.6+ (`py -3` or `python` on PATH). Firmware generate also needs `pip install zstandard` |
| Git | to clone EDK2 |
| Disk | a full EDK2 tree is several GB |
| Target VM | Hyper-V **Generation 2**, Windows 10/11 x64, administrator account |

VS Build Tools is enough if `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` is installed. `vswhere` must find it.

Download NASM and EDK2 as below. They are not part of a stock Windows install.

---

## Download the toolchain

Commands assume the **root of this tree** (the directory that contains `GpuUnlockPkg` and `tools`). From the parent repo, `cd uefi` first.

### 1. EDK2

The build script looks for `edksetup.bat` in:

- `.\edk2`
- `C:\edk2`
- `C:\src\edk2`
- `%USERPROFILE%\edk2`
- `%USERPROFILE%\src\edk2`
- environment variable `EDK2_PATH` or `-Edk2Path`

Use **edk2-stable202608** or later. The DSC links `CompilerIntrinsicsLib`, and submodules are required.

- `edk2-stable202408` does not contain that library. Its `subhook` submodule still points at `github.com/Zeex/subhook`, which is gone.
- `edk2-stable202411` has the library and `tianocore/edk2-subhook`, but `CompilerIntrinsicsLib/*_ms.c` typedefs `size_t` as 32-bit on x64 and fails with `C2371`.
- `edk2-stable202511` and later typedef `size_t` as `unsigned __int64`. That is the first stable tag that builds this package with VS x64.

`BaseTools\Bin\Win32\GenFw.exe` is not created by the clone. Build BaseTools once before this package: from an **x86 Native Tools** prompt, `edksetup.bat Rebuild`. A missing `GenFw.exe` means that step has not been run.

```powershell
git clone --recurse-submodules https://github.com/tianocore/edk2.git edk2
cd edk2
git checkout edk2-stable202608
git submodule update --init --recursive
cd ..
```

Shallow clone if the network is slow:

```powershell
git clone --depth 1 --branch edk2-stable202608 https://github.com/tianocore/edk2.git edk2
cd edk2
git submodule update --init --recursive
cd ..
```

An existing clone on another drive works with `-Edk2Path D:\src\edk2`.

### 2. NASM

EDK2 assembles `.nasm` files in MdePkg even for this application. Missing NASM is a hard failure. Download the **win64** build:

- Site: https://www.nasm.us/
- 3.02 zip: https://www.nasm.us/pub/nasm/releasebuilds/3.02/win64/nasm-3.02-win64.zip

Extract so `nasm.exe` sits here:

```
nasm-3.02\nasm.exe
```

`nasm.exe` on `PATH` also works, as does `NASM_PREFIX` / `-NasmPrefix` (directory path; the script adds a trailing backslash). 2.15.05 or later is enough; this tree expects 3.02.

### 3. Check the tools

```powershell
git --version
py -3 --version
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
```

After extracting NASM:

```powershell
.\nasm-3.02\nasm.exe -v
```

---

## Prepare

`tools\Prepare-GpuUnlock.ps1` is the workspace gate. It checks host tools **and** the source tree, then copies/links this package into EDK2 and stages optional firmware.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Prepare-GpuUnlock.ps1
powershell -ExecutionPolicy Bypass -File .\tools\Prepare-GpuUnlock.ps1 -Arch Turing
```

`-Arch` is `Turing` (default), `Ampere`, or `Ada`. Prepare and build compile **one** GPU architecture. Common loader files are always required; only the selected `Arch\<name>\` tree is treated as the inject backend.

What it verifies:

- Windows x64, VS2022 C++ tools (17.x), Python 3.6+, NASM 2.15.05+, git, EDK2
- EDK2 contents: `edksetup.bat`, `MdePkg`, `MdeModulePkg`, `MdeLibs.dsc.inc`, `CompilerIntrinsicsLib`, BaseTools
- Common directories: `GpuUnlockPkg\Application\Loader`, `Arch\Turing`, `Arch\Ampere`, `Arch\Ada`, `Include`, `tools`, `assets`, `bench`
- Selected arch sources (`Unlock.c` + `Portable\` for Turing, Ampere, and Ada). Other arches are listed as present and skipped
- Selected INF (`Loader.inf` / `LoaderAmpere.inf` / `LoaderAda.inf`) `[Sources]`, shared headers, tools scripts, `VERSION` / `LICENSE` / `NOTICE`
- Version strings match (`VERSION`, `TU_LOADER_VERSION`, `Loader.inf`, `GpuUnlockPkg.dec`)
- `BASE_NAME` stays `TuringUnlockLoader` (ESP copy) and magic stays `TURING_UNLOCK_LOADER_v1`. Host copies are `UnlockLoader-<Arch>.efi`
- If `-Arch Turing` and this tree sits next to the Linux module, each `Arch\Turing\Portable\*.c` has a matching kernel source

What it prepares:

- Junction `edk2\GpuUnlockPkg` → this tree's `GpuUnlockPkg` (falls back to a refreshed copy if `mklink /J` fails)
- Removes a leftover `edk2\TuringUnlockPkg` junction from older trees
- Copies EDK2 `Conf\*.txt` from BaseTools templates when missing
- Creates `.\build`
- If `assets\firmware\<Arch>\` already has that arch's blobs (from [Firmware blobs](#firmware-blobs)), reports them staged. This tree does not search sibling Linux repos.

If EDK2 or NASM is missing, fetch them (git / zip) instead of doing it by hand:

```powershell
.\tools\Prepare-GpuUnlock.ps1 -FetchEdk2
.\tools\Prepare-GpuUnlock.ps1 -FetchNasm
.\tools\Prepare-GpuUnlock.ps1 -FetchEdk2 -FetchNasm
```

Fix every `[fail]` line, then rerun Prepare until it prints `Prepare complete.`

## Build

Build always runs Prepare first (skip with `-SkipPrepare` only after a successful prepare in the same tree).

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1 -Arch Turing
```

The script:

1. Runs Prepare for that arch (tools + layout + EDK2 package link/copy + firmware stage)
2. Calls `edksetup.bat VS2022` then `build -a X64 -t VS2022 -b DEBUG -p GpuUnlockPkg\GpuUnlockPkg.dsc -D GPU_UNLOCK_ARCH=TURING`
3. Copies the EFI to `.\build\UnlockLoader-Turing.efi` (Ampere/Ada use the same `UnlockLoader-<Arch>.efi` name). Install still writes it to ESP as `EFI\turing-unlock\TuringUnlockLoader.efi`.

Ada uses the AD102-family Linux transaction (four-PLM V67, FLR on success). Ampere uses the GA102 Linux transaction (V67 + SS0/SS1, FLR on success). Each `-Arch` build compiles only that backend:

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ada
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ampere
```

Optional arguments:

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Turing -Edk2Path C:\edk2 -NasmPrefix .\nasm-3.02\ -Target RELEASE -ToolChain VS2022
```

Warnings about `iasl`, CLANG, and Cygwin are safe to ignore for this application. The old `Cannot find BaseTools Bin Win32` line was a false error from `toolsetup.bat` delayed expansion; the build script now sets `EDK_TOOLS_BIN` first.

A second build is incremental and takes a few seconds. The version string in the menu must match `TU_LOADER_VERSION` in `GpuUnlockPkg\Include\TuConfig.h` (currently `0.2.6`) and the arch you built.

EFI shell smoke test without replacing bootmgr:

```
fs0:
cd EFI\turing-unlock
TuringUnlockLoader.efi noload
```

`noload` stays in the loader after the menu. `inject` / `skip` skip the menu.

---

## Firmware blobs

NVIDIA firmware is **not** in git. Hyper-V skip-inject does not need it. Real-hardware inject reads `\EFI\turing-unlock\firmware\` on the volume that started the loader, the internal ESP or the USB stick. Missing files fail **before** MMIO (`firmware missing` / `load_fw error=-2`).

Each arch has its own pinned **610.43.03** set. Turing blobs do not unlock Ampere or Ada. TITAN RTX is TU102: generate **Turing** only.

### What the fetch step downloads

`tools\Build-GpuUnlockFirmware.ps1` runs `firmware\<arch>\fetch_artifacts.py`. URLs and SHA-256 are in `firmware\<arch>\artifacts-610.43.03.json` (Ada also has `ada-610.43.03.json`). The script only accepts a file whose hash matches the pin. Nothing downloaded is executed; our generators only extract and write blobs.

| Input | Source (pinned) | Used for |
| --- | --- | --- |
| `NVIDIA-kernel-module-source-610.43.03.tar.xz` | NVIDIA open kernel module source (`download.nvidia.com`) | Booter / GSP boot / SEC2 images decoded from generated C |
| `nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst` | CachyOS archive of that exact package | `gsp_tu10x.bin` or `gsp_ga10x.bin`, plus `NVIDIA-LICENSE` |

The V67 payload (`v67-*-610.43.03*.bin`) is generated locally from the upstream script. It is not an NVIDIA redistributable.

Cache (gitignored): `uefi\.cache\firmware\`. Output (gitignored): `uefi\assets\firmware\<Arch>\`. Do not commit `*.bin` or `NVIDIA-LICENSE`.

### Generate (from this `uefi/` directory)

Needs Python 3.6+ and the `zstandard` package (Windows has no `zstd`/`xz` by default; the wrapper uses Python `tarfile` + `zstandard`).

```powershell
python -m pip install zstandard
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Turing
```

Ampere / Ada: `-Arch Ampere` or `-Arch Ada`. `-Offline` rechecks files already in the cache without HTTPS. `-CacheDir` / `-OutputDir` override paths.

The NVIDIA Driver License Agreement in the utils package still covers the extracted firmware. Personal local packaging is the intended use; this tree does **not** claim redistribution rights. See `firmware\<arch>\NOTICE.md`.

Copy onto the ESP at Install time (`-FirmwareDir .\assets\firmware\Turing`, or omit it when the loader is named `UnlockLoader-<Arch>.efi` and that folder already has `.bin` files). File lists: [Inject on real hardware](#inject-on-real-hardware). More detail: [firmware/README.md](firmware/README.md).

---

## Clear

Delete host EFI copies and the EDK2 `Build\GpuUnlock*` trees. This does not uninstall from the ESP and does not delete the EDK2 or NASM clones.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Firmware
```

`-Arch` limits the wipe to that architecture's `UnlockLoader-<Arch>.efi` and its EDK2 output directory. `-Firmware` also drops staged `assets\firmware\` and `assets\firmware\<Arch>\` blobs. `-WhatIf` prints the paths without deleting.

---

## Hyper-V guest

Install / skip / enter-Windows was tested in this setup. Real-GPU inject was not.

1. Create a **Generation 2** VM and install Windows 10/11 x64.
2. Shut it down. Hyper-V Manager → VM → Settings → **Security**:
   - Clear **Enable Secure Boot**
3. Copy `build\UnlockLoader-Turing.efi` and the whole `tools\` folder into the guest (shared disk, ISO, or `Copy-VMFile`).
4. In the guest, run the install command from the next section in an **elevated** PowerShell.
5. Reboot. The menu should say `GPU Unlock Loader 0.2.6 (Turing)`. Press Enter or wait out the timeout; Windows should start.

With no NVIDIA GPU passed through, Inject is still safe: `gpu_count=0`, no MMIO.

Host TITAN RTX inject is documented in [Benchmark](#benchmark): registers unlock, GEMM does not move.

---

## Install

On the **target Windows** (the VM), elevated PowerShell:

```powershell
cd <tools directory on the guest>
powershell -ExecutionPolicy Bypass -File .\Install-GpuUnlockBoot.ps1 -LoaderEfi <full path to UnlockLoader-Turing.efi>
```

Example:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-GpuUnlockBoot.ps1 -LoaderEfi C:\Users\Public\UnlockLoader-Turing.efi
```

The script:

- refuses to run without elevation
- refuses to replace a non-PE `bootmgfw.efi`
- copies the original to `EFI\turing-unlock\bootmgfw_ms.efi`, `EFI\Microsoft\Boot\bootmgfw_ms.efi`, and a timestamped `backup\` folder
- writes `EFI\turing-unlock\config.ini` (`default=skip`)
- installs the wrapper as `EFI\Microsoft\Boot\bootmgfw.efi` and `EFI\turing-unlock\TuringUnlockLoader.efi`
- registers scheduled task `GpuUnlock Boot Guard` (once, 1 min after startup) and starts it once now. The task runs `tools\Protect-GpuUnlockBoot.ps1` from this directory; **do not delete the repo after a successful install** or the task has nothing to execute. Move the tree only if you re-run Install (or `Protect-GpuUnlockBoot.ps1 -RegisterTask`) from the new path.

Useful parameters:

| Parameter | Meaning |
| --- | --- |
| `-TimeoutSec 20` | menu timeout (default 20) |
| `-DefaultAction skip` | `skip` or `inject` |
| `-AlsoFallback` | also replace `\EFI\Boot\bootx64.efi` when it matches the original bootmgfw |
| `-SkipScheduledTask` | do not register the protect task |
| `-FirmwareDir <dir>` | copy that arch's blobs onto the ESP `firmware\` directory (needed only for real-hardware inject) |

**Re-run install after every rebuild**, or the ESP still has the old wrapper. The menu title must match the arch you built, for example `0.2.6 (Turing)`.

### ESP or USB

Turing belongs on the internal ESP. `Install-GpuUnlockBoot.ps1` replaces `\EFI\Microsoft\Boot\bootmgfw.efi` there. Before Windows starts, the loader writes the signed `bootmgfw.efi` back on that same volume. A startup task puts the wrapper back after Windows is up. There is no boot-target list. Turing's firmware set fits a stock ESP, which is often about 100 MB, next to the existing Windows boot files.

Ampere and Ada belong on a USB stick. Both need `gsp_ga10x.bin`, about 80 MB. A stock ESP of about 100 MB does not have room for that file beside `bootmgfw.efi` and the other Windows boot files. `New-GpuUnlockUsb.ps1` only copies onto a FAT32 stick. It does not mount or change the internal ESP. Firmware boot menu selects the stick. After the loader menu, USB mode lists EFI loaders on the internal NVMe and SATA disks (Windows Boot Manager, GRUB, systemd-boot) and starts the one you pick. Secure Boot stays off.

Ampere and Ada can use the same ESP install as Turing after that volume is large enough for the 80 MB blob and the files already on it. Enlarging the ESP is your own step, with a partition tool you already use, such as MiniTool Partition Wizard. This README does not describe those tools. When the ESP has the free space, run `Install-GpuUnlockBoot.ps1` with `-FirmwareDir .\assets\firmware\Ampere` or `Ada`.

### USB stick

The stick is the Ampere and Ada path, and a Turing path when you want to leave the internal ESP alone. `Install-GpuUnlockBoot.ps1` is unchanged and remains the ESP install.

Format the stick yourself before running the script. In File Explorer, right-click that USB drive, choose Format, and select FAT32. Explorer does not offer FAT32 for a volume larger than 32 GB. In that case, create a 32 GB or smaller volume in Disk Management, then format that volume as FAT32 in Explorer. This script never formats a disk.

Elevated PowerShell, after the loaders and firmware blobs are built. Name the FAT32 letter:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\New-GpuUnlockUsb.ps1 -DriveLetter H -Arch Ampere
```

Omit `-DriveLetter` and the script prints every FAT32 USB letter. A device with more than one letter is one line per letter. Type the number of the volume to write. A letter that is not FAT32 is refused, with the Explorer steps above.

The script refuses a volume that is not on a USB disk, and the boot or system disk. The chosen letter must already be FAT32 and have enough free space.

| Parameter | Meaning |
| --- | --- |
| `-DriveLetter H` | which USB letter to write. `H` and `H:` are the same. Omit this to pick from the list |
| `-Arch Turing,Ampere,Ada` | which loaders and firmware to copy (default: all three) |
| `-BootArch Ampere` | which loader is `EFI\BOOT\BOOTX64.EFI`. Required when `-Arch` lists more than one |
| `-LoaderDir` | directory of `UnlockLoader-<Arch>.efi` (default `build\`) |
| `-FirmwareRoot` | directory that contains `Turing\`, `Ampere\`, `Ada\` (default `assets\firmware\`) |
| `-TimeoutSec 20` | menu timeout written into `config.ini` |
| `-DefaultAction skip` | `skip` or `inject` |

The stick gets `EFI\BOOT\BOOTX64.EFI`, `EFI\turing-unlock\UnlockLoader-<Arch>.efi`, that arch's firmware, `config.ini`, and the marker file `EFI\turing-unlock\usb-boot`. Files already on the reused partition stay, except `EFI\BOOT\BOOTX64.EFI`, `UnlockLoader-*.efi`, and `EFI\turing-unlock\firmware\`, which this script replaces. If both Ampere and Ada are selected, their `gsp_ga10x.bin` hashes must match or the script stops before it writes the disk. Secure Boot must be off. Pick the stick from the firmware boot menu.

The loader decides the mode from files on its own volume. A signed `bootmgfw` backup means `boot mode=esp`: restore the wrapper on that volume only, and do not scan other disks. The `usb-boot` marker means `boot mode=usb`: leave every `bootmgfw.efi` untouched, connect NVMe and SATA controllers, and show the EFI loader list. Both present: the backup wins, so do not copy `bootmgfw_ms.efi` onto the stick. Neither file: `boot mode=unmarked` tries this volume, then the same list. The mode is on screen and in `unlock.log` before the list. On a USB boot the log stays on the stick; `Copy-GpuUnlockLog.ps1` only reads the internal ESP.

A listed file has to be an x64 UEFI application and must not contain this loader's magic. One Windows volume contributes `\EFI\Microsoft\Boot\bootmgfw.efi`. Each other `\EFI\<name>\` directory contributes `shimx64.efi`, or `grubx64.efi` when shim is absent, and `systemd-bootx64.efi` when that file is present. `\EFI\BOOT\bootx64.efi` is listed only when that volume contributed nothing else. The line includes the NVMe, SATA, ATAPI, or SCSI node from the device path. Volumes whose device path is USB are skipped. The number has no timeout. One entry still waits for Enter. Esc returns to firmware. The chosen loader is started with that FAT volume's device path, so GRUB reads `grub.cfg` and its modules from the same partition. A `bootmgfw.efi` that is still this loader is not listed, and USB mode does not restore it; boot the ESP install to run that restore. A BIOS GRUB `core.img` is not on the FAT partition, so it is not listed. An EFI stub `vmlinuz` is not listed either. The distro's EFI loader is what starts Linux.

---

## Boot menu

```
GPU Unlock Loader 0.2.6 (Turing)
NVIDIA display GPUs: 1  (feat read-only until I)
  0000:65:00.0  10de:1e02  Turing  (I forces)
    PLM=fffffff8  SS0=00000000  SS1=00000007  RO=00000000

  [Enter]  Continue WITHOUT inject   (default)
  [I]      Inject (force same-arch IDs) then show regs
  [R]      Chainload now, skip the inject screen

Default in 20 sec: SKIP inject
```

`I` injects whitelist IDs and also readout-only IDs of the **built** arch (TITAN RTX `10de:1e02` on a Turing build). After inject the loader prints `Feat AFTER inject` and waits 20 seconds or any key. On an ESP install it then restores and starts Windows. On a USB boot it then shows the loader list. Needs the TU102 firmware blobs on the loader volume or inject errors out before MMIO writes.

`0.2.3` connects PCI USB host controllers (xHCI/EHCI/UHCI/OHCI) and `ConsoleInHandle` before the menu so a USB keyboard works when firmware Fast POST skipped USB hand-off. It does not recursively connect every PCI device (that can rebind NVIDIA GOP). Default SKIP still leaves the menu after 20 seconds with no key.

On USB, skip and emergency go straight to the loader list. Inject goes there after the feature screen. The list itself waits for a number:

```
Boot targets on NVMe and SATA:

  [1]  Windows Boot Manager
       \EFI\Microsoft\Boot\bootmgfw.efi
       NVMe ns=1 part=1
  [2]  ubuntu
       \EFI\ubuntu\shimx64.efi
       SATA port=0 part=1

Type a number and press Enter.
Esc returns to firmware.
```

A healthy ESP skip-then-boot screen looks like:

```
Inject skipped. Continuing to boot.
restoring signed bootmgfw.efi from \EFI\Microsoft\Boot\bootmgfw_ms.efi (...)
restore ok: \EFI\Microsoft\Boot\bootmgfw.efi (...)
Starting EFI loader...
```

After Windows is up, `GpuUnlock Boot Guard` writes the wrapper back to `bootmgfw.efi` (about 1 minute after startup). The boot that just chain-loaded Windows used the restored Microsoft file, so it has no menu; the **next** reboot shows the menu. If Windows Update puts official `bootmgfw.efi` back during a session, the next reboot may skip the menu once; the startup task then pastes the wrapper for the reboot after that.

---

## Loader log

Before chainload the loader writes `\EFI\turing-unlock\unlock.log` on the volume it was started from. That file includes the `handoff` line (PLM/SS0/SS1/readout, WPR2, boot, GSP/SEC CPUCTL, PCI command, BAR0), `boot mode=esp|usb|unmarked`, and after inject one `unlock 10de:<id> result=<errno>` line per GPU. `inject finished status=0` means every attempted GPU returned 0. A failure leaves `status=8000000000000007` (`EFI_DEVICE_ERROR`) and the errno on the per-GPU line. Chainload still runs: on an ESP install that is Windows Boot Manager, and on USB it is the target list. A USB choice is logged as `boot choice <n> <title> <path>` before the selected loader starts. The internal ESP is not mounted for a normal Windows session. Copying its log off requires an elevated PowerShell. Run this from the tree you want the copy in (the compile tree, if that is where you build):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Copy-GpuUnlockLog.ps1
```

The script mounts the ESP when it has no drive letter, copies `EFI\turing-unlock\*.log` into this tree's `logs\`, then removes a drive letter it assigned itself. A letter that was already mounted is left alone. The newest log is saved as both of these:

- `logs\unlock.log`
- `logs\unlock-yyyyMMdd-HHmmss.log` (timestamp from the file on the ESP)

`logs\` is gitignored. `-Destination` overrides the output directory. `-EspRoot` uses an ESP root you already mounted, such as `S:`.

---

## Uninstall and recovery

### A. Windows still boots

Elevated:

```powershell
.\Uninstall-GpuUnlockBoot.ps1
```

Restores official `bootmgfw.efi` from `bootmgfw_ms.efi` and removes the scheduled task. The on-disk folder name stays `EFI\turing-unlock` (loader path). Without `-Purge` that folder is kept (loader copy, bootmgfw backups, **firmware blobs**).

```powershell
.\Uninstall-GpuUnlockBoot.ps1 -Purge
```

`-Purge` deletes `EFI\turing-unlock\` recursively (including `firmware\*.bin`), `EFI\Microsoft\Boot\bootmgfw_ms.efi`, and `C:\ProgramData\GpuUnlock`. It prints the firmware file list before removing it.

### B. Windows will not boot, ESP files are intact (prefer this)

Boot Windows setup USB / recovery → **Command Prompt**. Do **not** format first. Copy the signed backup over the official name; BCD can stay:

```
X:\> diskpart
DISKPART> list vol
DISKPART> sel vol <ESP volume number>
DISKPART> assign letter=S
DISKPART> exit

X:\> copy /y S:\EFI\turing-unlock\bootmgfw_ms.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
```

The ESP is usually FAT32, 100–500 MB, label often `SYSTEM`. If `turing-unlock` is missing:

```
X:\> copy /y S:\EFI\Microsoft\Boot\bootmgfw_ms.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
```

Reboot. Windows should start (the wrapper is gone).

### C. Recovery `0xc0000428` with the wrapper still at the official path

Windows is hashing `\EFI\Microsoft\Boot\bootmgfw.efi`, which is still the unsigned wrapper. Use the copy in B. Formatting is unnecessary.

---

## Rebuild the ESP from Windows setup / USB

**Last resort** when the ESP is corrupt, BCD is gone, and the backups are gone. `format` wipes every boot file on the EFI partition (wrapper, backups, BCD). If a backup copy still exists, use [B](#b-windows-will-not-boot-esp-files-are-intact-prefer-this) instead.

Boot the Windows install ISO / USB → **Repair your computer** → **Command Prompt**.

`C:` in WinPE is often **not** the Windows volume. List volumes first:

```
diskpart
   list vol
   sel vol <EFI volume number>
   assign letter=S:
   exit
```

While in `list vol`, note the Windows volume (NTFS, large). If Windows is not `C:`, change `C:\Windows` below to the real letter, for example `W:\Windows`.

After confirming `S:` is the small FAT32 EFI partition:

```
format S: /fs:FAT32 /q

bcdboot C:\Windows /s S: /f UEFI
```

`bcdboot` recreates `bootmgfw.efi` and BCD under `S:\EFI\Microsoft\Boot\`. Reboot into Windows. The wrapper is gone.

If `bcdboot` cannot find Windows:

```
dir C:\Windows\System32\winload.efi
dir D:\Windows\System32\winload.efi
dir E:\Windows\System32\winload.efi
```

Then run `bcdboot` against the volume that actually has `winload.efi`.

Hyper-V: attach an official Windows ISO, put DVD first in Firmware boot order, choose Repair.

---

## Benchmark

Script: [`bench/gemm_fp16.py`](bench/gemm_fp16.py). PyTorch → cuBLAS, FP16 inputs, tensor cores, FP32 accumulate inside. Sizes match the Linux README table (1024 / 2048 / 4096 / 8192).

**Read this first**

- Real GPUs measured with this tree: a host **TITAN RTX** (full 72-SM TU102) and an **RTX 3070 Ti** (GA104 `10de:2482`). Each comparison is stock or skip, then one UEFI inject, then GEMM again.
- The UEFI wrapper boot path is tested in a VM with no NVIDIA GPU. VM GEMM numbers are not comparable to this table.
- TITAN RTX is a full 72-SM TU102; 2080 Ti is 68 SM. Inject writes GOP-era SS to full (`SS0=88888888`, `SS1=8`) but does not add the ~1.5x GEMM seen on 2080 Ti.

On the host (physical machine with an NVIDIA GPU):

```powershell
pip install torch
python .\bench\gemm_fp16.py
python .\bench\gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80
```

Same host, 2026-10-25, Windows, driver 617.42, PyTorch 2.9.0+cu130, CUDA 13.0, power limit 320 W. Command: `python .\bench\gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80`. Values are the script's median TFLOPS.

- **Stock** (no UEFI inject): after the run 309 W, SM 1935 MHz, 49 C.
- **After UEFI inject**: loader `TU102 FEAT UNLOCKED`, `Feat AFTER` `PLM=FFFFFFFF SS0=88888888 SS1=00000008`. Then three GEMM runs in Windows; table uses the median of those three medians. After the runs ~285–290 W, SM 1920–1965 MHz, 47–50 C.

2080 Ti stock / unlocked columns are from the Turing Linux README (Dell RTX 2080 Ti 22GB @250W, cuBLAS FP16 / FP32 accumulate). 100% peak = 56.9 TFLOPS (*NVIDIA Turing Architecture Whitepaper*).

| M.N.K | TITAN RTX stock | TITAN RTX after inject | 2080 Ti stock | 2080 Ti unlocked |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 44.2 (77.6%) | 44.8 (78.7%) | 50.3 (88.4%) | 68.3 (120.0%) |
| 2048 | 87.6 (153.9%) | 87.4 (153.6%) | 55.7 (97.9%) | 91.4 (160.6%) |
| 4096 | 95.4 (167.6%) | 95.1 (167.1%) | 53.7 (94.4%) | 81.7 (143.6%) |
| 8192 | 99.3 (174.5%) | 99.1 (174.2%) | 52.8 (92.8%) | 77.9 (136.9%) |

Three after-inject medians: 1024 = 44.6 / 44.8 / 45.3; 2048 = 87.4 / 87.3 / 87.4; 4096 = 94.9 / 95.1 / 96.0; 8192 = 99.1 / 99.4 / 98.2. Delta vs stock is within run-to-run noise (about 1%). No tensor-rate gain.

1024 is launch-overhead bound on this path (median ~0.048 ms). 2048 is close to unlocked 2080 Ti. 4096 and 8192 exceed unlocked 2080 Ti, stock and after inject alike.

### RTX 3070 Ti

2026-10-27, driver 617.42, PyTorch 2.14.1+cu132, CUDA 13.2. Same script and sizes. Skip handoff: `PLM=ffffff8f SS0=04301072 SS1=3 RO=00400080`. The earlier inject the same day waited for SEC2 halt, timed out at `-110`, left those selectors unchanged, and GEMM matched skip.

The later inject is the SEC2 fix. Log banner `version=0.2.4`. `GA102 payload result` shows `boot=0 cpuctl=0x0 plm=ffffffff` with stock SS still in place, then `SS0=88888888` and `SS1=8` both read back, FLR kept them, and the log says `GA102 FEAT UNLOCKED`. Handoff: `PLM=ffffffff SS0=88888888 SS1=8 RO=0`, WPR2 `0:1ffffe00`, GSP halted, SEC2 `badf` (same idle engines as skip). `unlock 10de:2482 result=0`. Windows started. The display stays black for about a minute during boot. Wait. The script's `%peak` column is still the 2080 Ti 56.9 TFLOPS reference; compare the two runs on this card.

| M.N.K | skip | after inject | ratio |
| ---: | ---: | ---: | ---: |
| 1024 | 36.7 | 61.7 | 1.68x |
| 2048 | 42.5 | 75.2 | 1.77x |
| 4096 | 46.9 | 85.2 | 1.82x |
| 8192 | 48.5 | 86.3 | 1.78x |

Skip: SM 2010 MHz, about 283–296 W. After inject: SM 1815 MHz, about 309 W. Memory clock 9251 MHz on both. The higher TFLOPS is at a lower SM clock.

---

## Inject on real hardware

Tested on this host **NVIDIA TITAN RTX** (`10de:1e02`, full 72-SM TU102), loader 0.2.3, TU102 firmware on the ESP, force `I`:

- Loader: `TU102 FEAT UNLOCKED`, `Feat AFTER` `PLM=FFFFFFFF SS0=88888888 SS1=00000008`
- Keyboard works after Fast POST USB connect
- Windows GEMM matched stock within ~1% (TITAN is factory-full; no 2080 Ti-style ~1.5x)

Also tested on an **RTX 3070 Ti** (`10de:2482`, GA104), Ampere firmware, the SEC2 fix described above. Loader `GA102 FEAT UNLOCKED`. The display stays black for about a minute during boot. Wait. FP16 GEMM at 8192 rose from 48.5 to 86.3 TFLOPS. See [Benchmark](#benchmark).

Inject on a GeForce 2080 Ti or on Ada is **not** validated here. Use at your own risk. Full-speed tensor cores draw a lot of power and can damage hardware.

Each architecture needs its **own** blob set. How to fetch and generate them is in [Firmware blobs](#firmware-blobs). TITAN RTX is TU102; Turing is enough. Without those files the inject path errors out **before** FLR/MMIO. A VM has no card, so they are optional there.

Turing (`-Arch Turing`; success path writes SS0=`0x88888888`, SS1=`0x8`, then **does not FLR**):

- `booter_load_tu102.bin`, `v67-tu102-610.43.03.bin`, `gsp_tu10x.bin`, `gsp_boot_tu102.bin`, `gsp_boot_desc_tu102.bin`, `sec2_bl_tu102.bin`

Ampere (`-Arch Ampere`; success path **does** FLR):

- `booter_load_ga102.bin`, `v67-ga102-610.43.03.bin`, `gsp_ga10x.bin`, `gsp_boot_ga102.bin`, `gsp_boot_desc_ga102.bin`

Ada (`-Arch Ada`; four-PLM V67, success path **does** FLR):

- `booter_load_ad102.bin`, `v67-ad102-610.43.03-four-plm.bin`, `gsp_ga10x.bin`, `gsp_boot_ad102.bin`, `gsp_boot_desc_ad102.bin`

```powershell
.\tools\Install-GpuUnlockBoot.ps1 -LoaderEfi .\build\UnlockLoader-Turing.efi -FirmwareDir .\assets\firmware\Turing
```

If `-FirmwareDir` is omitted and the loader is `UnlockLoader-<Arch>.efi`, Install uses `assets\firmware\<Arch>\` when that folder already has `.bin` files.

Firmware binaries remain under the NVIDIA license. See `firmware/<arch>/NOTICE.md`. This tree does not ship those blobs.

---

## Troubleshooting

| Symptom | Meaning | What to do |
| --- | --- | --- |
| Firmware boots Windows with no menu | this boot used the restored Microsoft `bootmgfw.efi`, or Secure Boot is on, or the wrapper is gone | expected once after a loader/WU restore; next reboot should show the menu. If it still skips: turn SB off, run `Protect-GpuUnlockBoot.ps1 -RegisterTask`, or reinstall |
| `LoadImage failed F` / `Access Denied (0x800000000000000F)` | old loader used a memory buffer with a NULL path | use 0.1.1+, reinstall |
| Recovery `0xc0000428` | CI hashed unsigned wrapper at `bootmgfw.efi` | use 0.1.2+ (restore then chainload), or copy the backup as in [B](#b-windows-will-not-boot-esp-files-are-intact-prefer-this) |
| Menu is not `0.2.6 (<Arch>)` | the booted volume still has an old EFI | ESP install: run Install again. USB: rewrite the stick |
| Keyboard dead in the menu (Fast POST / Fast Boot) | firmware did not start USB xHCI | 0.2.3+ connects USB HC before the menu; reinstall |
| `firmware missing (status=e)` / `load_fw error=-2` | the volume that started the loader has no blobs for the built arch | build the firmware, then ESP: Install `-FirmwareDir .\assets\firmware\<Arch>`. USB: run `New-GpuUnlockUsb.ps1` again |
| Menu gone after one successful Windows boot | protect task has not run yet, or was never registered | wait ~1 min after login (`C:\ProgramData\GpuUnlock\protect.log`), or elevated `Protect-GpuUnlockBoot.ps1 -RegisterTask` |
| Prepare `[fail] NASM` / `[fail] EDK2` | toolchain or clone missing | `Prepare-GpuUnlock.ps1 -FetchNasm` and/or `-FetchEdk2` |
| Build fails on NASM | EDK2 assembles `.nasm` | extract to `nasm-3.02\nasm.exe` or `-FetchNasm` |
| `LNK2001 memset` | old DSC did not link CompilerIntrinsicsLib | this tree's DSC already includes it; rebuild from current sources |
| `C2371` `size_t` in `memset_ms.c` / `memcpy_ms.c` | EDK2 `edk2-stable202411` typedefs `size_t` as 32-bit on x64 | use `edk2-stable202608` (202408 has no `CompilerIntrinsicsLib`) |
| missing `GenFw.exe`, message says re-clone | BaseTools has not been built | from an x86 Native Tools prompt: `edksetup.bat Rebuild` |
| `error 0004: File read failure` on `Loader.inf` | INF/DSC/DEC contained non-ASCII (EDK2 opens them with the Windows ANSI code page, often cp936) | current INF files are ASCII only; rerun Prepare (it now fails on non-ASCII meta files) |
| `Cannot find BaseTools Bin Win32` during a successful build | `toolsetup.bat` tested `%EDK_TOOLS_BIN%` before delayed expansion applied | current Build script sets `EDK_TOOLS_BIN` before `edksetup.bat`; rebuild from current tools |

---

## License

Source in this tree is **GPL-2.0-only**, same as the three upstream Linux modules. See [LICENSE](LICENSE) (GNU GPL v2 text) and [NOTICE](NOTICE) (attribution).

The upstreams are version 2 only, so this combined work stays GPL-2.0-only. SPDX headers on the C sources match. NVIDIA firmware stays under NVIDIA's license and is not in git. NVIDIA MIT notices for inspected headers stay with the upstream `licenses/` trees.

---

## Layout

```
.                          this directory = standalone repo root
  GpuUnlockPkg/
    Application/Loader/    common boot wrapper (menu, chainload, PCI scan)
    Arch/Turing/           TU102 inject (build with -Arch Turing)
    Arch/Ampere/           GA102 inject (build with -Arch Ampere)
    Arch/Ada/              AD102 inject (build with -Arch Ada)
    Include/               shared headers
  tools/                   Prepare / Build EFI / Build firmware / Clear / Install / USB stick / Uninstall / Protect / Copy log
  logs/                    ESP unlock.log copies (gitignored)
  firmware/                per-arch blob generators (turing, ampere, ada)
  assets/config.ini
  assets/firmware/<Arch>/  generated blobs (gitignored)
  bench/gemm_fp16.py
  build/                   build output (gitignored)
  nasm-3.02/               local download, not in git
  edk2/                    local download, not in git
  LICENSE                  GPL-2.0-only
  NOTICE                   attribution to the three upstream Linux modules
  VERSION                  0.2.6
  README.md                English
  README_zh.md             Chinese
```