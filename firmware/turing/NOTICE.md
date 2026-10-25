# Firmware provenance and distribution scope

`gsp_tu10x.bin` is extracted byte-for-byte from the checksummed NVIDIA
610.43.03 package in `artifacts-610.43.03.json`. The TU102 Booter, GSP boot and
SEC2 loader images are decoded unchanged from the pinned 610.43.03 source
archive. The extraction also preserves that package's complete
`usr/share/licenses/nvidia-utils/LICENSE` as `NVIDIA-LICENSE`. It is installed
with the firmware package.

The source archive's open-module licensing does **not** license NVIDIA firmware.
The binary package contains the NVIDIA Driver License Agreement. Its section
1.1(d) addresses distribution for open source operating-system kernels with
unchanged binary files and inclusion of the agreement. Additional limitations
apply, including NVIDIA-platform use. Personal local packaging is the initial
delivery target. Review the full pinned agreement and the separately generated
payload before any public distribution; redistribution approval is not claimed.

The TU102 V67 image is an independently developed mutex-free chain: two BAR0
writes followed by a real token read and a return through the Booter's normal
ACR mutex release/finalize path. It is covered by the project's GPL-2.0-only
licensing.

NVIDIA source files inspected during the loader audit carry their own MIT
copyright notices. The minimum helper's metadata layout and Falcon operations
follow those source contracts; `licenses/NVIDIA-MIT.txt` preserves the notice.
