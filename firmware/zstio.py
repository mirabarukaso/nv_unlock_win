# SPDX-License-Identifier: GPL-2.0-only
"""Read a member from a .tar.zst without requiring a zstd binary."""
from __future__ import annotations

import shutil
import subprocess
import tarfile
from pathlib import Path


def extract_member_zst(package: Path, name: str, read_member) -> bytes:
    zstd_bin = shutil.which("zstd")
    if zstd_bin is not None:
        process = subprocess.Popen([zstd_bin, "-dc", str(package)], stdout=subprocess.PIPE)
        try:
            with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
                result = read_member(archive, name)
            if process.wait() != 0:
                raise ValueError("zstd could not read the pinned package")
            return result
        finally:
            if process.stdout:
                process.stdout.close()
            if process.poll() is None:
                process.kill()
                process.wait()
    try:
        import zstandard
    except ImportError as exc:
        raise ValueError("need the zstd binary or: python -m pip install zstandard") from exc
    with package.open("rb") as handle:
        reader = zstandard.ZstdDecompressor().stream_reader(handle)
        with tarfile.open(fileobj=reader, mode="r|") as archive:
            return read_member(archive, name)
