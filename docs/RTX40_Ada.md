# Ada / RTX 40-series (Windows)

Unlock for Ada GPUs reuses the **AD102-family** firmware payload (same blobs as stock RTX 4090). There is no separate fuse/SM/VBIOS table per SKU. Which PCI IDs the loader will unlock is controlled by `config.ini` flags and `targets`.

Back to the main [README](../README.md). Verified 4080 MMA numbers and ops notes are in this document (friend-run / posted on behalf of a tester).

## Verified summary (loader 0.2.12)

| GPU | PCI ID | Result |
| --- | --- | --- |
| RTX 4090 | `10de:2684` | Base Ada path. Windows inject OK. **Perf needs re-verification** — published Windows GEMM numbers **withdrawn** (unlocked 4080 0.2.12 MMA already exceeds the old 4090 figures). |
| RTX 4080 | `10de:2704` | Experimental path. Windows inject OK with `rtx4080_experimental=1` and exact BDF. `mma_bench_sm89`: **~2×** on `f16/f32`, `bf16/f32`, `fp8/e4m3`; control modes flat. |

Unlock is **session-only**. Reboot or power-off clears it.

## PCI IDs in this tree (from `GpuUnlockPkg/Arch/Ada`)

| Card / note | Device ID | Config flag | `targets` rule | Code status |
| --- | --- | --- | --- | --- |
| **RTX 4090** | `10de:2684` | *(none — base Ada ID)* | `all` or listed BDF | Supported / inject OK; **perf needs re-verification** |
| **RTX 4090 D** | `10de:2685` | `rtx4090d_experimental=1` | `all` or listed BDF | Experimental opt-in; **not** measured in this tree |
| **RTX 4080** | `10de:2704` | `rtx4080_experimental=1` | **exact BDF required** (`all` never admits) | Experimental; **verified** 0.2.12 MMA |
| **RTX 4080 SUPER** | `10de:2702` | `ad103_experimental=1` | **exact BDF required** | Experimental; not measured here |
| **AD103 canary** (4070 Ti SUPER ID in headers) | `10de:2705` | `ad103_experimental=1` | **exact BDF required** | Experimental canary; not measured here |
| **AD106** (4060 Ti 16GB ID in headers) | `10de:2805` | `ad106_experimental=1` | **exact BDF required** | Experimental; **untested on silicon** per source comments |

Stock **RTX 4090** (`2684`) is the only Ada **base** ID: no experimental flag. Everything else in the table needs its flag. Exact IDs (`2704` / `2702` / `2705` / `2805`) also need their PCI address in `targets`; `targets=all` will not unlock them even with the flag on. **4090 D** is non-exact experimental: flag on and `targets=all` (or a listed BDF) is enough.

