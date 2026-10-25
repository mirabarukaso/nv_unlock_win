# Ampere backend (GA102-family)

Port of the Linux `gpu-unlock` kernel transaction (`kernel/gpu_unlock`) into
this UEFI loader. Compile it alone:

```powershell
.\tools\Prepare-GpuUnlock.ps1 -Arch Ampere
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ampere
```

- `Ampere.c` — NVIDIA device IDs treated as Ampere
- `Unlock.c` — FWSEC-FRTS from ROM, GA102 Booter + V67, SS0/SS1 writes, FLR on success
- `Portable/` — booter geometry, layout, GFW eval, VBIOS helpers from the Ampere Linux module

## Device IDs

Same list as `F:\gpu-unlock\kernel\gpu_unlock\main.c` (GeForce GPU functions only; no audio, no professional GL).

| Chip | Example | Status |
| --- | --- | --- |
| GA102 | RTX 3090 Ti `10de:2203`, 3090 `2204`, 3080 10GB `2206` | reported unlocked on Linux |
| GA102 | RTX 3080 Ti 20GB OEM `10de:2205` | unlock completes, BF16 stays half-rate |
| GA104 | RTX 3070 `10de:2484`, 3070 Ti `2482` | 3070 reported; 3070 Ti from a Windows UEFI port |
| GA103 | `10de:2414` | untested |
| GA106 | RTX 3060 12GB `10de:2504` | validated on that ID |

## Firmware (ESP `EFI\turing-unlock\firmware`)

- `booter_load_ga102.bin` (60416-byte image)
- `v67-ga102-610.43.03.bin`
- `gsp_ga10x.bin` (shared GSP; Ampere signature section `.fwsignature_ga10x`)
- `gsp_boot_ga102.bin` (24576 bytes)
- `gsp_boot_desc_ga102.bin`

Ampere success **does** FLR so the stock Windows driver starts from a clean halted GSP.
Feature registers are Ampere-style (`0x8238xx`), not Turing `0x40965x`.
The UEFI path does not re-unlock after S3; that is a Linux-kmod concern.

Upstream: https://github.com/Zagitta/gpu-unlock

Linux reports unlock on several GA102/GA104/GA106 IDs. This UEFI tree has not
run Ampere inject on silicon. Compile and VM skip-boot are implemented.
