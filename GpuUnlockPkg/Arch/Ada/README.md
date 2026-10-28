# Ada backend (AD102-family)

Port of the Linux `gpu-unlock-ada` kernel transaction (`kernel/gpu_unlock`) into
this UEFI loader. Compile it alone:

```powershell
.\tools\Prepare-GpuUnlock.ps1 -Arch Ada
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ada
```

- `Ada.c` — NVIDIA device IDs treated as Ada
- `Unlock.c` — FWSEC-FRTS from ROM, AD102 Booter + four-PLM V67, SS0/SS1 writes, FLR on success
- `Portable/` — booter geometry, layout, GFW eval, VBIOS helpers from the Ada Linux module

## Device IDs

| ID | Card | Status |
| --- | --- | --- |
| `10de:2684` | RTX 4090 | UEFI inject wrote SS, Windows started, FP16 GEMM at 8192 rose 135.9 → 156.3 |
| `10de:2685` | RTX 4090 D | experimental, same AD102 assets |
| `10de:2705` | RTX 4070 Ti SUPER (AD103) | experimental canary |
| `10de:2805` | RTX 4060 Ti 16GB (AD106) | experimental, untested on silicon |

The one-board Latence.app AD104 MSI path (`10de:2782`) is not imported.

## Firmware (ESP `EFI\turing-unlock\firmware`)

- `booter_load_ad102.bin`
- `v67-ad102-610.43.03-four-plm.bin`
- `gsp_ga10x.bin` (shared GSP; Ada signature section `.fwsignature_ad10x`)
- `gsp_boot_ad102.bin`
- `gsp_boot_desc_ad102.bin`

Ada success **does** FLR so the stock Windows driver starts from a clean halted GSP.
SEC2 must halt before the stock signature or the selectors are written. The
four permission words are sampled before firmware, after that halt, and after
FLR. WPR_CFG `0x1fa7cc` must be `0xfffff0ff`, and WPR `0x1fa7c4` plus feature
PLM must be `0xffffffff`, before the selectors are written. FBPA `0x9a0148` is
logged only. The V67 DMA copy is compared with the file immediately before
SEC2 starts.
Feature registers are Ampere-style (`0x8238xx`), not Turing `0x40965x`.

Upstream: https://github.com/JustAResearcher/gpu-unlock-ada

This UEFI tree has not run Ada inject on silicon. Compile and VM skip-boot
are implemented.
