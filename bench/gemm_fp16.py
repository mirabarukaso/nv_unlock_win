#!/usr/bin/env python3
"""FP16 GEMM benchmark matching turing-unlock's cuBLAS FP16 / FP32-accumulate table.

Uses PyTorch's cuBLAS path (fp16 inputs, tensor cores, fp32 accumulate internally).
Square sizes default to 1024, 2048, 4096, 8192 — the same M=N=K as the Linux README.

  python bench/gemm_fp16.py
  python bench/gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80

Status
------
The UEFI loader itself is validated on a Hyper-V Gen2 VM (Secure Boot off).
That guest has no Turing GPU, so inject is a no-op.

On the host, NVIDIA TITAN RTX (full 72-SM TU102) already reaches and exceeds
the unlocked RTX 2080 Ti peaks in the Linux README table, stock. After a
successful UEFI inject (SS0=88888888, SS1=8) GEMM stayed within ~1% of stock.
Do not expect the ~1.5x seen on 2080 Ti.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time

try:
    import torch
except ImportError:
    sys.exit("Need PyTorch with CUDA: pip install torch")


# RTX 2080 Ti whitepaper dense FP16 (the 100% peak in this project's README).
PEAK_2080TI_TFLOPS = 56.9

README_STOCK = {
    1024: 50.3,
    2048: 55.7,
    4096: 53.7,
    8192: 52.8,
}
README_UNLOCKED = {
    1024: 68.3,
    2048: 91.4,
    4096: 81.7,
    8192: 77.9,
}


def parse_sizes(text: str) -> list[int]:
    sizes = [int(p.strip()) for p in text.split(",") if p.strip()]
    if not sizes:
        raise argparse.ArgumentTypeError("need at least one size")
    for n in sizes:
        if n < 8 or n % 8:
            raise argparse.ArgumentTypeError(f"{n} must be a multiple of 8 (tensor-core alignment)")
    return sizes


def nvquery(fields: str) -> list[str]:
    exe = shutil.which("nvidia-smi")
    if not exe:
        return []
    try:
        raw = subprocess.check_output(
            [exe, f"--query-gpu={fields}", "--format=csv,noheader,nounits", "-i", "0"],
            encoding="utf-8",
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    return [p.strip() for p in raw.strip().split(",")]


def gpu_banner(device: int) -> None:
    props = torch.cuda.get_device_properties(device)
    extra = nvquery("driver_version,power.draw,clocks.sm,clocks.mem,temperature.gpu")
    print(f"GPU      {props.name}")
    print(f"CC       {props.major}.{props.minor}   VRAM {props.total_memory / (1024 ** 3):.1f} GiB")
    print(f"PyTorch  {torch.__version__}   CUDA {torch.version.cuda}")
    if extra and len(extra) >= 4:
        print(
            f"Driver   {extra[0]}   power {extra[1]} W   "
            f"SM {extra[2]} MHz   MEM {extra[3]} MHz   temp {extra[4]} C"
        )
    print(f"Peak ref {PEAK_2080TI_TFLOPS} TFLOPS  (2080 Ti whitepaper, this project's 100%)")
    print()


def gemm_tflops(m: int, n: int, k: int, ms: float) -> float:
    return (2.0 * m * n * k) / (ms * 1e-3) / 1e12


def bench_one(
    m: int,
    n: int,
    k: int,
    device: torch.device,
    warmup: int,
    repeat: int,
    min_ms: float,
) -> tuple[float, float, int]:
    a = torch.randn(m, k, device=device, dtype=torch.float16)
    b = torch.randn(k, n, device=device, dtype=torch.float16)
    c = torch.empty(m, n, device=device, dtype=torch.float16)

    for _ in range(warmup):
        torch.mm(a, b, out=c)
    torch.cuda.synchronize()

    starter = torch.cuda.Event(enable_timing=True)
    ender = torch.cuda.Event(enable_timing=True)
    times: list[float] = []
    iters = 0
    t0 = time.perf_counter()
    while iters < repeat or (time.perf_counter() - t0) * 1000 < min_ms:
        starter.record()
        torch.mm(a, b, out=c)
        ender.record()
        torch.cuda.synchronize()
        times.append(starter.elapsed_time(ender))
        iters += 1

    times.sort()
    median = times[len(times) // 2]
    best = times[0]
    del a, b, c
    torch.cuda.empty_cache()
    return median, best, iters


def main() -> int:
    parser = argparse.ArgumentParser(description="FP16 tensor-core GEMM TFLOPS (cuBLAS via PyTorch)")
    parser.add_argument("--sizes", type=parse_sizes, default="1024,2048,4096,8192")
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--repeat", type=int, default=50)
    parser.add_argument("--min-ms", type=float, default=400.0, help="keep iterating each size at least this long")
    args = parser.parse_args()

    if not torch.cuda.is_available():
        print("CUDA is not available.", file=sys.stderr)
        return 1

    torch.cuda.set_device(args.device)
    device = torch.device("cuda", args.device)
    matmul = torch.backends.cuda.matmul
    if hasattr(matmul, "fp32_precision"):
        matmul.fp32_precision = "ieee"
    else:
        matmul.allow_tf32 = False
        torch.backends.cudnn.allow_tf32 = False
    if hasattr(matmul, "allow_fp16_reduced_precision_reduction"):
        matmul.allow_fp16_reduced_precision_reduction = False

    # Pull SM clocks off idle before the first small size.
    a = torch.randn(4096, 4096, device=device, dtype=torch.float16)
    b = torch.randn(4096, 4096, device=device, dtype=torch.float16)
    c = torch.empty(4096, 4096, device=device, dtype=torch.float16)
    t_boost = time.perf_counter()
    while time.perf_counter() - t_boost < 1.5:
        torch.mm(a, b, out=c)
    torch.cuda.synchronize()
    del a, b, c

    gpu_banner(args.device)
    print("dtype    A,B,C = fp16   (tensor cores, fp32 accumulate inside cuBLAS)")
    print("sizes    M=N=K")
    print()
    print(f"{'M.N.K':>8}  {'ms':>8}  {'TFLOPS':>8}  {'%peak':>7}  {'README stock':>14}  {'README unlock':>14}")
    print("-" * 72)

    for n in args.sizes:
        median, best, iters = bench_one(n, n, n, device, args.warmup, args.repeat, args.min_ms)
        tflops = gemm_tflops(n, n, n, median)
        pct = 100.0 * tflops / PEAK_2080TI_TFLOPS
        stock = README_STOCK.get(n)
        unlocked = README_UNLOCKED.get(n)
        stock_s = f"{stock:6.1f}" if stock is not None else "     -"
        unlock_s = f"{unlocked:6.1f}" if unlocked is not None else "     -"
        print(
            f"{n:8d}  {median:8.3f}  {tflops:8.1f}  {pct:6.1f}%  "
            f"{stock_s:>14}  {unlock_s:>14}   (best {gemm_tflops(n, n, n, best):.1f}, {iters} iters)"
        )

    extra = nvquery("power.draw,clocks.sm,temperature.gpu")
    if extra:
        print()
        print(f"after    power {extra[0]} W   SM {extra[1]} MHz   temp {extra[2]} C")
    print()
    print("Compare TFLOPS to the Linux README stock vs unlock columns on the same SKU.")
    print("TITAN RTX is a full 72-SM TU102 (2080 Ti is 68 SM). Stock TITAN RTX already")
    print("reaches/exceeds unlocked 2080 Ti peaks; inject is not expected to add ~1.5x.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
