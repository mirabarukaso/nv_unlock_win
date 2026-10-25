# Firmware blobs (UEFI loader)

NVIDIA firmware is **not** in git. Each architecture needs its own pinned
610.43.03 set. Turing blobs do **not** unlock Ampere or Ada.

| Arch | When to generate | Output |
| --- | --- | --- |
| Turing | TU102 inject (`UnlockLoader-Turing.efi`) | `assets/firmware/Turing/` |
| Ampere | GA102 inject (`UnlockLoader-Ampere.efi`) | `assets/firmware/Ampere/` |
| Ada | AD102 inject (`UnlockLoader-Ada.efi`) | `assets/firmware/Ada/` |

This machine's TITAN RTX is TU102. Generate **Turing** only for that card.
Ampere and Ada scripts live here so a future standalone `uefi/` repo can
build those blobs without the parent Linux trees.

## Generate (from the `uefi/` directory)

Windows (cmd or PowerShell). Cache is `uefi/.cache/firmware/` (gitignored).

```powershell
python -m pip install zstandard
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Ampere
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Ada
```

`-Offline` reuses already-fetched inputs. `-CacheDir` / `-OutputDir` override
paths. The wrapper fetches HTTPS artifacts, checks SHA-256, extracts the
NVIDIA source with Python `tarfile` (no `xz.exe`), and reads `.pkg.tar.zst`
with `zstd` or the `zstandard` package.

Install the matching arch folder onto the ESP:

```powershell
.\tools\Install-GpuUnlockBoot.ps1 -LoaderEfi .\build\UnlockLoader-Turing.efi -FirmwareDir .\assets\firmware\Turing
```

If `-FirmwareDir` is omitted, Install uses `assets\firmware\<Arch>\` when the
loader filename is `UnlockLoader-<Arch>.efi`.

## Sources

Scripts under `turing/`, `ampere/`, and `ada/` are the upstream firmware
generators, plus a Windows `.tar.zst` helper `zstio.py`:

- Turing: https://github.com/nlzy/turing-unlock
- Ampere: https://github.com/Zagitta/gpu-unlock
- Ada: https://github.com/JustAResearcher/gpu-unlock-ada

See each `NOTICE.md`. NVIDIA blobs stay under NVIDIA's license.
