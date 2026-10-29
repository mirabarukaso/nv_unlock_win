> [!IMPORTANT]
> Secure Boot was off for these runs.
>
> - **TITAN RTX** (TU102, `10de:1e02`), Windows 10, driver 617.42. Loader printed `TU102 FEAT UNLOCKED` with `SS0=88888888` `SS1=8`. Windows started. FP16 GEMM at 8192 stayed 99.3 → 99.1 TFLOPS. This card is already factory-full.
> - **RTX 3070 Ti** (GA104, `10de:2482`), Windows 10, driver 617.42. Loader printed `GA102 FEAT UNLOCKED` with `SS0=88888888` `SS1=8`. Windows started. The display stays black for about a minute during boot. Wait. FP16 GEMM (FP32 accumulate) at 8192 rose from 48.5 to 86.3 TFLOPS (+78%).
> - **RTX 4080** (AD103, `10de:2704`) works on Windows 11 + UEFI loader **0.2.12**, driver 617.42 — FP16 98.6→160.4 TFLOPS (+63%), BF16 98.4→163.7 (+66%) ([#3](https://github.com/mirabarukaso/nv_unlock_win/issues/3)). Needs `rtx4080_experimental=1` and an **exact** PCI BDF in `targets` (not `all`). Details: **[docs/RTX40_Ada.md](docs/RTX40_Ada.md)**.
> - **RTX 4090** (AD102, `10de:2684`) — **needs re-verification**. Loader printed `AD102 FEAT UNLOCKED` with `SS0=88888888` `SS1=8`. Windows started. Published Windows GEMM numbers are **withdrawn** pending a re-benchmark (unlocked 4080 0.2.12 MMA already exceeds the old figures). Config support for base ID `2684` is unchanged.

[中文README](README_zh.md)

# GPU Unlock UEFI loader (Windows)

Unsigned EFI loader for Turing, Ampere, and Ada. Boot it from a FAT32 USB stick, optionally unlock the GPU, then start Windows, GRUB, systemd-boot, or another x64 EFI loader on an internal disk. The stick does not modify the internal EFI system partition (ESP), so the firmware boot menu can still start the systems already installed there.

Version `0.2.13`. License: GPL-2.0-only, see [LICENSE](LICENSE) and [NOTICE](NOTICE). History is at the [end](#changelog).

Most people stop after [Use](#use): download a release EFI, generate the matching firmware, copy both onto a USB stick. [Build from source](#build-from-source) is only for compiling the EFI yourself.

Linux module sources this tree ports (Ada Windows handoff also credits TOTORONG):

| Arch | Upstream | Release EFI |
| --- | --- | --- |
| Turing | https://github.com/nlzy/turing-unlock | `UnlockLoader-Turing.efi` |
| Ampere | https://github.com/Zagitta/gpu-unlock | `UnlockLoader-Ampere.efi` |
| Ada | https://github.com/JustAResearcher/gpu-unlock-ada | `UnlockLoader-Ada.efi` |
| Ada (Windows handoff) | https://huggingface.co/TOTORONG/AD103_win_unlocker | — |

## Contents

**Use**

1. [Before you start](#before-you-start)
2. [How it works](#how-it-works)
3. [Get the EFI and the firmware](#get-the-efi-and-the-firmware)
4. [USB stick](#usb-stick)
5. [Boot menu](#boot-menu)
6. [Loader log](#loader-log)
7. [Ada / RTX 40-series](#ada--rtx-40-series) → details [docs/RTX40_Ada.md](docs/RTX40_Ada.md)
8. [If the machine will not start](#if-the-machine-will-not-start)
9. [Measured results](#measured-results)

**Advanced**

10. [ESP install](#esp-install)

**Build from source**

11. [Prerequisites](#prerequisites)
12. [Download the toolchain](#download-the-toolchain)
13. [Prepare and build](#prepare-and-build)
14. [Clear](#clear)
15. [Hyper-V guest](#hyper-v-guest)
16. [Build troubleshooting](#build-troubleshooting)
17. [Layout](#layout)

18. [License](#license)
19. [Changelog](#changelog)

---

## Use

### Before you start

Turn **Secure Boot off**. The loader is unsigned. Firmware will not load it while Secure Boot is on.

Keep a WinPE disk or a bootable Windows installation disc nearby before you try the [ESP install](#esp-install). The USB path does not rewrite the internal ESP, so the firmware menu can still boot the disk you already use. The install disc matters once `bootmgfw.efi` on that ESP has been replaced.

Pick the EFI that matches the GPU. Turing blobs do not unlock Ampere or Ada.

On an RTX 3070 Ti the display stays black for about a minute during boot. Wait.

### How it works

You select the USB stick in the firmware boot menu. The loader shows a short menu, reads the feature registers, and writes them only if you choose inject and the matching firmware is on the stick. It then lists EFI loaders on internal NVMe and SATA disks and starts the one you pick, using that volume's own device path. GRUB can read `grub.cfg` and its modules from that partition. A BIOS GRUB `core.img` and a bare EFI-stub `vmlinuz` are not listed. The distro's EFI loader is what starts Linux.

The internal ESP is left as it is. `bootmgfw.efi` on the Windows disk is not replaced, and no startup task is registered.

UEFI GOP fonts usually cannot draw CJK, so the menu is English.

### Get the EFI and the firmware

Download `UnlockLoader-<Arch>.efi` from the [GitHub release](https://github.com/mirabarukaso/nv_unlock_win/releases). You still need this repo for the scripts. NVIDIA firmware is not in git and is not in the release.

Generate one architecture. This downloads the pinned 610.43.03 inputs, checks SHA-256, and writes blobs. It does not compile the EFI. Python 3.6+ and the `zstandard` package are required.

```powershell
python -m pip install zstandard
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Turing
```

Use `-Arch Ampere` or `-Arch Ada` for those cards. Output is `assets\firmware\<Arch>\`. `-Offline` rechecks files already in the cache. The NVIDIA license in the utils package still covers the extracted firmware. See `firmware\<arch>\NOTICE.md` and [firmware/README.md](firmware/README.md).

| Arch | Files the loader reads |
| --- | --- |
| Turing | `booter_load_tu102.bin`, `v67-tu102-610.43.03.bin`, `gsp_tu10x.bin`, `gsp_boot_tu102.bin`, `gsp_boot_desc_tu102.bin`, `sec2_bl_tu102.bin` |
| Ampere | `booter_load_ga102.bin`, `v67-ga102-610.43.03.bin`, `gsp_ga10x.bin`, `gsp_boot_ga102.bin`, `gsp_boot_desc_ga102.bin` |
| Ada | `booter_load_ad102.bin`, `v67-ad102-610.43.03-four-plm.bin`, `gsp_ga10x.bin`, `gsp_boot_ad102.bin`, `gsp_boot_desc_ad102.bin` |

Turing's success path writes SS0=`0x88888888`, SS1=`0x8`, and does not FLR. Ampere and Ada FLR after a successful unlock. Missing blobs fail before any MMIO write (`firmware missing` / `load_fw error=-2`).

### USB stick

Use a USB stick for every architecture. Format it yourself in File Explorer: right-click the drive, Format, FAT32. Explorer does not offer FAT32 for a volume larger than 32 GB. Create a 32 GB or smaller volume in Disk Management, then format that volume as FAT32. `New-GpuUnlockUsb.ps1` never formats a disk.

Elevated PowerShell, from this repo. `-LoaderDir` is the folder that contains the release EFI. Omit it when `build\UnlockLoader-<Arch>.efi` is already there.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\New-GpuUnlockUsb.ps1 -DriveLetter H -Arch Turing -LoaderDir C:\path\to\release -FirmwareRoot .\assets\firmware
```

`H` and `H:` are the same. Omit `-DriveLetter` and the script lists every FAT32 USB letter. One device with several letters is one line per letter. Type the number of the volume to write. A letter that is not FAT32 is refused.

| Parameter | Meaning |
| --- | --- |
| `-DriveLetter H` | USB letter to write. Omit this to pick from the list |
| `-Arch Turing` | which loader and firmware to copy. `Ampere` or `Ada` for those cards |
| `-BootArch` | which loader becomes `EFI\BOOT\BOOTX64.EFI` when `-Arch` lists more than one |
| `-LoaderDir` | directory of `UnlockLoader-<Arch>.efi` (default `build\`) |
| `-FirmwareRoot` | directory that contains `Turing\`, `Ampere\`, `Ada\` (default `assets\firmware\`) |
| `-TimeoutSec 20` | menu timeout written into `config.ini` |
| `-DefaultAction skip` | `skip` or `inject` |

The script refuses a volume that is not on USB, and the boot or system disk. It replaces `EFI\BOOT\BOOTX64.EFI`, `UnlockLoader-*.efi`, and `EFI\turing-unlock\firmware\` on that letter. Other files on the stick stay. It also writes `config.ini` and the marker `EFI\turing-unlock\usb-boot`.

In the firmware boot menu, boot the stick. Secure Boot stays off.

### Boot menu

```
GPU Unlock Loader 0.2.13 (Turing)

NVIDIA display GPUs: 1
  0000:65:00.0  10de:1e02  Turing
    PLM=fffffff8  SS0=00000000  SS1=00000007  RO=00000000

 > Skip inject
   Inject
   Boot now

Up/Down, Enter to confirm  (20 sec)
```

Up and Down move the highlight. Enter confirms it. Until you press a key, the highlighted line starts when the timer reaches zero. That line is Skip unless `config.ini` says `default=inject`. On the USB stick all three then show the same target list. That list has no timer. Up and Down move, Enter starts the highlighted loader, Esc returns to firmware.

```
Select a boot target
Up/Down, Enter to start, Esc for firmware

 > Windows Boot Manager
   ubuntu

\EFI\Microsoft\Boot\bootmgfw.efi
NVMe ns=1 part=1
```

A listed file is an x64 UEFI application and does not contain this loader's magic. Windows contributes `\EFI\Microsoft\Boot\bootmgfw.efi`. Each other `\EFI\<name>\` directory contributes `shimx64.efi`, or `grubx64.efi` when shim is absent, and `systemd-bootx64.efi` when that file is present. `\EFI\BOOT\bootx64.efi` is listed only when that volume contributed nothing else. USB volumes are skipped. The line shows the NVMe, SATA, ATAPI, or SCSI node so two disks are easy to tell apart.

`I` also injects readout-only IDs of the built arch, such as TITAN RTX `10de:1e02` on a Turing build. Inject needs that arch's blobs on the stick.

### Loader log

Before the chosen loader starts, the stick gets `\EFI\turing-unlock\unlock.log`. PCI scan always logs each GPU as `Found <seg:bus:dev.fn>  10de:<id>  <Arch>` (skip and inject). It also includes `handoff` (PLM/SS0/SS1/readout, WPR2, boot, GSP/SEC CPUCTL, PCI command, BAR0), `boot mode=usb`, and after inject one `unlock 10de:<id> result=<errno>` line per GPU. `inject finished status=0` means every attempted GPU returned 0. A failure leaves `status=8000000000000007` (`EFI_DEVICE_ERROR`). The target list still appears. The choice is logged as `boot choice <n> <title> <path>`.

The log stays on the stick. Copy the file off after you boot an OS that can read that FAT volume. Register traces stay in that file, and so does each one-shot line the screen shows (the choice, unlock result, and a chainload error). The menu itself is redrawn every second and is not copied into the file. Set `verbose=1` in `config.ini` to mirror the file onto the screen.

If Explorer is showing the internal EFI system partition, elevated PowerShell removes that drive letter and leaves the files in place. USB disks are skipped.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Hide-GpuUnlockEsp.ps1
```

### Supported GPUs

Built from the ID lists in code (`Arch/Turing/TuringIds.c`, `Arch/Ampere/Ampere.c`, `Arch/Ada/arch.h` + `Ada.c`). Names from [pci.ids](https://pci-ids.ucw.cz/read/PC/10de).

| Series | Chip | PCI IDs in code (`10de:`) | Cards | Status |
| --- | --- | --- | --- | --- |
| 20 | TU102 | `1e02` (not listed, range match) | TITAN RTX | **Verified (ours)**: force-inject works, no gain (factory full) |
| 20 | TU102 | `1e07` | RTX 2080 Ti Rev. A | **Verified upstream** (nlzy, Dell 2080 Ti) |
| 20 | TU102 | `1e03 1e04 1e2d 1e2e` | RTX 2080 Ti 12GB / 2080 Ti / engineering samples | Listed, untested |
| 20 | TU104 | `1e81 1e82 1e84 1e87 1e89 1ec2 1ec7` | RTX 2080 SUPER / 2080 / 2070 SUPER / 2080 Rev. A / 2060 | Listed, untested |
| 20 | TU106 | `1f02 1f03 1f06 1f07 1f08 1f42 1f47` | RTX 2070 / 2060 12GB / 2060 SUPER / 2070 Rev. A / 2060 Rev. A | Listed, untested |
| 20 | other TU10x/TU11x | any `1e00-1fff` not listed | — | Range force-inject still allowed (old parts, no reported harm) |
| 30 | GA102 | `2200 2203 2204 2205 2206 2207 2208 220a 220d 2216 222b 222f` | RTX 3090 Ti / 3090 / 3080 Ti / 3080 / 3080 12GB / 3070 Ti (GA102) / CMP 90HX / ES; `2200` unnamed | Listed, untested |
| 30 | GA104 | `2482` | RTX 3070 Ti | **Verified (ours)**: FP16 GEMM +78% |
| 30 | GA104 | `2480 2483 2484 2486 2487 2488 2489 248a 248c 248d 248e 249c 249d 249f 24a0 24a4 24ac 24ad 24af 24bf 24c0 24c7 24c8 24c9 24dc 24dd 24df 24e0` | RTX 3070 / 3060 Ti / 3060 (GA104) / LHR / CMP 70HX / 3080-3070 Ti laptop / ES; `2480 2483 249f 24a4 24c0 24df` unnamed or dev IDs | Listed, untested |
| 30 | GA103 | `2414` | RTX 3060 Ti (GA103) | Listed, untested |
| 30 | GA106 | `2504` | RTX 3060 LHR (12GB) | **Verified upstream** (Zagitta) |
| 30 | GA106 | `2501 2503 2505 2509 252f 2544` | RTX 3060 / 3060 12GB Rev. 2 / ES; `2505` unnamed | Listed, untested |
| 30 | GA106 | `2507 2508` | **RTX 3050 / 3050 OEM** | **Not supported** (skipped in 0.2.13) |
| 30 | GA107 / laptop | not listed (`2582-25ec`, `2523 2563` …) | **RTX 3050** desktop / laptop | **Not supported**: unlisted Ampere IDs are skipped |
| 40 | AD102 | `2684` | RTX 4090 | Base ID, `targets=all` ok. Boot/inject OK; **perf needs re-verification** |
| 40 | AD102 | `2685` | RTX 4090 D | `rtx4090d_experimental=1`, untested |
| 40 | AD103 | `2704` | RTX 4080 | `rtx4080_experimental=1` + exact BDF. **Verified (ours, 0.2.12)** |
| 40 | AD103 | `2702` | RTX 4080 SUPER | `ad103_experimental=1` + exact BDF, untested |
| 40 | AD103 | `2705` | RTX 4070 Ti SUPER | `ad103_experimental=1` + exact BDF, untested |
| 40 | AD106 | `2805` | RTX 4060 Ti 16GB | `ad106_experimental=1` + exact BDF, untested |
| 40 | AD107 | not listed (`28a1 28e1` …) | **RTX 4050** laptop | **Not supported**: Ada admission rejects it |

**RTX 3050 and RTX 4050 are not supported.** Any ID not in this table on Ampere/Ada is skipped with a log line; only Turing still force-injects by range.

### Ada / RTX 40-series

Stock **RTX 4090** (`10de:2684`) is the Ada **base** ID: `UnlockLoader-Ada.efi`, `targets=all` (or a listed BDF), no experimental flag. Inject/boot support stands; **published Windows GEMM numbers are withdrawn** pending re-benchmark (**needs re-verification**).

Other Ada IDs in this tree need a config flag; several also need an **exact** BDF. Summary:

| Card | ID | Flag | `targets` |
| --- | --- | --- | --- |
| RTX 4090 | `2684` | *(base)* | `all` or BDF |
| RTX 4090 D | `2685` | `rtx4090d_experimental=1` | `all` or BDF |
| RTX 4080 | `2704` | `rtx4080_experimental=1` | **exact BDF** |
| RTX 4080 SUPER | `2702` | `ad103_experimental=1` | **exact BDF** |
| AD103 canary | `2705` | `ad103_experimental=1` | **exact BDF** |
| AD106 | `2805` | `ad106_experimental=1` | **exact BDF** |

- Session-only unlock; Inject again after reboot.
- Turn **Fast Startup** off between inject tests (UI or `HiberbootEnabled=0`; missing key often means Fast Startup is on). Full registry steps: [docs/RTX40_Ada.md](docs/RTX40_Ada.md#ops-notes-4080--primary-display-gpu).
- Ada Windows handoff (0.2.12+) credits [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker) (code GPL-2.0-only; new docs CC BY-NC 4.0). Details: [docs/RTX40_Ada.md](docs/RTX40_Ada.md#credits-ada-windows-handoff).
- Full per-card examples, BDF howto, `skip_flr`, Fast Startup / `HiberbootEnabled`, TOTORONG credit, and 4080 MMA tables: **[docs/RTX40_Ada.md](docs/RTX40_Ada.md)**.

### If the machine will not start

Boot the internal disk from the firmware menu and leave the stick unplugged. That disk was not modified by the USB script.

If you used the ESP install below and Windows stops with `0xc0000428`, the official path is still the unsigned loader. Boot WinPE or Windows setup, open a command prompt, and copy the signed backup back. Do not format the ESP when the backup is still there:

```
diskpart
list vol
sel vol <ESP volume number>
assign letter=S
exit
copy /y S:\EFI\turing-unlock\bootmgfw_ms.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
```

The ESP is usually FAT32, about 100–500 MB. If `turing-unlock` is missing, copy `S:\EFI\Microsoft\Boot\bootmgfw_ms.efi` over the official name instead.

Formatting the ESP and running `bcdboot` is the last resort, only when the backups and the BCD are already gone. From the same WinPE prompt, after `S:` is the small FAT32 EFI partition and `C:\Windows` is the real Windows volume:

```
format S: /fs:FAT32 /q
bcdboot C:\Windows /s S: /f UEFI
```

### Measured results

Two scripts, same sizes (1024 / 2048 / 4096 / 8192). PyTorch calls cuBLAS. The inputs are FP16 or BF16, tensor cores do the multiply, and the dot product accumulates in FP32. The output matrix stays in the input type. `FP16→FP32` means that accumulation, not an FP32 result buffer.

30-series and 40-series cards should run both, once on Skip and once after Inject. Compare TFLOPS on that same card. Turing has no BF16 tensor cores, so a 20-series GPU only runs the FP16 script.

```powershell
pip install torch
python .\bench\gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80
python .\bench\gemm_bf16.py --sizes 1024,2048,4096,8192 --repeat 80
```

**TITAN RTX** (`10de:1e02`, full 72-SM TU102), 2026-10-25, Windows 10, driver 617.42, PyTorch 2.9.0+cu130, CUDA 13.0, power limit 320 W. Loader `TU102 FEAT UNLOCKED`, `PLM=FFFFFFFF SS0=88888888 SS1=00000008`. Values are median TFLOPS. The 2080 Ti columns are from the Turing Linux README (Dell RTX 2080 Ti 22GB @250W), not from this tree. 100% peak in those columns is 56.9 TFLOPS.

| M.N.K | TITAN RTX stock | TITAN RTX after inject | 2080 Ti stock | 2080 Ti unlocked |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 44.2 (77.6%) | 44.8 (78.7%) | 50.3 (88.4%) | 68.3 (120.0%) |
| 2048 | 87.6 (153.9%) | 87.4 (153.6%) | 55.7 (97.9%) | 91.4 (160.6%) |
| 4096 | 95.4 (167.6%) | 95.1 (167.1%) | 53.7 (94.4%) | 81.7 (143.6%) |
| 8192 | 99.3 (174.5%) | 99.1 (174.2%) | 52.8 (92.8%) | 77.9 (136.9%) |

Stock after the run: 309 W, SM 1935 MHz, 49 C. After inject: about 285–290 W, SM 1920–1965 MHz, 47–50 C. The delta is within run-to-run noise.

**RTX 3070 Ti** (`10de:2482`, GA104), 2026-10-27, Windows 10, driver 617.42, PyTorch 2.14.1+cu132, CUDA 13.2. `GA102 FEAT UNLOCKED`, handoff `PLM=ffffffff SS0=88888888 SS1=8`. Windows started. The display stays black for about a minute during boot. Wait. The script's `%peak` column is still the 2080 Ti 56.9 TFLOPS reference.

| M.N.K | skip | after inject | ratio |
| ---: | ---: | ---: | ---: |
| 1024 | 36.7 | 61.7 | 1.68x |
| 2048 | 42.5 | 75.2 | 1.77x |
| 4096 | 46.9 | 85.2 | 1.82x |
| 8192 | 48.5 | 86.3 | 1.78x |

Skip: SM 2010 MHz, about 283–296 W. After inject: SM 1815 MHz, about 309 W. Memory clock 9251 MHz on both.

**RTX 4080** (`10de:2704`, AD103) works on Windows 11 + UEFI loader **0.2.12**, driver 617.42 — FP16 98.6→160.4 TFLOPS (+63%), BF16 98.4→163.7 (+66%) ([#3](https://github.com/mirabarukaso/nv_unlock_win/issues/3)). Loader printed `AD102 FEAT UNLOCKED` with `SS0=88888888` `SS1=8`. `mma_bench_sm89` tables, config, and ops notes: [docs/RTX40_Ada.md](docs/RTX40_Ada.md#mma-results-rtx-4080-0212).

**RTX 4090** (`10de:2684`, AD102) — **needs re-verification.** Loader 0.2.7 printed `AD102 FEAT UNLOCKED`, handoff `PLM=ffffffff SS0=88888888 SS1=8`; Windows started. Earlier published Windows cuBLAS FP16 GEMM figures for this SKU are **withdrawn** pending a re-benchmark (unlocked 4080 0.2.12 MMA already exceeds the old figures). Base Ada config for `2684` is unchanged.

On Ada, register-resident issue-rate probes (such as `mma_bench_sm89`) and large cuBLAS GEMM can disagree a lot: GEMM often sits on the power limit. Prefer the issue-rate probe for unlock proof.

Inject on a GeForce 2080 Ti is not validated here. Full-speed tensor cores draw a lot of power and can damage hardware.

### Runtime troubleshooting

| Symptom | What to do |
| --- | --- |
| Stick is not listed, or the menu never appears | Secure Boot is still on, or the firmware boot menu was not set to the stick |
| `firmware missing` / `load_fw error=-2` | Generate that arch's blobs and run `New-GpuUnlockUsb.ps1` again |
| Menu is not `0.2.13 (<Arch>)` | The stick still has an older EFI. Run the USB script again |
| Keyboard dead in the menu | 0.2.3 and later connect USB before the menu. Rewrite the stick with a current EFI |
| RTX 3070 Ti stays black after inject | Wait about a minute. The measured boot did this and then reached Windows |
| No internal disk in the target list | The firmware did not publish a FAT EFI partition on NVMe or SATA, or the only loader there is this unsigned wrapper |
| `0xc0000428` after an ESP install | Copy `bootmgfw_ms.efi` back as in [If the machine will not start](#if-the-machine-will-not-start) |

---

## ESP install

This path replaces `\EFI\Microsoft\Boot\bootmgfw.efi` on the internal ESP and registers a startup task. Have WinPE or a Windows installation disc ready first. The USB stick is the path that leaves that partition alone.

A stock ESP is often about 100 MB. Turing's firmware fits beside the existing Windows boot files. Ampere and Ada each need `gsp_ga10x.bin`, about 80 MB, which does not fit a stock ESP next to `bootmgfw.efi`. Enlarging the ESP is your own step, with a partition tool you already use, such as MiniTool Partition Wizard. This README does not describe those tools. After the volume has room, the same install command works for Ampere and Ada.

On the target Windows machine, elevated PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Install-GpuUnlockBoot.ps1 -LoaderEfi C:\path\to\UnlockLoader-Turing.efi -FirmwareDir .\assets\firmware\Turing
```

The script refuses to run without elevation and refuses to replace a non-PE `bootmgfw.efi`. It copies the original to `EFI\turing-unlock\bootmgfw_ms.efi` and `EFI\Microsoft\Boot\bootmgfw_ms.efi`, writes `config.ini`, and installs the loader as `bootmgfw.efi`. It registers scheduled task `GpuUnlock Boot Guard`, which runs `tools\Protect-GpuUnlockBoot.ps1` from this directory about one minute after startup. Keep this tree on disk. Deleting it leaves the task pointing at a missing script. Move the tree only if you re-run Install from the new path.

Before Windows starts, the loader writes the signed `bootmgfw.efi` back, then starts it. The boot that just entered Windows has no loader menu. The task puts the wrapper back, so the next reboot shows the menu. There is no target list on this path. It starts Windows on this volume only.

| Parameter | Meaning |
| --- | --- |
| `-TimeoutSec 20` | menu timeout |
| `-DefaultAction skip` | `skip` or `inject` |
| `-AlsoFallback` | also replace `\EFI\Boot\bootx64.efi` when it matches the original bootmgfw |
| `-SkipScheduledTask` | do not register the guard task |
| `-FirmwareDir <dir>` | copy that arch's blobs onto the ESP |

Re-run Install after you replace the EFI, or the ESP still has the old loader.

While Windows still boots, elevated `.\tools\Uninstall-GpuUnlockBoot.ps1` restores `bootmgfw.efi` and removes the task. `-Purge` also deletes `EFI\turing-unlock\`, including firmware blobs.

`Copy-GpuUnlockLog.ps1` reads `unlock.log` from the internal ESP into `logs\`. A USB stick keeps its own log. This script does not read the stick.

A healthy ESP skip looks like:

```
Inject skipped. Continuing to boot.
restoring signed bootmgfw.efi from \EFI\Microsoft\Boot\bootmgfw_ms.efi (...)
restore ok: \EFI\Microsoft\Boot\bootmgfw.efi (...)
Starting EFI loader...
```

| Symptom | What to do |
| --- | --- |
| Firmware boots Windows with no menu | Expected once after the loader restores Microsoft's file. The next reboot should show the menu. If it still skips, turn Secure Boot off or run `Protect-GpuUnlockBoot.ps1 -RegisterTask` |
| Menu gone after one successful Windows boot | Wait about a minute (`C:\ProgramData\GpuUnlock\protect.log`), or register the task again |
| `0xc0000428` | The official path is still the unsigned loader. Use the copy in [If the machine will not start](#if-the-machine-will-not-start) |

---

## Build from source

Build on a Windows machine with Visual Studio. The machine does not have to be the one with the GPU.

| Item | Requirement |
| --- | --- |
| OS | Windows 10/11 x64 |
| Visual Studio 2022 | workload **Desktop development with C++**, MSVC v143 x64 |
| Python | 3.6+ (`py -3` or `python` on PATH). Firmware generate also needs `pip install zstandard` |
| Git | to clone EDK2 |
| Disk | a full EDK2 tree is several GB |

VS Build Tools is enough if `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` is installed. `vswhere` must find it.

Commands assume the root of this tree (the directory that contains `GpuUnlockPkg` and `tools`).

### Download the toolchain

The build script looks for `edksetup.bat` in `.\edk2`, `C:\edk2`, `C:\src\edk2`, `%USERPROFILE%\edk2`, `%USERPROFILE%\src\edk2`, or `EDK2_PATH` / `-Edk2Path`.

Use **edk2-stable202608** or later. The DSC links `CompilerIntrinsicsLib`, and submodules are required.

- `edk2-stable202408` does not contain that library. Its `subhook` submodule still points at `github.com/Zeex/subhook`, which is gone.
- `edk2-stable202411` has the library and `tianocore/edk2-subhook`, but `CompilerIntrinsicsLib/*_ms.c` typedefs `size_t` as 32-bit on x64 and fails with `C2371`.
- `edk2-stable202511` and later typedef `size_t` as `unsigned __int64`. That is the first stable tag that builds this package with VS x64.

`BaseTools\Bin\Win32\GenFw.exe` is not created by the clone. Build BaseTools once before this package: from an **x86 Native Tools** prompt, `edksetup.bat Rebuild`.

```powershell
git clone --recurse-submodules https://github.com/tianocore/edk2.git edk2
cd edk2
git checkout edk2-stable202608
git submodule update --init --recursive
cd ..
```

Shallow clone:

```powershell
git clone --depth 1 --branch edk2-stable202608 https://github.com/tianocore/edk2.git edk2
cd edk2
git submodule update --init --recursive
cd ..
```

EDK2 assembles `.nasm` files in MdePkg. Download the NASM **win64** build (2.15.05 or later; this tree expects 3.02):

- https://www.nasm.us/
- https://www.nasm.us/pub/nasm/releasebuilds/3.02/win64/nasm-3.02-win64.zip

Extract so `nasm-3.02\nasm.exe` exists. `PATH` or `NASM_PREFIX` / `-NasmPrefix` also work.

```powershell
git --version
py -3 --version
.\nasm-3.02\nasm.exe -v
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
```

### Prepare and build

`tools\Prepare-GpuUnlock.ps1` checks the host tools and this tree, links or copies the package into EDK2, and stages firmware that is already present. `-Arch` is `Turing` (default), `Ampere`, or `Ada`. One build contains one inject backend.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Prepare-GpuUnlock.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1 -Arch Turing
```

Prepare checks Windows x64, VS2022 C++ (17.x), Python 3.6+, NASM 2.15.05+, git, and the EDK2 tree (`edksetup.bat`, `MdePkg`, `MdeModulePkg`, `MdeLibs.dsc.inc`, `CompilerIntrinsicsLib`, BaseTools). It checks the common loader, all three `Arch\` trees, headers, tools, `assets`, `bench`, the selected INF, and matching version strings in `VERSION`, `TU_LOADER_VERSION`, `Loader.inf`, and `GpuUnlockPkg.dec`. `BASE_NAME` stays `TuringUnlockLoader`. Host copies are `UnlockLoader-<Arch>.efi`.

It creates `edk2\GpuUnlockPkg` as a junction to this tree's `GpuUnlockPkg` (a copy if `mklink /J` fails), removes a leftover `edk2\TuringUnlockPkg` junction, copies missing `Conf\*.txt` templates, and creates `.\build`.

```powershell
.\tools\Prepare-GpuUnlock.ps1 -FetchEdk2
.\tools\Prepare-GpuUnlock.ps1 -FetchNasm
```

Fix every `[fail]` line until it prints `Prepare complete.`

Build runs Prepare first (`-SkipPrepare` only after that succeeded in the same tree), then `edksetup.bat VS2022` and `build -a X64 -t VS2022 -b DEBUG -p GpuUnlockPkg\GpuUnlockPkg.dsc`. The EFI is copied to `.\build\UnlockLoader-<Arch>.efi`.

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ampere
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ada
.\tools\Build-GpuUnlockEfi.ps1 -Arch Turing -Edk2Path C:\edk2 -NasmPrefix .\nasm-3.02\ -Target RELEASE -ToolChain VS2022
```

Warnings about `iasl`, CLANG, and Cygwin can be ignored. A second build is incremental. The menu version must match `TU_LOADER_VERSION` in `GpuUnlockPkg\Include\TuConfig.h` (currently `0.2.13`).

Ada uses the AD102 Linux transaction (four-PLM V67, then FLR). Ampere uses the GA102 Linux transaction (V67, selectors, then FLR). Turing writes the selectors and does not FLR.

EFI shell smoke test without replacing bootmgr:

```
fs0:
cd EFI\turing-unlock
TuringUnlockLoader.efi noload
```

`noload` stays in the loader after the menu. `inject` / `skip` skip the menu.

Firmware generation for a local build is the same command as in [Get the EFI and the firmware](#get-the-efi-and-the-firmware). `tools\Build-GpuUnlockFirmware.ps1` runs `firmware\<arch>\fetch_artifacts.py`. URLs and SHA-256 are in `firmware\<arch>\artifacts-610.43.03.json`. The script accepts a download only when the hash matches. Nothing downloaded is executed.

| Input | Source | Used for |
| --- | --- | --- |
| `NVIDIA-kernel-module-source-610.43.03.tar.xz` | NVIDIA open kernel module source | Booter / GSP boot / SEC2 images |
| `nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst` | CachyOS archive of that package | `gsp_tu10x.bin` or `gsp_ga10x.bin`, plus `NVIDIA-LICENSE` |

The V67 payload is generated locally. It is not an NVIDIA redistributable. Cache: `.cache\firmware\` (gitignored). Do not commit `*.bin` or `NVIDIA-LICENSE`.

### Clear

Deletes host EFI copies and the EDK2 `Build\GpuUnlock*` trees. It does not uninstall an ESP install and does not delete the EDK2 or NASM clones.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Firmware
```

`-Firmware` also drops `assets\firmware\`. `-WhatIf` prints the paths.

### Hyper-V guest

Install, skip, and entering Windows were tested in a Generation 2 VM with no NVIDIA GPU. Real-GPU inject was not.

1. Create a Generation 2 VM and install Windows 10/11 x64.
2. Shut it down. Settings → Security → clear **Enable Secure Boot**.
3. Copy `build\UnlockLoader-Turing.efi` and `tools\` into the guest.
4. Run the ESP install from an elevated PowerShell in the guest.
5. Reboot. The menu should say `GPU Unlock Loader 0.2.13 (Turing)`. Enter or the timeout should start Windows.

With no GPU passed through, Inject prints `gpu_count=0` and does not write MMIO.

### Build troubleshooting

| Symptom | What to do |
| --- | --- |
| Prepare `[fail] NASM` or `[fail] EDK2` | `Prepare-GpuUnlock.ps1 -FetchNasm` and/or `-FetchEdk2` |
| Build fails on NASM | Extract `nasm-3.02\nasm.exe` or pass `-NasmPrefix` |
| `LNK2001 memset` | Rebuild from this tree. The DSC links `CompilerIntrinsicsLib` |
| `C2371` `size_t` in `memset_ms.c` / `memcpy_ms.c` | Use `edk2-stable202608`. The 202411 tag typedefs `size_t` as 32-bit on x64 |
| missing `GenFw.exe` | From an x86 Native Tools prompt: `edksetup.bat Rebuild` |
| `error 0004: File read failure` on `Loader.inf` | INF/DSC/DEC must be ASCII. EDK2 opens them with the system ANSI code page |
| `Cannot find BaseTools Bin Win32` during a successful build | Harmless on current tools. `EDK_TOOLS_BIN` is set before `edksetup.bat` |

### Layout

```
.                          this directory = repo root
  GpuUnlockPkg/
    Application/Loader/    menu, chainload, PCI scan
    Arch/Turing/           TU102 inject (-Arch Turing)
    Arch/Ampere/           GA102 inject (-Arch Ampere)
    Arch/Ada/              AD102 inject (-Arch Ada)
    Include/               shared headers
  tools/                   Prepare / Build EFI / Build firmware / Clear / Install / USB / Uninstall / Protect / Copy log / Hide ESP letter
  logs/                    ESP unlock.log copies (gitignored)
  firmware/                per-arch blob generators (turing, ampere, ada)
  assets/config.ini
  assets/firmware/<Arch>/  generated blobs (gitignored)
  bench/gemm_fp16.py
  bench/gemm_bf16.py
  build/                   UnlockLoader-<Arch>.efi (gitignored)
  nasm-3.02/               local download, not in git
  edk2/                    local download, not in git
  LICENSE                  GPL-2.0-only
  NOTICE                   attribution
  VERSION                  0.2.13
  README.md                English
  README_zh.md             Chinese
```

---

## License

Source in this tree is **GPL-2.0-only**, same as the three upstream Linux modules. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

NVIDIA firmware stays under NVIDIA's license and is not in git.

---

## Changelog

`0.2.13`: Ampere no longer force-injects IDs that only match the `0x2200-0x25ff` range (GA107 / laptop RTX 3050 and other unlisted parts are skipped with a log line). RTX 3050 GA106 (`2507`/`2508`) is skipped too. RTX 4050 (AD107) stays rejected by Ada admission. Turing range force-inject unchanged. README adds a 20/30/40-series support table built from the code ID lists.

`0.2.12` follows the TOTORONG AD103 Windows unlocker handoff: preload (LoadImage) Windows Boot Manager before disconnect/unlock when possible; keep pre-FLR `driver_release`; after FLR, `ConnectController` reconnects GOP, re-check selectors (`post-GOP-reconnect`), then StartImage the preloaded image (falls back to the previous LoadImage path if preload missed or StartImage fails). Ada/Ampere success paths both reconnect. Unlock payload unchanged.

Docs (same tree): RTX 4080 MMA ~2× issue-rate validation on 0.2.12 inject (`10de:2704`, `skip_flr=0`); Experimental section documents Fast Startup / `HiberbootEnabled` registry, volatile unlock, and `mma_bench_sm89` as the Ada unlock check.

`0.2.11` pre-FLR console/GOP / display-child disconnect (Latence-style `driver_release`) before Ada/Ampere PCIe FLR, plus optional `skip_flr=1` to isolate primary-GPU FLR reboot. Disconnect covers TextOut, GraphicsOutput under the GPU, every PciIo BY_CHILD (gopless display children; upstream AD103 Windows #39 / #92), then the GPU PCI handle. Working #92 Windows 2705 boots blank ~1s at FLR/GOP reconnect — not a power-cycle; Fast Startup should be off. Ada unlock transaction otherwise unchanged from 0.2.10.

`0.2.10` hardens UEFI PCIe FLR / config restore toward Linux `gpu-unlock-ada` (`unlock_610` / `gpu_unlock_command_restore`): after Initiate FLR, selective BAR restore, COMMAND restore with MEMORY on / BME off, BAR + identity checks, and DevSta TRPND clear. Skips a blind 256-byte config blast when identity mismatches. Ada unlock transaction otherwise unchanged.

`0.2.9` PCI scan writes each GPU's BDF into `unlock.log` on skip and inject (`Found <seg:bus:dev.fn>  10de:<id>`). Pre-0.2.9 skip logs only had the device id. README documents RTX 4080 experimental ops: flag + exact `targets` BDF (`targets=all` never admits 4080).

`0.2.8` adds Ada admission for experimental RTX 4080 (`10de:2704`) and RTX 4080 SUPER (`10de:2702`). Both need a config.ini flag (`rtx4080_experimental` / `ad103_experimental`) and their exact PCI BDF in `targets` (never via `targets=all`). Existing experimental Ada IDs (`2685`, `2705`, `2805`) use the same style of gates. Unlock routing uses an explicit `ad102_family()` map onto the pinned AD102 assets; no per-ID fuse/SM/VBIOS tables. Readout-only Ada IDs are no longer force-injected.

`0.2.7` chooses inject, skip, and a USB boot target with the up and down keys. Diagnostic lines go to `unlock.log`. The screen keeps the menu, the result, and errors. `verbose=0` is the default. `tools\Hide-GpuUnlockEsp.ps1` removes a drive letter from the internal EFI system partition.

`0.2.6` keeps the ESP path: a signed `bootmgfw` backup on the loader volume still restores that file and starts Windows from that volume only. USB mode (the `usb-boot` marker, and no signed backup) connects NVMe and SATA controllers, then lists EFI loaders on other FAT volumes and waits for a number. The list can contain Windows Boot Manager, a distro `shimx64.efi` or `grubx64.efi`, and `systemd-bootx64.efi`. `\EFI\BOOT\bootx64.efi` is included only when that volume has none of those. Inject, skip, and emergency all reach this list. The selected file is started with that volume's own device path. The scan does not write `bootmgfw.efi`. `New-GpuUnlockUsb.ps1` copies onto a FAT32 volume and does not format any disk. The USB stick is the recommended path for every architecture. The ESP install remains available.

`0.2.5` adds a USB boot path that leaves the internal ESP alone, and corrects the Ampere SEC2 completion check. Before chainload the loader logs `boot mode=esp` when that volume holds a signed `bootmgfw` backup, `boot mode=usb` when `\EFI\turing-unlock\usb-boot` is present, and `boot mode=unmarked` otherwise.

Ampere follows the Linux GA10x transaction: SEC2 success is feature PLM opening to `0xffffffff`, not a later halt. On loader 0.2.4 a GA104 RTX 3070 Ti (`10de:2482`) opened PLM with CPUCTL still `0`. The extra 5-second halt poll returned `-110`, SS0/SS1 stayed at stock, and the error-path FLR let Windows start with an unchanged GEMM. Ada still waits for halt, then checks the four AD102 permission words. WPR_CFG `0x1fa7cc` must be `0xfffff0ff`, and WPR `0x1fa7c4` plus feature PLM must be `0xffffffff`, before the selectors are written. FBPA `0x9a0148` is logged only. The same four words are sampled before firmware and again after FLR. The V67 DMA copy is compared with the file immediately before SEC2 starts. A failed GPU is logged as `unlock 10de:<id> result=<errno>`, and `inject finished status=` is no longer `0` when that happens. Chainload still continues. The 3070 Ti retest used that transaction. Its log banner still says `version=0.2.4` (the EFI was built before the 0.2.5 version string).

`0.2.4` answers the build notes in [#1](https://github.com/mirabarukaso/nv_unlock_win/issues/1): the EDK2 pin is `edk2-stable202608`, `assets\config.ini` is in the tree, and the loader writes a `handoff` line to `unlock.log` before the next loader starts.
