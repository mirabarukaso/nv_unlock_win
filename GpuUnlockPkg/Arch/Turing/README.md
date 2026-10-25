# Turing backend (TU102 / TU104)

Port of the Linux `turing-unlock` kernel transaction into this UEFI loader.
Compile it alone:

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Turing
```

- `TuringIds.c` — NVIDIA device IDs treated as Turing
- `Unlock.c` — SEC2 Booter + V67 LS overflow, SS0/SS1 writes, no FLR on success
- `Portable/` — booter parse, layout, GFW eval, VBIOS helpers ported from the Linux module
- Headers next to the sources so Ampere/Ada can use the same names later without colliding on the common include path

Upstream: https://github.com/nlzy/turing-unlock

Host TITAN RTX (`10de:1e02`) force-inject reached full SS (`SS0=88888888`,
`SS1=8`). Windows GEMM stayed within ~1% of stock. Cards that need unlocking
(2080 Ti class) are not validated here.
