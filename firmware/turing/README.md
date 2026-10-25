# Pinned 610.43.03 firmware

`make inputs` runs `fetch_artifacts.py`, which downloads the NVIDIA open kernel
module source archive and the exact CachyOS `nvidia-utils` package pinned in
`artifacts-610.43.03.json`. Downloads use HTTPS and are checked against their
SHA-256 before they enter the cache; cached files are rechecked on every run.
Nothing downloaded is executed or installed.

`make firmware` builds the TU102 files the kernel module loads from
`turing-unlock/610.43.03/`:

- `generate_v67.py` extracts `gsp_tu10x.bin`, preserves the NVIDIA license, and
  generates the 64,000-byte `v67-tu102-610.43.03.bin`.
- `extract_boot_assets.py` builds the TU102 Booter/GSP boot assets and
  `sec2_bl_tu102.bin` from the source archive.

Both scripts check every input and output against its pinned SHA-256 before
writing anything. See
[NOTICE.md](NOTICE.md) for provenance and licensing.