- **Shared stick / other gens:** the Ada experimental flags default to `0` and are ignored by Turing/Ampere loaders; `skip_flr=0` is the normal path (FLR as usual). A specific BDF in `targets` is only required for exact experimental IDs (4080/4080 SUPER etc.). For Turing, Ampere, and stock 4090 use `targets=all` (or that card's own BDF) — don't leave a 4080 BDF on a stick used for other cards.

Other Ada retail IDs (for example most 4070 / 4060 SKUs beyond the rows above) are **not** in the admit list. Do not invent support.

## Example `config.ini` snippets

**RTX 4090** (base — default stick is already enough if `targets=all`; **perf needs re-verification**):

```
targets=all
# no experimental flags needed for 10de:2684
skip_flr=0
```

**RTX 4090 D**:

```
rtx4090d_experimental=1
targets=all
skip_flr=0
```

**RTX 4080** (replace BDF):

```
rtx4080_experimental=1
targets=0000:01:00.0
skip_flr=0
```

**RTX 4080 SUPER / AD103 canary `2705`**:

```
ad103_experimental=1
targets=0000:01:00.0
skip_flr=0
```

**AD106 `2805`**:

```
ad106_experimental=1
targets=0000:01:00.0
skip_flr=0
```

## Ops notes (4080 / primary display GPU)

Focus: primary-GPU ops validated on RTX 4080. Full ID/flag table is above.

`10de:2704` (RTX 4080) and `10de:2702` (RTX 4080 SUPER) are experimental Ada IDs. They reuse the AD102-family payload; there is no separate fuse/SM/VBIOS table. Admission needs both a config flag and an **exact** PCI BDF in `targets`. `targets=all` never admits them, even when the flag is on.

Example `config.ini` on the USB stick:

```
rtx4080_experimental=1
targets=0000:01:00.0
skip_flr=0
```

Replace `0000:01:00.0` with the BDF of your card. For 4080 SUPER use `ad103_experimental=1` instead of `rtx4080_experimental`.

- Unlock is **session-only**. A reboot or power cycle clears it; choose Inject again on the next boot.
- On Inject (0.2.12+), the loader preloads `bootmgfw` when it can, then Ada/Ampere disconnect TextOut/GOP/display children, Initiate FLR (when `skip_flr=0`), reconnect GOP, re-check selectors, and StartImage the preloaded image. On a working Windows path expect a brief console blank (~1s), then Windows — not a second full POST (see upstream issue #92). If the board still power-cycles, set `skip_flr=1` to skip Initiate FLR while still disconnecting, to isolate reboot vs FLR.
- **Turn Windows Fast Startup off** between inject tests. Fast Startup is a hybrid shutdown; unlock state from the previous inject can look wrong across a soft "reboot".
  - UI: Settings → System → Power & battery → Additional power settings → Choose what the power buttons do → (Change settings that are currently unavailable) → uncheck **Turn on fast startup**.
  - Registry: `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power\HiberbootEnabled` (DWORD). `0` = Fast Startup off, `1` = on. If the value is **missing**, typical consumer Windows treats Fast Startup as **on**. Confirm with:

```
reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power" /v HiberbootEnabled
```

    To force off (admin):

```
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power" /v HiberbootEnabled /t REG_DWORD /d 0 /f
```

    Prefer the UI when it is available; use the registry to verify.
- Validate unlock with the Ada issue-rate probe (`mma_bench_sm89`), not cuBLAS FP16/BF16 GEMM alone. Large GEMM often sits on the power limit and can move little or even go slightly down after inject (a 0.2.8 Skip vs Inject GEMM compare on this SKU was power-wall noise even when the loader already printed `AD102 FEAT UNLOCKED`). Verified 0.2.12 MMA numbers are under [MMA results (RTX 4080, 0.2.12)](#mma-results-rtx-4080-0212).

**How to get the BDF without Inject**

1. **Loader menu (screen).** The GPU list shows `seg:bus:dev.fn` next to `10de:2704`, for example `0000:01:00.0`. The menu redraw is not copied into `unlock.log`.
2. **Skip `unlock.log` (0.2.9+).** PCI scan writes a line such as `Found 0000:01:00.0  10de:2704  Ada` (and a matching `turing_unlock: NVIDIA PCI 0000:01:00.0 10de:2704 ...` note). Skip-only logs from **0.2.8 and earlier** had `NVIDIA PCI 10de:2704` with **no** BDF — use the menu or Windows methods below for those files.
3. **Windows after a normal boot** (device id `2704`):
   - `nvidia-smi --query-gpu=pci.bus_id,name --format=csv` — bus id looks like `00000000:01:00.0`; drop the extra zeros in the segment to get `targets=0000:01:00.0`.
   - Device Manager → Display adapters → your GPU → Details → **Location information** (PCI bus / device / function). Segment is usually `0000`.

Then rewrite the stick (or edit `EFI\turing-unlock\config.ini`), set the two lines above, reboot with Secure Boot off, and choose Inject.

### Credits (Ada Windows handoff)

Loader **0.2.12** Ada/Ampere Windows handoff (preload Windows Boot Manager before disconnect/unlock; pre-FLR `driver_release` / TextOut+GOP+display-child disconnect; post-FLR GOP `ConnectController` reconnect, then StartImage) follows the public Windows path described by [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker). That project licenses **code under GPL-2.0-only** (see its `LICENSE`) and marks **new narrative documents as CC BY-NC 4.0** (see its `LICENSE-POLICY.md`). This tree does not vendor that EFI or its firmware blobs; unlock payloads here remain the existing AD102-family assets. Attribution only — not a claim that every 4080/VBIOS combination is covered.


## MMA results (RTX 4080, 0.2.12)

**RTX 4080** (`10de:2704`, AD103 / AD102-family path), 2026-10-29, Windows 11, driver 617.42, power limit 320 W, 76 SMs, sm_89. Loader **0.2.12** Inject: `AD102 FEAT UNLOCKED`, `skip_flr=0`, `targets=0000:01:00.0`, handoff `PLM=ffffffff SS0=88888888 SS1=8`. Probe: `mma_bench_sm89` (`iters=20000` `reps=3`), not cuBLAS GEMM.

Stock / previous session (no lasting unlock):

```
# NVIDIA GeForce RTX 4080, 76 SMs, sm_89, iters=20000 reps=3
mode       best TF/s   all runs
f16/f16        195.5   178.3/180.2/195.5
f16/f32         98.4   98.1/97.7/98.4
bf16/f32        97.8   97.5/97.5/97.8
s8/s32         393.8   387.7/389.0/393.8
fp8/e4m3       195.8   195.8/194.8/192.5
dp4a            98.6   98.6/98.1/95.2
ffma            47.4   47.4/45.4/47.4
```

0.2.12 inject (`mma_bench_sm89`):

```
mode       best TF/s   all runs
f16/f16        200.1   184.3/185.1/200.1
f16/f32        200.0   199.1/199.5/200.0
bf16/f32       199.8   199.3/198.4/199.8
s8/s32         398.1   398.1/397.9/395.6
fp8/e4m3       403.0   398.6/403.0/400.9
dp4a           100.5   100.5/90.3/100.0
ffma            48.3   48.3/48.1/47.0
```

| mode | stock best | inject best | ratio |
| ---: | ---: | ---: | ---: |
| f16/f16 | 195.5 | 200.1 | ~1.02× (control) |
| f16/f32 | 98.4 | 200.0 | **~2.03×** |
| bf16/f32 | 97.8 | 199.8 | **~2.04×** |
| s8/s32 | 393.8 | 398.1 | ~1.01× (control) |
| fp8/e4m3 | 195.8 | 403.0 | **~2.06×** |
| dp4a | 98.6 | 100.5 | ~1.02× (control) |
| ffma | 47.4 | 48.3 | ~1.02× (control) |

Unlock looks real when `f16/f32`, `bf16/f32`, and `fp8/e4m3` are about 2× while `f16/f16` / `s8/s32` / `dp4a` / `ffma` stay roughly flat. Do **not** treat cuBLAS FP16/BF16 GEMM alone as unlock proof on this SKU — a 0.2.8 Skip vs Inject GEMM compare on this card was power-wall noise even though the loader already printed `AD102 FEAT UNLOCKED`.

On Ada, issue-rate probes and large cuBLAS GEMM can disagree a lot because GEMM often sits on the power limit. Prefer `mma_bench_sm89` for unlock proof. Any older published **RTX 4090** Windows GEMM deltas are withdrawn (**needs re-verification**); unlocked 4080 0.2.12 MMA already exceeds those figures.

Inject on a GeForce 2080 Ti is not validated here. Full-speed tensor cores draw a lot of power and can damage hardware.

Validate unlock with `mma_bench_sm89`, not cuBLAS FP16/BF16 GEMM alone. Large GEMM often sits on the power limit. The tables above are the publishable record for the 0.2.12 friend-run (posted on behalf of a tester).
