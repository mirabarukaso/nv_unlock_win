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
| `10de:2684` | RTX 4090 | base; UEFI inject wrote SS, Windows started, FP16 GEMM at 8192 rose 135.9 to 156.3 |
| `10de:2685` | RTX 4090 D | experimental (`rtx4090d_experimental`); same AD102 assets |
| `10de:2702` | RTX 4080 SUPER | experimental (`ad103_experimental` + exact `targets` BDF); AD102-family assets |
| `10de:2704` | RTX 4080 | experimental (`rtx4080_experimental` + exact `targets` BDF); AD102-family assets |
| `10de:2705` | RTX 4070 Ti SUPER (AD103) | experimental (`ad103_experimental` + exact BDF) canary |
| `10de:2805` | RTX 4060 Ti 16GB (AD106) | experimental (`ad106_experimental` + exact BDF), untested on silicon |

The one-board Latence.app AD104 MSI path (`10de:2782`) is not imported.

## Firmware (ESP `EFI\turing-unlock\firmware`)

- `booter_load_ad102.bin`
- `v67-ad102-610.43.03-four-plm.bin`
- `gsp_ga10x.bin` (shared GSP; Ada signature section `.fwsignature_ad10x`)
- `gsp_boot_ad102.bin`
- `gsp_boot_desc_ad102.bin`

Ada success **does** FLR by default so the stock Windows driver starts from a
clean halted GSP. Inject handoff (0.2.12+, TOTORONG AD103 Windows unlocker):
preload Windows Boot Manager (`LoadImage`) when possible; Latence-style
`driver_release` (SimpleTextOut / GraphicsOutput under the GPU, every PciIo
`BY_CHILD` display child including gopless children; upstream #39 / #92), then
the GPU PCI handle; Initiate FLR; `ConnectController` reconnect GOP; observe
selectors (`post-GOP-reconnect`); `StartImage` the preloaded image. A brief
console blank (~1s) around FLR/GOP reconnect is expected on working Windows
ports — that is not a machine power-cycle. Set `skip_flr=1` in `config.ini` to
skip Initiate FLR after selectors (still disconnects, still reconnects) when
isolating a primary-GPU reboot; unlock state is session-volatile either way.

SEC2 must halt before the stock signature or the selectors are written. The
four permission words are sampled before firmware, after that halt, and after
FLR. WPR_CFG `0x1fa7cc` must be `0xfffff0ff`, and WPR `0x1fa7c4` plus feature
PLM must be `0xffffffff`, before the selectors are written. FBPA `0x9a0148` is
logged only (`0xffffff0f` after unlock is expected / non-fatal on 2705). The
V67 DMA copy is compared with the file immediately before SEC2 starts.
Feature registers are Ampere-style (`0x8238xx`), not Turing `0x40965x`.

## RTX 4080 ops (experimental)

`10de:2704` needs `rtx4080_experimental=1` and an exact BDF in `targets` (not `all`). Same AD102-family assets as 4090; no per-ID fuse table.

```
rtx4080_experimental=1
targets=0000:01:00.0
```

Get the BDF from the loader menu (`seg:bus:dev.fn` on screen), from the `Found ...` line in the loader log on **0.2.9+** (`unlock-YYYYMMDD-HHMMSS.log` since 0.2.14, previously `unlock.log`; skip is enough; older skip logs only had `10de:2704`), or from Windows `nvidia-smi --query-gpu=pci.bus_id,name --format=csv` / Device Manager location.

On Windows, turn **Fast Startup** off (full shutdown between inject tests).
TOTORONG's public AD103/`2704` Windows unlocker and upstream #92 (`2705`)
both used preload → disconnect → FLR → reconnect GOP → StartImage; console
blank ~1s at reconnect is expected — not a board reboot. Prefer default
`skip_flr=0` with 0.2.12 handoff; set `skip_flr=1` only to isolate FLR.
Confirm unlock with register-resident `mma_bench`
(`f16acc` vs `f32acc`), not a diffusion app FPS.

Upstream: https://github.com/JustAResearcher/gpu-unlock-ada

This UEFI tree has not run Ada inject on silicon. Compile and VM skip-boot
are implemented.
