> [!IMPORTANT]
> 没怎么测试过        
> 仅在以下平台关闭Secure Boot进行了验证: Titan RTX(Turing, Windows 10) / Hyper-V (Windows 10)       
>

# GPU Unlock UEFI loader (Windows)

[English README](README.md)

未签名 EFI 包装器，替换 `\EFI\Microsoft\Boot\bootmgfw.efi`，可选执行 Turing / Ampere / Ada 解锁，再 chain-load 原版 Windows Boot Manager。

本树移植的 Linux 模块来源：

| 架构 | 上游 |
| --- | --- |
| Turing | https://github.com/nlzy/turing-unlock |
| Ampere | https://github.com/Zagitta/gpu-unlock |
| Ada | https://github.com/JustAResearcher/gpu-unlock-ada |

**本 UEFI 树已验证的范围**

- Hyper-V 第二代虚拟机，**Secure Boot 关闭**：菜单 → 跳过注入 → 进入 Windows。虚拟机没有 NVIDIA GPU。Inject 会枚举 PCI，打印 `0` 张卡，**不写 MMIO**。
- 实机 **NVIDIA TITAN RTX**（完整 72 SM TU102）：先跑库存 FP16 GEMM，再做一次 UEFI 强制注入（`I`，`10de:1e02`）。Loader 打印 `TU102 FEAT UNLOCKED`，`SS0=88888888` `SS1=8`。进 Windows 后 GEMM 与库存一致（噪声范围内），见 [Benchmark](#benchmark)。TITAN 出厂已是满 SM，不会出现 2080 Ti 那种大约 1.5 倍。
- 其它实机（2080 Ti、Ampere、Ada）上的 UEFI 注入 **尚未** 在本树验证。

版本：`0.2.4`（菜单标题 `GPU Unlock Loader 0.2.4 (<Arch>)`）。许可证：GPL-2.0-only，见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。

`0.2.4` 对应 [#1](https://github.com/mirabarukaso/nv_unlock_win/issues/1) 里的构建问题：EDK2 固定到 `edk2-stable202608`，仓库带上 `assets\config.ini`，loader 在进 Windows 前把一行 `handoff` 写入 `unlock.log`。2080 Ti 仍需要复测。这张 TITAN RTX 看不出 SS 写入是否生效，也看不出驱动是不是因为设备 ID `10de:1e02` 才没有拒绝。

---

## 目录

1. [它做什么](#它做什么)
2. [准备条件](#准备条件)
3. [下载工具链](#下载工具链)
4. [准备工作](#准备工作)
5. [编译](#编译)
6. [Firmware blob](#firmware-blob)
7. [清理](#清理)
8. [Hyper-V 虚拟机](#hyper-v-虚拟机)
9. [安装](#安装)
10. [启动菜单](#启动菜单)
11. [启动日志](#启动日志)
12. [卸载与恢复](#卸载与恢复)
13. [WinPE / 安装 U 盘重建 ESP](#winpe--安装-u-盘重建-esp)
14. [Benchmark](#benchmark)
15. [实机注入](#实机注入)
16. [排障](#排障)
17. [许可证](#许可证)
18. [布局](#布局)

---

## 它做什么

固件仍然从 `\EFI\Microsoft\Boot\bootmgfw.efi` 启动，不必改 BIOS BootOrder。

1. 安装脚本把原版 `bootmgfw.efi` 备份为 `bootmgfw_ms.efi`，再把本 loader 写到官方路径。
2. 开机菜单默认 **SKIP inject**（20 秒超时）。倒计时期间 **只读** PLM/SS0/SS1 并显示。`I` 注入（含本架构仅 readout 的 ID，例如 TITAN RTX）。注入后再读一遍寄存器，等 20 秒或按任意键，再进系统。`R` 紧急进入 Windows。chainload 前，跳过和注入都会记一行 `handoff`，并把 `\EFI\turing-unlock\unlock.log` 一次写完。这一行是 Windows 接手时的寄存器：PLM/SS0/SS1/readout、WPR2、boot、GSP/SEC CPUCTL、PCI command、BAR0。
3. 注入只在扫描到当前 **编译架构** 的 NVIDIA 显示设备 **并且** ESP 上有 firmware blob 时才会写 MMIO。
4. 无论是否注入，loader 都会把 **签名过的原版** 写回 `\EFI\Microsoft\Boot\bootmgfw.efi`（BCD / Code Integrity 认这个文件名），再 `LoadImage` 它。只 chainload 改名后的 `bootmgfw_ms.efi` 会触发 `0xc0000428`。
5. 计划任务 `GpuUnlock Boot Guard`（SYSTEM）只在 **开机约 1 分钟后跑一次**。Loader 进 Windows 前必须把签名原版 `bootmgfw.efi` 写回去，所以**这一次**开机可以没有菜单、直接进系统。Windows 起来后任务再把包装器贴回。会话中 Windows Update 或其它回滚最多让**下一次**重启少一次菜单，再下一轮就会回来。不需要每小时跑。任务的 `-File` 指向本仓库里的 `tools\Protect-GpuUnlockBoot.ps1`。装完后**不要删这个目录**，否则任务会指向不存在的脚本。

UEFI GOP 字体通常不能画汉字，菜单是英文。

**Secure Boot 必须关闭。** 包装器未签名，固件在 SB 开启时不会加载它。关掉 SB 只让固件愿意加载包装器；Windows 仍会校验 `bootmgfw.efi` 的数字签名，所以官方路径上必须是微软原件才能进系统。

---

## 准备条件

在 **装有 Visual Studio 的 Windows 机器** 上编译（不必是目标虚拟机）。建议：

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10/11 x64 |
| Visual Studio 2022 | 工作负载 **使用 C++ 的桌面开发**，MSVC v143 x64 |
| Python | 3.6+（`py -3` 或 `python` 在 PATH 上）。生成 firmware 还要 `pip install zstandard` |
| Git | 用来克隆 EDK2 |
| 磁盘 | EDK2 完整树大约数 GB |
| 目标虚拟机 | Hyper-V **第二代**，Windows 10/11 x64，管理员账号 |

Build Tools 也可以：只要装了 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`。`vswhere` 能扫到即可。

NASM、EDK2 按下一节下载，不要指望系统自带。

---

## 下载工具链

所有命令默认当前目录是 **本仓库根**（含 `GpuUnlockPkg` 和 `tools` 的那一层）。若仍在父仓库里，先 `cd uefi`。

### 1. EDK2

构建脚本在这些位置找 `edksetup.bat`：

- `.\edk2`
- `C:\edk2`
- `C:\src\edk2`
- `%USERPROFILE%\edk2`
- `%USERPROFILE%\src\edk2`
- 环境变量 `EDK2_PATH` 或参数 `-Edk2Path`

使用 **edk2-stable202608** 或更新。DSC 会链接 `CompilerIntrinsicsLib`，并且必须拉 submodule。

- `edk2-stable202408` 没有这个库，`subhook` 仍指向已经不存在的 `github.com/Zeex/subhook`。
- `edk2-stable202411` 有这个库，subhook 也改到了 `tianocore/edk2-subhook`，但 `CompilerIntrinsicsLib/*_ms.c` 在 x64 上把 `size_t` 定义成 32 位，编译报 `C2371`。
- `edk2-stable202511` 及之后把 `size_t` 定义成 `unsigned __int64`。这是第一个能用 VS x64 编过本包的 stable 标签。

克隆本身不会生成 `BaseTools\Bin\Win32\GenFw.exe`。编本包之前要先编一次 BaseTools：打开 **x86 Native Tools** 提示符，运行 `edksetup.bat Rebuild`。缺 `GenFw.exe` 是这一步没做，不是缺 submodule。

```powershell
git clone --recurse-submodules https://github.com/tianocore/edk2.git edk2
cd edk2
git checkout edk2-stable202608
git submodule update --init --recursive
cd ..
```

网络不好可以浅克隆：

```powershell
git clone --depth 1 --branch edk2-stable202608 https://github.com/tianocore/edk2.git edk2
cd edk2
git submodule update --init --recursive
cd ..
```

也可以把已有 EDK2 放到别的盘，构建时传 `-Edk2Path D:\src\edk2`。

### 2. NASM

EDK2 即使编本应用也会汇编 MdePkg 里的 `.nasm`，没有 NASM 会直接失败。下载 **win64** 包：

- 站点：https://www.nasm.us/
- 3.02 zip：https://www.nasm.us/pub/nasm/releasebuilds/3.02/win64/nasm-3.02-win64.zip

解压后让 `nasm.exe` 出现在：

```
nasm-3.02\nasm.exe
```

也可以把 `nasm.exe` 放到 PATH，或设 `NASM_PREFIX` / 传 `-NasmPrefix`（目录路径，脚本会补尾部反斜杠）。2.15.05 以上即可，本树按 3.02 写的。

### 3. 确认工具

```powershell
git --version
py -3 --version
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
```

NASM 解压后：

```powershell
.\nasm-3.02\nasm.exe -v
```

---

## 准备工作

`tools\Prepare-GpuUnlock.ps1` 会检查本机工具和源码目录，再把本包链接/复制进 EDK2，并暂存可选 firmware。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Prepare-GpuUnlock.ps1
powershell -ExecutionPolicy Bypass -File .\tools\Prepare-GpuUnlock.ps1 -Arch Turing
```

`-Arch` 为 `Turing`（默认）、`Ampere` 或 `Ada`。准备和编译都只针对 **一个** GPU 架构。公共 loader 始终检查；只有选中的 `Arch\<name>\` 作为注入后端。

检查项：

- Windows x64、VS2022 C++（17.x）、Python 3.6+、NASM 2.15.05+、git、EDK2
- EDK2 内容：`edksetup.bat`、`MdePkg`、`MdeModulePkg`、`MdeLibs.dsc.inc`、`CompilerIntrinsicsLib`、BaseTools
- 公共目录：`GpuUnlockPkg\Application\Loader`、`Arch\Turing`、`Arch\Ampere`、`Arch\Ada`、`Include`、`tools`、`assets`、`bench`
- 选中架构的源码（Turing、Ampere、Ada 都是 `Unlock.c` + `Portable\`）。另外两个架构只报告存在，不编进去
- 选中的 INF（`Loader.inf` / `LoaderAmpere.inf` / `LoaderAda.inf`）`[Sources]`、公共头文件、tools 脚本、`VERSION` / `LICENSE` / `NOTICE`
- 版本字符串一致（`VERSION`、`TU_LOADER_VERSION`、`Loader.inf`、`GpuUnlockPkg.dec`）
- `BASE_NAME` 仍是 `TuringUnlockLoader`（ESP 上的拷贝），magic 仍是 `TURING_UNLOCK_LOADER_v1`。本机输出是 `UnlockLoader-<Arch>.efi`
- `-Arch Turing` 且和 Linux 模块在一起时，每个 `Arch\Turing\Portable\*.c` 都有对应的 kernel 源文件

准备动作：

- 建立 junction `edk2\GpuUnlockPkg` → 本目录 `GpuUnlockPkg`（`mklink /J` 失败则整包刷新复制）
- 清掉旧树留下的 `edk2\TuringUnlockPkg` junction
- 缺 `Conf\*.txt` 时从 BaseTools 模板拷过去
- 创建 `.\build`
- 若 `assets\firmware\<Arch>\` 里已有该架构 blob（见 [Firmware blob](#firmware-blob)），就报告已暂存。不再去搜旁边的 Linux 仓库。

缺 EDK2 或 NASM 时可以直接拉：

```powershell
.\tools\Prepare-GpuUnlock.ps1 -FetchEdk2
.\tools\Prepare-GpuUnlock.ps1 -FetchNasm
.\tools\Prepare-GpuUnlock.ps1 -FetchEdk2 -FetchNasm
```

所有 `[fail]` 修掉，直到打印 `Prepare complete.`

## 编译

编译会先跑 Prepare（同一棵树已经 Prepare 成功时可用 `-SkipPrepare`）。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1 -Arch Turing
```

脚本会：

1. 按该架构跑 Prepare（工具 + 目录 + EDK2 包链接/复制 + firmware 暂存）
2. 调用 `edksetup.bat VS2022` 再 `build -a X64 -t VS2022 -b DEBUG -p GpuUnlockPkg\GpuUnlockPkg.dsc -D GPU_UNLOCK_ARCH=TURING`
3. 把 EFI 拷到 `.\build\UnlockLoader-Turing.efi`（Ampere/Ada 同样是 `UnlockLoader-<Arch>.efi`）。安装时仍写到 ESP 的 `EFI\turing-unlock\TuringUnlockLoader.efi`。

Ada 用 AD102 系列 Linux 事务（four-PLM V67，成功后 FLR）。Ampere 用 GA102 Linux 事务（V67 + SS0/SS1，成功后 FLR）。每次 `-Arch` 只编该后端：

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ada
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ampere
```

可选参数：

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Turing -Edk2Path C:\edk2 -NasmPrefix .\nasm-3.02\ -Target RELEASE -ToolChain VS2022
```

`iasl`、CLANG、Cygwin 的提示可以忽略，本应用用不到。以前那行 `Cannot find BaseTools Bin Win32` 是 `toolsetup.bat` 延迟展开的误报；现在编译脚本会先设置 `EDK_TOOLS_BIN`。

成功后再编一次是增量的，大约几秒。菜单里的版本字符串必须和 `GpuUnlockPkg\Include\TuConfig.h` 里的 `TU_LOADER_VERSION` 一致（现在是 `0.2.4`），并且对应当前编译的架构。

用 EFI shell 冒烟、先不替换 bootmgr：

```
fs0:
cd EFI\turing-unlock
TuringUnlockLoader.efi noload
```

`noload` 停在菜单之后。`inject` / `skip` 跳过菜单。

---

## Firmware blob

NVIDIA firmware **不在 git 里**。Hyper-V 跳过注入时不需要。实机注入会读 ESP 上的 `\EFI\turing-unlock\firmware\`，缺文件会在碰 MMIO **之前** 失败（`firmware missing` / `load_fw error=-2`）。

每个架构有自己那一套 pinned **610.43.03**。Turing 的 blob 不能解 Ampere / Ada。TITAN RTX 是 TU102，只生成 **Turing**。

### 获取步骤下载什么

`tools\Build-GpuUnlockFirmware.ps1` 会跑 `firmware\<arch>\fetch_artifacts.py`。URL 和 SHA-256 写在 `firmware\<arch>\artifacts-610.43.03.json`（Ada 还有 `ada-610.43.03.json`）。哈希对不上的文件不会收下。下载的东西不会拿来执行，只由本树的生成器解包、写出 blob。

| 输入 | 来源（pinned） | 用途 |
| --- | --- | --- |
| `NVIDIA-kernel-module-source-610.43.03.tar.xz` | NVIDIA 开源内核模块源码（`download.nvidia.com`） | 从 generated C 解出 Booter / GSP boot / SEC2 镜像 |
| `nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst` | CachyOS 上该版本的软件包 | `gsp_tu10x.bin` 或 `gsp_ga10x.bin`，以及 `NVIDIA-LICENSE` |

V67 载荷（`v67-*-610.43.03*.bin`）由上游脚本在本地生成，不是 NVIDIA 可再分发文件。

缓存（gitignore）：`uefi\.cache\firmware\`。输出（gitignore）：`uefi\assets\firmware\<Arch>\`。不要提交 `*.bin` 或 `NVIDIA-LICENSE`。

### 生成（在本目录 `uefi/` 下）

需要 Python 3.6+ 和 `zstandard` 包（Windows 默认没有 `zstd`/`xz`，包装器用 Python `tarfile` + `zstandard`）。

```powershell
python -m pip install zstandard
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Turing
```

Ampere / Ada 把 `-Arch` 换成 `Ampere` 或 `Ada`。`-Offline` 只校验缓存、不再走 HTTPS。`-CacheDir` / `-OutputDir` 可改路径。

utils 包里的 NVIDIA Driver License Agreement 仍约束解出来的 firmware。预期用途是本机打包；本树 **不主张** 再分发这些 blob。见 `firmware\<arch>\NOTICE.md`。

安装时用 `-FirmwareDir .\assets\firmware\Turing` 拷到 ESP（loader 文件名是 `UnlockLoader-<Arch>.efi` 且该目录已有 `.bin` 时可省略）。文件清单见 [实机注入](#实机注入)。更多说明：[firmware/README.md](firmware/README.md)。

---

## 清理

删掉本机 EFI 拷贝和 EDK2 的 `Build\GpuUnlock*` 目录。不会卸载 ESP 上的包装器，也不会删 EDK2 / NASM 克隆。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Firmware
```

`-Arch` 只清该架构的 `UnlockLoader-<Arch>.efi` 和对应 EDK2 输出目录。`-Firmware` 再删 `assets\firmware\` 和 `assets\firmware\<Arch>\` 里暂存的 blob。`-WhatIf` 只打印路径，不删。

---

## Hyper-V 虚拟机

本 loader 的安装/跳过/进系统路径是在这种环境里测的。实机 GPU 注入没有。

1. 新建 **Generation 2** 虚拟机，装 Windows 10/11 x64。
2. 关机。Hyper-V 管理器 → 虚拟机 → Settings → **Security**：
   - 取消 **Enable Secure Boot**
3. 把 `build\UnlockLoader-Turing.efi` 和整个 `tools\` 拷进虚拟机（共享盘、ISO 或 `Copy-VMFile`）。
4. 虚拟机里用 **管理员** PowerShell 跑下一节的安装命令。
5. 重启。应看到 `GPU Unlock Loader 0.2.4 (Turing)`。直接回车或等超时，应进入 Windows。

虚拟机没有 NVIDIA 直通时，Inject 也是安全的：`gpu_count=0`，不写 MMIO。

宿主机 TITAN RTX 的注入结果见 [Benchmark](#benchmark)：寄存器解锁，GEMM 不变。

---

## 安装

在 **目标 Windows**（虚拟机）上，管理员 PowerShell：

```powershell
cd <拷进去的 tools 目录>
powershell -ExecutionPolicy Bypass -File .\Install-GpuUnlockBoot.ps1 -LoaderEfi <UnlockLoader-Turing.efi 的完整路径>
```

示例（路径按实际修改）：

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-GpuUnlockBoot.ps1 -LoaderEfi C:\Users\Public\UnlockLoader-Turing.efi
```

脚本会：

- 拒绝非管理员
- 拒绝覆盖非 PE 的 `bootmgfw.efi`
- 把原件拷到 `EFI\turing-unlock\bootmgfw_ms.efi`、`EFI\Microsoft\Boot\bootmgfw_ms.efi` 以及带时间戳的 `backup\`
- 写 `EFI\turing-unlock\config.ini`（`default=skip`）
- 把包装器写到 `EFI\Microsoft\Boot\bootmgfw.efi` 和 `EFI\turing-unlock\TuringUnlockLoader.efi`
- 注册计划任务 `GpuUnlock Boot Guard`（开机约 1 分钟后跑一次），并马上启动一次。任务执行的是本目录下的 `tools\Protect-GpuUnlockBoot.ps1`；**装好后不要删掉这个仓库**，否则任务没有脚本可跑。若以后要搬家，在新路径再跑一次 Install（或 `Protect-GpuUnlockBoot.ps1 -RegisterTask`）。

常用参数：

| 参数 | 含义 |
| --- | --- |
| `-TimeoutSec 20` | 菜单秒数（默认 20） |
| `-DefaultAction skip` | 默认动作 `skip` 或 `inject` |
| `-AlsoFallback` | 若 `\EFI\Boot\bootx64.efi` 与原版 bootmgfw 相同，也换成包装器 |
| `-SkipScheduledTask` | 不注册保护任务 |
| `-FirmwareDir <dir>` | 把该架构的 blob 拷到 ESP 的 `firmware\`（实机注入才需要） |

**每次重新编译后都要再跑一次安装**，否则 ESP 上还是旧包装器。菜单标题必须对应当前编译的架构，例如 `0.2.4 (Turing)`。

---

## 启动菜单

```
GPU Unlock Loader 0.2.4 (Turing)
NVIDIA display GPUs: 1  (feat read-only until I)
  0000:65:00.0  10de:1e02  Turing  (I forces)
    PLM=fffffff8  SS0=00000000  SS1=00000007  RO=00000000

  [Enter]  Continue WITHOUT inject   (default)
  [I]      Inject (force same-arch IDs) then show regs
  [R]      Emergency chainload Windows now

Default in 20 sec: SKIP inject
```

`I` 会注入白名单，也会强制注入本架构仅 readout 的卡（Turing 编译下的 TITAN RTX `10de:1e02`）。注入后打印 `Feat AFTER inject`，等 20 秒或按任意键再进 Windows。ESP 上要有 TU102 firmware blob，否则会在写 MMIO 之前失败。

`0.2.3` 在进菜单前会 Connect PCI USB 主机控制器（xHCI/EHCI/UHCI/OHCI）和 `ConsoleInHandle`，这样固件快速 POST 没交接 USB 时键盘也能用。不会递归 Connect 全部 PCI 设备（避免把 NVIDIA GOP 再绑一遍）。默认 SKIP 仍然可以 20 秒不按键进 Windows。

日志里正常的跳过 + 进系统大致是：

```
Inject skipped. Continuing to Windows.
restoring signed bootmgfw.efi from \EFI\Microsoft\Boot\bootmgfw_ms.efi (...)
restore ok: \EFI\Microsoft\Boot\bootmgfw.efi (...)
Starting Windows Boot Manager...
```

Windows 起来后，`GpuUnlock Boot Guard` 会把包装器写回 `bootmgfw.efi`（大约开机 1 分钟后）。刚 chainload 进 Windows 的那一次用的是还原后的微软文件，所以没有菜单；**下一次**重启才会再出菜单。若 Windows Update 在运行期间把官方 `bootmgfw.efi` 写回去，下一次重启可能少一次菜单；开机任务会在那一轮 Windows 里再贴回包装器，再下一轮重启就有菜单。

---

## 启动日志

chainload 前，loader 会把 `\EFI\turing-unlock\unlock.log` 一次写完。里面有一行 `handoff`（PLM/SS0/SS1/readout、WPR2、boot、GSP/SEC CPUCTL、PCI command、BAR0）。平时 Windows 不挂载 ESP，把这份日志拷出来需要管理员 PowerShell。在你希望保存日志的那棵目录里跑（编译就在编译树里跑）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Copy-GpuUnlockLog.ps1
```

脚本在 ESP 没有盘符时挂上它，把 `EFI\turing-unlock\*.log` 拷到本树的 `logs\`，然后卸掉自己分配的盘符。原来就已经挂着的盘符会留着。最新一份同时存成：

- `logs\unlock.log`
- `logs\unlock-yyyyMMdd-HHmmss.log`（时间取 ESP 上该文件的修改时间）

`logs\` 在 gitignore 里。`-Destination` 可以改输出目录。`-EspRoot` 使用已经挂好的 ESP 根，例如 `S:`。

---

## 卸载与恢复

### A. 系统还能进 Windows

管理员：

```powershell
.\Uninstall-GpuUnlockBoot.ps1
```

从 `bootmgfw_ms.efi` 恢复官方 `bootmgfw.efi`，并删除计划任务。ESP 上目录名仍是 `EFI\turing-unlock`（loader 路径）。不加 `-Purge` 会留下这个目录（loader 副本、bootmgfw 备份、**firmware blob**）。

```powershell
.\Uninstall-GpuUnlockBoot.ps1 -Purge
```

`-Purge` 会递归删除 `EFI\turing-unlock\`（含 `firmware\*.bin`）、`EFI\Microsoft\Boot\bootmgfw_ms.efi` 以及 `C:\ProgramData\GpuUnlock`。删之前会打印 firmware 文件列表。

### B. 进不了 Windows，但 ESP 文件还在（优先用这个）

用 Windows 安装 U 盘 / 恢复环境进 **命令提示符**，**不要** 先 format。把签名备份盖回官方文件名即可，BCD 可以留着：

```
X:\> diskpart
DISKPART> list vol
DISKPART> sel vol <ESP 的卷号>
DISKPART> assign letter=S
DISKPART> exit

X:\> copy /y S:\EFI\turing-unlock\bootmgfw_ms.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
```

ESP 一般是 FAT32、一百到几百 MB，标签可能是 `SYSTEM`。若 `turing-unlock` 目录没有，再试：

```
X:\> copy /y S:\EFI\Microsoft\Boot\bootmgfw_ms.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
```

然后重启，应直接进 Windows（包装器没了）。

### C. 包装器卡在官方路径上的 `0xc0000428`

原因：Windows 校验的是 `\EFI\Microsoft\Boot\bootmgfw.efi`，那里还是未签名包装器。用 B 的 copy 即可，不必 format。

---

## WinPE / 安装 U 盘重建 ESP

下面是 **最后手段**：ESP 损坏、BCD 丢失、备份也没了。`format` 会清掉整块 EFI 分区上的启动文件（包装器、备份、BCD 全部消失）。能 copy 备份就不要走到这一步。

用 Windows 安装 ISO / U 盘启动 → **修复计算机** → **命令提示符**。

`C:` 在 WinPE 里常常 **不是** Windows 分区。先看卷：

```
diskpart
   list vol
   sel vol <EFI volume number>
   assign letter=S:
   exit
```

`list vol` 时同时看清 Windows 分区（NTFS、体积大）。若 Windows 不在 `C:`，下面的 `C:\Windows` 改成实际盘符，例如 `W:\Windows`。

确认 `S:` 就是那块小的 FAT32 EFI 分区之后：

```
format S: /fs:FAT32 /q

bcdboot C:\Windows /s S: /f UEFI
```

`bcdboot` 会在 `S:\EFI\Microsoft\Boot\` 重建 `bootmgfw.efi` 和 BCD。重启后应进 Windows，包装器不再存在。

若 `bcdboot` 报找不到 Windows，先：

```
dir C:\Windows\System32\winload.efi
dir D:\Windows\System32\winload.efi
dir E:\Windows\System32\winload.efi
```

找到真正的系统盘再跑 `bcdboot`。

Hyper-V：虚拟机设置里挂上官方 Windows ISO，Firmware 启动顺序把 DVD 放前面，选 Repair。

---

## Benchmark

脚本：[`bench/gemm_fp16.py`](bench/gemm_fp16.py)。用 PyTorch 走 cuBLAS：FP16 输入、tensor core、内部 FP32 accumulate，尺寸与 Linux README 的 1024/2048/4096/8192 表一致。

**说明（请先读）**

- 本树唯一测过的实机 GPU 是宿主机 **TITAN RTX**（完整 72 SM TU102）。先库存 GEMM，再 UEFI 注入，再 GEMM。
- UEFI 包装器的启动路径是在没有 NVIDIA GPU 的虚拟机里测的。虚拟机里的 GEMM 数字不能和这张表比。
- TITAN RTX 是完整 72 SM 的 TU102，2080 Ti 是 68 SM。注入会把 GOP 时代 SS 写成满速（`SS0=88888888`、`SS1=8`），但 GEMM 不会出现 2080 Ti 那种大约 1.5 倍。

宿主机（有 NVIDIA GPU 的物理机）：

```powershell
pip install torch
python .\bench\gemm_fp16.py
python .\bench\gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80
```

同一台机器，2026-10-25，Windows，驱动 617.42，PyTorch 2.9.0+cu130，CUDA 13.0，功耗上限 320 W。命令：`python .\bench\gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80`。表中是脚本给出的中位数 TFLOPS。

- **库存**（无 UEFI 注入）：跑完约 309 W、SM 1935 MHz、49 C。
- **UEFI 注入后**：loader `TU102 FEAT UNLOCKED`，`Feat AFTER` 为 `PLM=FFFFFFFF SS0=88888888 SS1=00000008`。随后在 Windows 里连跑三次 GEMM；表用三次中位数的中位数。跑完约 285–290 W、SM 1920–1965 MHz、47–50 C。

2080 Ti 解锁前 / 解锁后两列来自 Turing Linux README（Dell RTX 2080 Ti 22GB @250W，cuBLAS FP16 / FP32 accumulate）。100% peak = 56.9 TFLOPS（《NVIDIA Turing Architecture Whitepaper》）。

| M.N.K | TITAN RTX 库存 | TITAN RTX 注入后 | 2080 Ti 解锁前 | 2080 Ti 解锁后 |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 44.2 (77.6%) | 44.8 (78.7%) | 50.3 (88.4%) | 68.3 (120.0%) |
| 2048 | 87.6 (153.9%) | 87.4 (153.6%) | 55.7 (97.9%) | 91.4 (160.6%) |
| 4096 | 95.4 (167.6%) | 95.1 (167.1%) | 53.7 (94.4%) | 81.7 (143.6%) |
| 8192 | 99.3 (174.5%) | 99.1 (174.2%) | 52.8 (92.8%) | 77.9 (136.9%) |

注入后三次中位数：1024 = 44.6 / 44.8 / 45.3；2048 = 87.4 / 87.3 / 87.4；4096 = 94.9 / 95.1 / 96.0；8192 = 99.1 / 99.4 / 98.2。相对库存大约 1% 以内，属于跑次噪声，没有 tensor 速率提升。

1024 这个尺寸受 kernel launch 开销影响（中位数约 0.048 ms）。2048 接近解锁后的 2080 Ti。4096 和 8192 无论库存还是注入后都超过解锁后的 2080 Ti。

---

## 实机注入

本机 **NVIDIA TITAN RTX**（`10de:1e02`，完整 72 SM TU102）已测过：loader 0.2.3，ESP 上有 TU102 firmware，强制 `I`：

- Loader：`TU102 FEAT UNLOCKED`，`Feat AFTER` 为 `PLM=FFFFFFFF SS0=88888888 SS1=00000008`
- 快速 POST 下 USB 键盘可用
- Windows 里 GEMM 与库存差大约 1%（TITAN 出厂已满速，没有 2080 Ti 那种约 1.5 倍）

真正需要解锁的卡（GeForce 2080 Ti、Ampere、Ada）上的注入 **尚未** 在本树验证。后果自负。Tensor core 满速功耗很高，可能损坏硬件。

每个架构要 **自己那一套** blob。如何获取和生成见 [Firmware blob](#firmware-blob)。TITAN RTX 是 TU102，编 Turing 即可。没有这些文件时，注入路径会在碰 FLR/MMIO **之前** 报错。虚拟机没有卡，这些文件可省略。

Turing（`-Arch Turing`；成功路径写 SS0=`0x88888888`、SS1=`0x8`，然后 **不 FLR**）：

- `booter_load_tu102.bin`、`v67-tu102-610.43.03.bin`、`gsp_tu10x.bin`、`gsp_boot_tu102.bin`、`gsp_boot_desc_tu102.bin`、`sec2_bl_tu102.bin`

Ampere（`-Arch Ampere`；成功路径 **会** FLR）：

- `booter_load_ga102.bin`、`v67-ga102-610.43.03.bin`、`gsp_ga10x.bin`、`gsp_boot_ga102.bin`、`gsp_boot_desc_ga102.bin`

Ada（`-Arch Ada`；four-PLM V67，成功路径 **会** FLR）：

- `booter_load_ad102.bin`、`v67-ad102-610.43.03-four-plm.bin`、`gsp_ga10x.bin`、`gsp_boot_ad102.bin`、`gsp_boot_desc_ad102.bin`

```powershell
.\tools\Install-GpuUnlockBoot.ps1 -LoaderEfi .\build\UnlockLoader-Turing.efi -FirmwareDir .\assets\firmware\Turing
```

省略 `-FirmwareDir` 且 loader 文件名是 `UnlockLoader-<Arch>.efi` 时，若 `assets\firmware\<Arch>\` 里已有 `.bin`，Install 会用那个目录。

Firmware 二进制仍受 NVIDIA 许可约束，见 `firmware/<arch>/NOTICE.md`。本树不带这些 blob。

---

## 排障

| 现象 | 含义 | 处理 |
| --- | --- | --- |
| 固件直接进 Windows，没有菜单 | 这一次用的是还原后的微软 `bootmgfw.efi`，或 SB 仍开，或包装器没了 | loader/WU 还原后这一轮没有菜单是预期；下次重启应出现。若仍没有：关 SB，跑 `Protect-GpuUnlockBoot.ps1 -RegisterTask`，或重装 |
| `LoadImage failed F` / `Access Denied (0x800000000000000F)` | 旧 loader 用内存 buffer + NULL 路径加载 | 使用 0.1.1+，重装 |
| 恢复蓝屏 `0xc0000428` | CI 校验到的 `bootmgfw.efi` 还是未签名包装器 | 使用 0.1.2+（先 restore 再 chainload）；或按 [B](#b-进不了-windows但-esp-文件还在优先用这个) copy 备份 |
| 菜单不是 `0.2.4 (<Arch>)` | ESP 上还是旧 EFI | 再跑 Install |
| 菜单里键盘没反应（快速 POST） | 固件没启动 USB xHCI | 0.2.3+ 进菜单前会 Connect USB 主机；重装 |
| `firmware missing (status=e)` / `load_fw error=-2` | ESP 上没有当前架构的 blob | 跑 `Build-GpuUnlockFirmware.ps1 -Arch <Arch>`，再 Install `-FirmwareDir .\assets\firmware\<Arch>` |
| 进一次 Windows 后菜单消失 | 保护任务还没跑，或根本没注册 | 登录后等约 1 分钟（看 `C:\ProgramData\GpuUnlock\protect.log`），或管理员跑 `Protect-GpuUnlockBoot.ps1 -RegisterTask` |
| 构建缺 NASM | EDK2 要汇编 `.nasm` | 按上面解压到 `nasm-3.02\nasm.exe` |
| `LNK2001 memset` | 旧 DSC 没链 CompilerIntrinsicsLib | 本树 DSC 已包含，更新源码再编 |
| `memset_ms.c` / `memcpy_ms.c` 报 `C2371` `size_t` | EDK2 `edk2-stable202411` 在 x64 上把 `size_t` 定义成 32 位 | 换 `edk2-stable202608`（202408 没有 `CompilerIntrinsicsLib`） |
| 缺 `GenFw.exe`，报错让你重新克隆 | BaseTools 还没编 | 在 x86 Native Tools 提示符里运行 `edksetup.bat Rebuild` |
| `error 0004: File read failure` 指向 `Loader.inf` | INF/DSC/DEC 含非 ASCII（EDK2 按系统 ANSI 代码页打开，中文 Windows 常见 cp936） | 当前 INF 已是纯 ASCII；再跑 Prepare（非 ASCII 元数据会直接 fail） |
| 编译成功但仍打印 `Cannot find BaseTools Bin Win32` | `toolsetup.bat` 在延迟展开生效前测试了空的 `%EDK_TOOLS_BIN%` | 当前 Build 会在 `edksetup.bat` 之前设置 `EDK_TOOLS_BIN`；用现在的 tools 再编一次 |

---

## 许可证

本树源码是 **GPL-2.0-only**，与三个上游 Linux 模块相同。见 [LICENSE](LICENSE)（GNU GPL v2 全文）和 [NOTICE](NOTICE)（署名）。

三个上游都是 version 2 only，合并作品也保持 GPL-2.0-only。C 源码上的 SPDX 头与此一致。NVIDIA firmware 仍受 NVIDIA 许可约束，不在 git 里。审计时看过的 NVIDIA 头文件 MIT 声明仍留在各上游的 `licenses/`。

---

## 布局

```
.                          本目录 = 独立仓库根
  GpuUnlockPkg/
    Application/Loader/    公共启动包装（菜单、chainload、PCI）
    Arch/Turing/           TU102 注入（-Arch Turing）
    Arch/Ampere/           GA102 注入（-Arch Ampere）
    Arch/Ada/              AD102 注入（-Arch Ada）
    Include/               公共头文件
  tools/                   Prepare / 编 EFI / 编 firmware / Clear / Install / Uninstall / Protect / 拷日志
  logs/                    从 ESP 拷出的 unlock.log（gitignore）
  firmware/                分架构 blob 生成器（turing、ampere、ada）
  assets/config.ini
  assets/firmware/<Arch>/  生成的 blob（gitignore）
  bench/gemm_fp16.py
  build/                   编译输出（gitignore）
  nasm-3.02/               本地下载，不入库
  edk2/                    本地下载，不入库
  LICENSE                  GPL-2.0-only
  NOTICE                   三个上游 Linux 模块的署名
  VERSION                  0.2.4
  README.md                英文
  README_zh.md             中文
```
