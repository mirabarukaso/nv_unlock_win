#!/usr/bin/env python3
"""BF16 GEMM benchmark for Ampere (30-series) and Ada (40-series).

PyTorch calls cuBLAS with bf16 inputs. Tensor cores do the multiply and
accumulate in fp32. Square sizes match the FP16 script: 1024, 2048, 4096, 8192.

  python bench/gemm_bf16.py
  python bench/gemm_bf16.py --sizes 1024,2048,4096,8192 --repeat 80

Run it once with the loader on Skip and once after Inject. Compare TFLOPS
on the same card. Turing (20-series, compute capability 7.5) has no BF16
tensor cores, so this script refuses that GPU.
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


def parse_sizes(text: str) -> list[int]:
    sizes = [int(p.strip()) for p in text.split(",") if p.strip()]
    if not sizes:
        raise argparse.ArgumentTypeError("need at least one size")
    for n in sizes:
        if n < 8 or n % 8:
            raise argparse.ArgumentTypeError(f"{n} must be a multiple of 8 (tensor-core alignment)")
    return sizes


def nvquery(fields: str, device: int) -> list[str]:
    exe = shutil.which("nvidia-smi")
    if not exe:
        return []
    try:
        raw = subprocess.check_output(
            [exe, f"--query-gpu={fields}", "--format=csv,noheader,nounits", "-i", str(device)],
            encoding="utf-8",
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    return [p.strip() for p in raw.strip().split(",")]


def family(major: int, minor: int) -> str:
    if major == 8 and minor == 6:
        return "Ampere 30-series"
    if major == 8 and minor == 9:
        return "Ada 40-series"
    if major == 8 and minor == 0:
        return "Ampere GA100"
    if major == 9:
        return "Hopper"
    if major >= 10:
        return "Blackwell"
    return f"CC {major}.{minor}"


def gpu_banner(device: int) -> None:
    props = torch.cuda.get_device_properties(device)
    extra = nvquery("driver_version,power.draw,clocks.sm,clocks.mem,temperature.gpu", device)
    print(f"GPU      {props.name}")
    print(
        f"CC       {props.major}.{props.minor}  {family(props.major, props.minor)}"
        f"   VRAM {props.total_memory / (1024 ** 3):.1f} GiB"
    )
    print(f"PyTorch  {torch.__version__}   CUDA {torch.version.cuda}")
    if extra and len(extra) >= 5:
        print(
            f"Driver   {extra[0]}   power {extra[1]} W   "
            f"SM {extra[2]} MHz   MEM {extra[3]} MHz   temp {extra[4]} C"
        )
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
    a = torch.randn(m, k, device=device, dtype=torch.bfloat16)
    b = torch.randn(k, n, device=device, dtype=torch.bfloat16)
    c = torch.empty(m, n, device=device, dtype=torch.bfloat16)

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
    parser = argparse.ArgumentParser(description="BF16 tensor-core GEMM TFLOPS (cuBLAS via PyTorch)")
    parser.add_argument("--sizes", type=parse_sizes, default="1024,2048,4096,8192")
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--repeat", type=int, default=50)
    parser.add_argument("--min-ms", type=float, default=400.0, help="keep iterating each size at least this long")
    args = parser.parse_args()

    if not torch.cuda.is_available():
        print("CUDA is not available.", file=sys.stderr)
        return 1

    props = torch.cuda.get_device_properties(args.device)
    if props.major < 8:
        print(
            f"{props.name} is compute capability {props.major}.{props.minor}. "
            "BF16 tensor cores start at Ampere (30-series, 8.6) and Ada (40-series, 8.9).",
            file=sys.stderr,
        )
        return 1

    torch.cuda.set_device(args.device)
    device = torch.device("cuda", args.device)
    matmul = torch.backends.cuda.matmul
    if hasattr(matmul, "fp32_precision"):
        matmul.fp32_precision = "ieee"
    else:
        matmul.allow_tf32 = False
        torch.backends.cudnn.allow_tf32 = False
    if hasattr(matmul, "allow_bf16_reduced_precision_reduction"):
        matmul.allow_bf16_reduced_precision_reduction = False

    a = torch.randn(4096, 4096, device=device, dtype=torch.bfloat16)
    b = torch.randn(4096, 4096, device=device, dtype=torch.bfloat16)
    c = torch.empty(4096, 4096, device=device, dtype=torch.bfloat16)
    t_boost = time.perf_counter()
    while time.perf_counter() - t_boost < 1.5:
        torch.mm(a, b, out=c)
    torch.cuda.synchronize()
    del a, b, c

    gpu_banner(args.device)
    print("dtype    A,B,C = bf16   (tensor cores, fp32 accumulate inside cuBLAS)")
    print("sizes    M=N=K")
    print()
    print(f"{'M.N.K':>8}  {'ms':>8}  {'TFLOPS':>8}")
    print("-" * 28)

    for n in args.sizes:
        median, best, iters = bench_one(n, n, n, device, args.warmup, args.repeat, args.min_ms)
        tflops = gemm_tflops(n, n, n, median)
        print(
            f"{n:8d}  {median:8.3f}  {tflops:8.1f}"
            f"   (best {gemm_tflops(n, n, n, best):.1f}, {iters} iters)"
        )

    extra = nvquery("power.draw,clocks.sm,temperature.gpu", args.device)
    if extra and len(extra) >= 3:
        print()
        print(f"after    power {extra[0]} W   SM {extra[1]} MHz   temp {extra[2]} C")
    print()
    print("Compare this table with a Skip run and an Inject run on the same card.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
