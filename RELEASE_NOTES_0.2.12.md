# Release notes — loader 0.2.12

Unsigned EFI loaders for Turing, Ampere, and Ada. Secure Boot must be off.

## Highlights

- **Ada RTX 4080 Windows inject verified** (`10de:2704`, friend-run): loader prints `AD102 FEAT UNLOCKED`; `mma_bench_sm89` shows about **2×** on `f16/f32`, `bf16/f32`, and `fp8/e4m3` (control modes flat). Needs `rtx4080_experimental=1` and an **exact** PCI BDF in `targets` (not `all`). Details and tables: [docs/RTX40_Ada.md](docs/RTX40_Ada.md).
- **Windows handoff** follows the public path from [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker) (code GPL-2.0-only; new narrative docs CC BY-NC 4.0): preload Windows Boot Manager when possible, pre-FLR disconnect, post-FLR GOP reconnect, then StartImage. Unlock payload unchanged.
- **`skip_flr=1`** remains available to isolate primary-GPU FLR reboot while still disconnecting/reconnecting.
- Turn **Fast Startup** off between inject tests (`HiberbootEnabled=0`; a missing key often means Fast Startup is on). Unlock is **session-only**.
- **RTX 4090** (`10de:2684`) config support unchanged; published Windows GEMM numbers are **withdrawn** and **need re-verification** (unlocked 4080 0.2.12 MMA already exceeds the old published 4090 figures).
- Stock Ada experimental flags default to `0` and do not affect Turing/Ampere loaders; `skip_flr=0` is the normal FLR path. Do not leave a 4080-only BDF in `targets` on a stick shared with 20/30-series or stock 4090 — use `targets=all` (or that card's BDF) for those.

## Artifacts

Rebuild all three host EFIs (gitignored under `build/`):

- `UnlockLoader-Turing.efi`
- `UnlockLoader-Ampere.efi`
- `UnlockLoader-Ada.efi`

Generate matching firmware with `tools\Build-GpuUnlockFirmware.ps1 -Arch <Arch>` (NVIDIA blobs are not in git).

## Docs

- Short Ada summary in [README.md](README.md) / [README_zh.md](README_zh.md)
- Per-card Ada flags, registry, MMA: [docs/RTX40_Ada.md](docs/RTX40_Ada.md) / [docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md)
