> [!IMPORTANT]
> 这些测试都关闭了 Secure Boot。
>
> - **TITAN RTX**（TU102，`10de:1e02`），Windows 10，驱动 617.42。Loader 打印 `TU102 FEAT UNLOCKED`，`SS0=88888888` `SS1=8`。Windows 正常启动。8192 的 FP16 GEMM 保持 99.3 → 99.1 TFLOPS。这张卡出厂已是满速。
> - **RTX 3070 Ti**（GA104，`10de:2482`），Windows 10，驱动 617.42。Loader 打印 `GA102 FEAT UNLOCKED`，`SS0=88888888` `SS1=8`。Windows 正常启动。启动过程中会黑屏大约 1 分钟，别慌。8192 的 FP16 GEMM（FP32 累加）从 48.5 升到 86.3 TFLOPS（+78%）。
> - **RTX 4080**（AD103，`10de:2704`），loader **0.2.12** Windows Inject（代测）。Loader 打印 `AD102 FEAT UNLOCKED`，`SS0=88888888` `SS1=8`。Windows 正常启动。`mma_bench_sm89` 在 `f16/f32` / `bf16/f32` / `fp8/e4m3` 约 **2×**（对照项基本持平）。需要 `rtx4080_experimental=1` 以及 `targets` 里的 **精确** PCI BDF（不能用 `all`）。解锁仅当前会话有效；Inject 测试之间请关闭 **快速启动**。详情：**[docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md)**。
> - **RTX 4090**（AD102，`10de:2684`）— **需重新验证**。Loader 打印 `AD102 FEAT UNLOCKED`，`SS0=88888888` `SS1=8`。Windows 正常启动。此前发布的 Windows GEMM 数字已**暂时撤回**，等待重测（解锁后的 4080 0.2.12 MMA 已超过旧数字）。基线 ID `2684` 的配置支持不变。

# GPU Unlock UEFI loader (Windows)

[English README](README.md)

Turing、Ampere、Ada 的未签名 EFI loader。从一块 FAT32 U 盘启动，可选解锁 GPU，再启动内部磁盘上的 Windows、GRUB、systemd-boot 或其它 x64 EFI 加载器。U 盘不改内部 EFI 系统分区（ESP），固件启动菜单仍然可以启动已经装在那里的系统。

版本 `0.2.12`。许可证：GPL-2.0-only，见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。历史在[文末](#更新记录)。

多数人看到 [使用](#使用) 即可：下载 release 里的 EFI，生成对应 firmware，拷到 U 盘。[从头编译](#从头编译) 只在你要自己编 EFI 时才需要。

本树移植的 Linux 模块来源（Ada Windows handoff 另致谢 TOTORONG）：

| 架构 | 上游 | Release EFI |
| --- | --- | --- |
| Turing | https://github.com/nlzy/turing-unlock | `UnlockLoader-Turing.efi` |
| Ampere | https://github.com/Zagitta/gpu-unlock | `UnlockLoader-Ampere.efi` |
| Ada | https://github.com/JustAResearcher/gpu-unlock-ada | `UnlockLoader-Ada.efi` |
| Ada（Windows handoff） | https://huggingface.co/TOTORONG/AD103_win_unlocker | — |

## 目录

**使用**

1. [开始之前](#开始之前)
2. [它怎么工作](#它怎么工作)
3. [取得 EFI 和 firmware](#取得-efi-和-firmware)
4. [U 盘](#u-盘)
5. [启动菜单](#启动菜单)
6. [启动日志](#启动日志)
7. [Ada / RTX 40 系](#ada--rtx-40-系) → 细节 [docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md)
8. [机器起不来](#机器起不来)
9. [实测结果](#实测结果)

**高级**

10. [ESP 安装](#esp-安装)

**从头编译**

11. [准备条件](#准备条件)
12. [下载工具链](#下载工具链)
13. [准备和编译](#准备和编译)
14. [清理](#清理)
15. [Hyper-V 虚拟机](#hyper-v-虚拟机)
16. [编译排障](#编译排障)
17. [布局](#布局)

18. [许可证](#许可证)
19. [更新记录](#更新记录)

---

## 使用

### 开始之前

**关闭 Secure Boot。** Loader 未签名。Secure Boot 开着时固件不会加载它。

试 [ESP 安装](#esp-安装) 之前，准备好 WinPE 或能启动的 Windows 安装盘。U 盘路径不改内部 ESP，固件菜单仍能启动你原来的磁盘。只有 ESP 上的 `bootmgfw.efi` 被替换之后，这张盘才用得上。

EFI 要和 GPU 架构一致。Turing 的 firmware 不能解 Ampere 或 Ada。

RTX 3070 Ti 启动过程中会黑屏大约 1 分钟，别慌。

### 它怎么工作

在固件启动菜单里选这块 U 盘。Loader 显示一个短菜单，读特性寄存器；只有你选择注入、并且 U 盘上有对应 firmware 时才写入。然后它列出内部 NVMe 和 SATA 上的 EFI 加载器，用那块卷自己的设备路径启动你选的那一个。GRUB 会从该分区读 `grub.cfg` 和模块。装在 MBR 里的 BIOS GRUB，以及单独的 EFI stub `vmlinuz`，不会出现在列表里。启动 Linux 的是发行版自己的 EFI 加载器。

内部 ESP 保持不动。Windows 磁盘上的 `bootmgfw.efi` 不会被替换，也不会注册开机任务。

UEFI GOP 字体通常不能画汉字，菜单是英文。

### 取得 EFI 和 firmware

从 [GitHub release](https://github.com/mirabarukaso/nv_unlock_win/releases) 下载 `UnlockLoader-<Arch>.efi`。脚本仍在本仓库里。NVIDIA firmware 不在 git 里，也不在 release 里。

按架构生成。这一步下载固定的 610.43.03 输入，核对 SHA-256，写出 blob，不编译 EFI。需要 Python 3.6+ 和 `zstandard`。

```powershell
python -m pip install zstandard
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockFirmware.ps1 -Arch Turing
```

Ampere、Ada 分别用 `-Arch Ampere`、`-Arch Ada`。输出在 `assets\firmware\<Arch>\`。`-Offline` 只复核缓存里已有的文件。解出来的 firmware 仍受 NVIDIA 许可约束。见 `firmware\<arch>\NOTICE.md` 和 [firmware/README.md](firmware/README.md)。

| 架构 | Loader 要读的文件 |
| --- | --- |
| Turing | `booter_load_tu102.bin`、`v67-tu102-610.43.03.bin`、`gsp_tu10x.bin`、`gsp_boot_tu102.bin`、`gsp_boot_desc_tu102.bin`、`sec2_bl_tu102.bin` |
| Ampere | `booter_load_ga102.bin`、`v67-ga102-610.43.03.bin`、`gsp_ga10x.bin`、`gsp_boot_ga102.bin`、`gsp_boot_desc_ga102.bin` |
| Ada | `booter_load_ad102.bin`、`v67-ad102-610.43.03-four-plm.bin`、`gsp_ga10x.bin`、`gsp_boot_ad102.bin`、`gsp_boot_desc_ad102.bin` |

Turing 成功时写 SS0=`0x88888888`、SS1=`0x8`，然后不做 FLR。Ampere 和 Ada 在解锁成功后做 FLR。缺文件会在写 MMIO 之前失败（`firmware missing` / `load_fw error=-2`）。

### U 盘

全系列都用 U 盘。先在资源管理器里自己格式化：右键这块盘，选“格式化”，文件系统选 FAT32。卷大于 32 GB 时，资源管理器不会提供 FAT32。先在磁盘管理里划一个不超过 32 GB 的卷，再把那个卷格式化成 FAT32。`New-GpuUnlockUsb.ps1` 不会格式化磁盘。

在本仓库用管理员 PowerShell。`-LoaderDir` 是放着 release EFI 的目录。`build\UnlockLoader-<Arch>.efi` 已经在时可以不写。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\New-GpuUnlockUsb.ps1 -DriveLetter H -Arch Turing -LoaderDir C:\path\to\release -FirmwareRoot .\assets\firmware
```

`H` 和 `H:` 相同。不写 `-DriveLetter` 时，脚本列出每个 FAT32 USB 盘符。一个设备有多个盘符就占多行。输入序号，写入那个卷。不是 FAT32 的盘符会被拒绝。

| 参数 | 含义 |
| --- | --- |
| `-DriveLetter H` | 写入哪个 USB 盘符。不写就从列表里选 |
| `-Arch Turing` | 拷贝哪一套 loader 和 firmware。Ampere、Ada 换成对应名字 |
| `-BootArch` | `-Arch` 多于一个时，哪一个写成 `EFI\BOOT\BOOTX64.EFI` |
| `-LoaderDir` | `UnlockLoader-<Arch>.efi` 所在目录（默认 `build\`） |
| `-FirmwareRoot` | 下面有 `Turing\`、`Ampere\`、`Ada\` 的目录（默认 `assets\firmware\`） |
| `-TimeoutSec 20` | 写入 `config.ini` 的菜单秒数 |
| `-DefaultAction skip` | `skip` 或 `inject` |

脚本拒绝不在 USB 上的卷，也拒绝启动盘和系统盘。它会替换该盘符上的 `EFI\BOOT\BOOTX64.EFI`、`UnlockLoader-*.efi` 和 `EFI\turing-unlock\firmware\`。其它文件留下。同时写入 `config.ini` 和标记 `EFI\turing-unlock\usb-boot`。

在固件启动菜单里选这块 U 盘。Secure Boot 保持关闭。

### 启动菜单

```
GPU Unlock Loader 0.2.12 (Turing)

NVIDIA display GPUs: 1
  0000:65:00.0  10de:1e02  Turing
    PLM=fffffff8  SS0=00000000  SS1=00000007  RO=00000000

 > Skip inject
   Inject
   Boot now

Up/Down, Enter to confirm  (20 sec)
```

上下键移动高亮，回车确认。在你按键之前，倒计时结束会启动当前高亮的那一行。默认是 Skip，除非 `config.ini` 里写了 `default=inject`。U 盘上这三个选项之后都进入同一张列表。那张列表没有倒计时。上下键移动，回车启动高亮的加载器，Esc 回到固件。

```
Select a boot target
Up/Down, Enter to start, Esc for firmware

 > Windows Boot Manager
   ubuntu

\EFI\Microsoft\Boot\bootmgfw.efi
NVMe ns=1 part=1
```

列入的文件是 x64 UEFI 应用程序，并且不带这份 loader 的 magic。Windows 贡献 `\EFI\Microsoft\Boot\bootmgfw.efi`。其它每个 `\EFI\<名字>\` 目录贡献 `shimx64.efi`，没有 shim 时用 `grubx64.efi`，有 `systemd-bootx64.efi` 时再加一条。`\EFI\BOOT\bootx64.efi` 只在这一卷没有别的加载器时列入。USB 卷会跳过。每一行带 NVMe、SATA、ATAPI 或 SCSI 节点，用来分开两块盘。

`I` 也会注入本架构仅 readout 的 ID，例如 Turing 编译下的 TITAN RTX `10de:1e02`。注入需要 U 盘上有该架构的 blob。

### 启动日志

选中的加载器启动前，U 盘上会有 `\EFI\turing-unlock\unlock.log`。PCI 扫描总会为每张卡写一行 `Found <seg:bus:dev.fn>  10de:<id>  <Arch>`（Skip 和 Inject 都有）。里面还有 `handoff`（PLM/SS0/SS1/readout、WPR2、boot、GSP/SEC CPUCTL、PCI command、BAR0）、`boot mode=usb`，以及注入后每张卡一行 `unlock 10de:<id> result=<errno>`。`inject finished status=0` 表示每张尝试过的卡都返回 0。失败时这条是 `status=8000000000000007`（`EFI_DEVICE_ERROR`）。启动项列表仍会出现。选中项记成 `boot choice <n> <title> <path>`。

日志留在 U 盘上。启动一个能读这块 FAT 的系统后把文件拷出来。过程记录都在这个文件里，屏幕上的一次性结果（选项、解锁结果、chainload 错误）也会写进去。菜单每秒重画一次，不会重复写进文件。要在屏幕上同时看到文件内容，把 `config.ini` 里的 `verbose` 改成 `1`。

如果资源管理器里一直显示着内部 EFI 系统分区，用管理员 PowerShell 删掉它的盘符。分区里的文件不动，U 盘也不会动。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Hide-GpuUnlockEsp.ps1
```

### Ada / RTX 40 系

公版 **RTX 4090**（`10de:2684`）是 Ada **基线** ID：用 `UnlockLoader-Ada.efi`，`targets=all`（或列出 BDF），**不需要**实验开关。Inject/启动支持仍有效；此前发布的 Windows GEMM 数字已**撤回**，等待重测（**需重新验证**）。

本树里其余 Ada ID 要开 config 开关；其中若干还必须写 **精确** BDF。摘要：

| 卡 | ID | 开关 | `targets` |
| --- | --- | --- | --- |
| RTX 4090 | `2684` | *（基线）* | `all` 或 BDF |
| RTX 4090 D | `2685` | `rtx4090d_experimental=1` | `all` 或 BDF |
| RTX 4080 | `2704` | `rtx4080_experimental=1` | **精确 BDF** |
| RTX 4080 SUPER | `2702` | `ad103_experimental=1` | **精确 BDF** |
| AD103 canary | `2705` | `ad103_experimental=1` | **精确 BDF** |
| AD106 | `2805` | `ad106_experimental=1` | **精确 BDF** |

- 解锁仅当前会话；重启后需再 Inject。
- Inject 测试之间请关闭 **快速启动**（设置界面或 `HiberbootEnabled=0`；键缺失时常见默认视为开启）。完整注册表步骤：[docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md#操作注意4080--主显示-gpu)。
- Ada Windows handoff（0.2.12+）致谢 [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker)（代码 GPL-2.0-only；其新写说明文档 CC BY-NC 4.0）。详见 [docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md#致谢ada-windows-handoff)。
- 各卡完整示例、取 BDF、`skip_flr`、快速启动 / `HiberbootEnabled`、TOTORONG 致谢与 4080 MMA 全表：**[docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md)**。

### 机器起不来

在固件菜单里启动内部磁盘，U 盘可以拔掉。USB 脚本没有改过那块盘。

如果用了下面的 ESP 安装，Windows 停在 `0xc0000428`，说明官方路径上还是未签名 loader。用 WinPE 或 Windows 安装盘进命令提示符，把签名备份拷回去。备份还在时不要格式化 ESP：

```
diskpart
list vol
sel vol <ESP 卷号>
assign letter=S
exit
copy /y S:\EFI\turing-unlock\bootmgfw_ms.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
```

ESP 通常是 FAT32，大约 100–500 MB。没有 `turing-unlock` 时，改为把 `S:\EFI\Microsoft\Boot\bootmgfw_ms.efi` 拷到官方文件名上。

格式化 ESP 再跑 `bcdboot` 是最后手段，只在备份和 BCD 都已经没了的时候用。同一份 WinPE 提示符里，确认 `S:` 是那块小的 FAT32 EFI 分区，`C:\Windows` 是真正的 Windows 卷之后：

```
format S: /fs:FAT32 /q
bcdboot C:\Windows /s S: /f UEFI
```

### 实测结果

两个脚本，尺寸都是 1024 / 2048 / 4096 / 8192。PyTorch 调用 cuBLAS。输入是 FP16 或 BF16，tensor core 做乘法，点积在内部用 FP32 累加。结果矩阵仍是输入的类型。`FP16→FP32` 指的是这条累加，不是把结果存成 FP32。

30 系和 40 系建议两个都跑：Skip 一次，注入后再一次，比较这张卡自己的 TFLOPS。Turing 没有 BF16 tensor core，20 系只跑 FP16 脚本。

```powershell
pip install torch
python .\bench\gemm_fp16.py --sizes 1024,2048,4096,8192 --repeat 80
python .\bench\gemm_bf16.py --sizes 1024,2048,4096,8192 --repeat 80
```

**TITAN RTX**（`10de:1e02`，完整 72 SM TU102），2026-10-25，Windows 10，驱动 617.42，PyTorch 2.9.0+cu130，CUDA 13.0，功耗墙 320 W。Loader `TU102 FEAT UNLOCKED`，`PLM=FFFFFFFF SS0=88888888 SS1=00000008`。表内是中位数 TFLOPS。2080 Ti 两列来自 Turing 的 Linux README（Dell RTX 2080 Ti 22GB @250W），不是本树测的。那两列的 100% 峰值是 56.9 TFLOPS。

| M.N.K | TITAN RTX 库存 | TITAN RTX 注入后 | 2080 Ti 解锁前 | 2080 Ti 解锁后 |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 44.2 (77.6%) | 44.8 (78.7%) | 50.3 (88.4%) | 68.3 (120.0%) |
| 2048 | 87.6 (153.9%) | 87.4 (153.6%) | 55.7 (97.9%) | 91.4 (160.6%) |
| 4096 | 95.4 (167.6%) | 95.1 (167.1%) | 53.7 (94.4%) | 81.7 (143.6%) |
| 8192 | 99.3 (174.5%) | 99.1 (174.2%) | 52.8 (92.8%) | 77.9 (136.9%) |

库存跑完：309 W，SM 1935 MHz，49 C。注入后：大约 285–290 W，SM 1920–1965 MHz，47–50 C。差值和跑次噪声相当。

**RTX 3070 Ti**（`10de:2482`，GA104），2026-10-27，Windows 10，驱动 617.42，PyTorch 2.14.1+cu132，CUDA 13.2。`GA102 FEAT UNLOCKED`，交接 `PLM=ffffffff SS0=88888888 SS1=8`。Windows 正常启动。启动过程中会黑屏大约 1 分钟，别慌。脚本的 `%peak` 列仍是 2080 Ti 的 56.9 TFLOPS 基准。

| M.N.K | skip | 注入后 | 倍数 |
| ---: | ---: | ---: | ---: |
| 1024 | 36.7 | 61.7 | 1.68x |
| 2048 | 42.5 | 75.2 | 1.77x |
| 4096 | 46.9 | 85.2 | 1.82x |
| 8192 | 48.5 | 86.3 | 1.78x |

Skip：SM 2010 MHz，大约 283–296 W。注入后：SM 1815 MHz，大约 309 W。两边显存时钟都是 9251 MHz。

**RTX 4080**（`10de:2704`，AD103），代测，loader **0.2.12** Windows Inject。Loader 打印 `AD102 FEAT UNLOCKED`，`SS0=88888888` `SS1=8`。Windows 正常启动。`mma_bench_sm89` 在 `f16/f32` / `bf16/f32` / `fp8/e4m3` 约 **2×**（对照项基本持平）。完整库存对比表与操作说明见 [docs/RTX40_Ada_zh.md](docs/RTX40_Ada_zh.md#mma-结果rtx-4080-0212)。本 SKU **不要**只靠 cuBLAS FP16/BF16 GEMM 当解锁证明。

**RTX 4090**（`10de:2684`，AD102）— **需重新验证。** Loader 0.2.7 打印 `AD102 FEAT UNLOCKED`，交接 `PLM=ffffffff SS0=88888888 SS1=8`；Windows 正常启动。本 SKU 此前发布的 Windows cuBLAS FP16 GEMM 数字已**暂时撤回**，等待重测（解锁后的 4080 0.2.12 MMA 已超过旧数字）。基线 Ada 配置（`2684`）不变。

在 Ada 上，寄存器驻留的指令发射率探针（如 `mma_bench_sm89`）与大尺寸 cuBLAS GEMM 经常差很多：GEMM 往往顶在功耗墙上。解锁证明请优先用发射率探针。

GeForce 2080 Ti 上的注入尚未在本树验证。Tensor core 满速功耗很高，可能损坏硬件。

### 使用时排障

| 现象 | 处理 |
| --- | --- |
| 启动菜单里没有这块 U 盘，或看不到 loader 菜单 | Secure Boot 仍开着，或固件启动菜单没有选这块 U 盘 |
| `firmware missing` / `load_fw error=-2` | 生成该架构的 blob，再跑一次 `New-GpuUnlockUsb.ps1` |
| 菜单不是 `0.2.12 (<Arch>)` | U 盘上还是旧 EFI。再跑一次 U 盘脚本 |
| 菜单里键盘没反应 | 0.2.3 及以后会在进菜单前连接 USB。用当前 EFI 重写 U 盘 |
| RTX 3070 Ti 注入后一直黑屏 | 等大约 1 分钟。实测启动就是这样，然后进入了 Windows |
| 列表里没有内部磁盘 | 固件没有在 NVMe 或 SATA 上挂出 FAT EFI 分区，或那里唯一的加载器就是这份未签名包装器 |
| ESP 安装之后出现 `0xc0000428` | 按 [机器起不来](#机器起不来) 把 `bootmgfw_ms.efi` 拷回去 |

---

## ESP 安装

这条路径会替换内部 ESP 上的 `\EFI\Microsoft\Boot\bootmgfw.efi`，并注册开机任务。先准备好 WinPE 或 Windows 安装盘。不想动这块分区时，用上面的 U 盘。

出厂 ESP 常见大约 100 MB。Turing 的 firmware 放得进现有 Windows 启动文件旁边。Ampere 和 Ada 各要一份大约 80 MB 的 `gsp_ga10x.bin`，出厂 ESP 在 `bootmgfw.efi` 旁边放不下。把 ESP 加大是你自己做的事，用你手头的分区工具即可，例如 MiniTool Partition Wizard。这里不写这些工具的用法。空间够了之后，Ampere 和 Ada 用同一条安装命令。

在目标 Windows 上用管理员 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Install-GpuUnlockBoot.ps1 -LoaderEfi C:\path\to\UnlockLoader-Turing.efi -FirmwareDir .\assets\firmware\Turing
```

脚本拒绝非管理员运行，也拒绝替换一个不是 PE 的 `bootmgfw.efi`。它把原件拷到 `EFI\turing-unlock\bootmgfw_ms.efi` 和 `EFI\Microsoft\Boot\bootmgfw_ms.efi`，写入 `config.ini`，再把 loader 装成 `bootmgfw.efi`。它注册计划任务 `GpuUnlock Boot Guard`，开机约 1 分钟后运行本目录里的 `tools\Protect-GpuUnlockBoot.ps1`。这个目录要留在磁盘上。删掉之后任务会指向不存在的脚本。挪走目录时，要从新路径再跑一次 Install。

进 Windows 之前，loader 把签名的 `bootmgfw.efi` 写回去再启动它。刚刚进入 Windows 的这一次没有 loader 菜单。任务再把包装器贴回去，下一次重启才会看到菜单。这条路径没有启动项列表，只启动本卷上的 Windows。

| 参数 | 含义 |
| --- | --- |
| `-TimeoutSec 20` | 菜单秒数 |
| `-DefaultAction skip` | `skip` 或 `inject` |
| `-AlsoFallback` | `\EFI\Boot\bootx64.efi` 与原版 bootmgfw 相同时一并替换 |
| `-SkipScheduledTask` | 不注册守护任务 |
| `-FirmwareDir <dir>` | 把该架构的 blob 拷进 ESP |

换了 EFI 之后要再跑一次 Install，否则 ESP 上还是旧 loader。

Windows 还能进时，管理员运行 `.\tools\Uninstall-GpuUnlockBoot.ps1` 会还原 `bootmgfw.efi` 并删掉任务。`-Purge` 还会删除 `EFI\turing-unlock\`，包括 firmware blob。

`Copy-GpuUnlockLog.ps1` 把内部 ESP 上的 `unlock.log` 拷到 `logs\`。U 盘的日志留在 U 盘上，这个脚本不读 U 盘。

ESP 上正常跳过大致是：

```
Inject skipped. Continuing to boot.
restoring signed bootmgfw.efi from \EFI\Microsoft\Boot\bootmgfw_ms.efi (...)
restore ok: \EFI\Microsoft\Boot\bootmgfw.efi (...)
Starting EFI loader...
```

| 现象 | 处理 |
| --- | --- |
| 固件直接进 Windows，没有菜单 | loader 刚把微软原件写回去时，这一轮没有菜单是预期。下次重启应出现。若仍没有：关 Secure Boot，或跑 `Protect-GpuUnlockBoot.ps1 -RegisterTask` |
| 进一次 Windows 后菜单消失 | 等约 1 分钟（看 `C:\ProgramData\GpuUnlock\protect.log`），或重新注册任务 |
| `0xc0000428` | 官方路径上还是未签名 loader。按 [机器起不来](#机器起不来) 拷回备份 |

---

## 从头编译

在装有 Visual Studio 的 Windows 上编译。这台机器不必插着那张 GPU。

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10/11 x64 |
| Visual Studio 2022 | 工作负载 **使用 C++ 的桌面开发**，MSVC v143 x64 |
| Python | 3.6+（`py -3` 或 `python` 在 PATH 上）。生成 firmware 还要 `pip install zstandard` |
| Git | 用来克隆 EDK2 |
| 磁盘 | EDK2 完整树大约数 GB |

只要装了 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`，Build Tools 也可以。`vswhere` 能扫到即可。

命令默认当前目录是本仓库根（含 `GpuUnlockPkg` 和 `tools` 的那一层）。

### 下载工具链

构建脚本在这些位置找 `edksetup.bat`：`.\edk2`、`C:\edk2`、`C:\src\edk2`、`%USERPROFILE%\edk2`、`%USERPROFILE%\src\edk2`，或 `EDK2_PATH` / `-Edk2Path`。

使用 **edk2-stable202608** 或更新。DSC 会链接 `CompilerIntrinsicsLib`，并且必须拉 submodule。

- `edk2-stable202408` 没有这个库，`subhook` 仍指向已经不存在的 `github.com/Zeex/subhook`。
- `edk2-stable202411` 有这个库，subhook 也改到了 `tianocore/edk2-subhook`，但 `CompilerIntrinsicsLib/*_ms.c` 在 x64 上把 `size_t` 定义成 32 位，编译报 `C2371`。
- `edk2-stable202511` 及之后把 `size_t` 定义成 `unsigned __int64`。这是第一个能用 VS x64 编过本包的 stable 标签。

克隆本身不会生成 `BaseTools\Bin\Win32\GenFw.exe`。编本包之前要先编一次 BaseTools：打开 **x86 Native Tools** 提示符，运行 `edksetup.bat Rebuild`。

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

EDK2 即使编本应用也会汇编 MdePkg 里的 `.nasm`。下载 NASM **win64** 包（2.15.05 以上即可，本树按 3.02 写的）：

- https://www.nasm.us/
- https://www.nasm.us/pub/nasm/releasebuilds/3.02/win64/nasm-3.02-win64.zip

解压后要有 `nasm-3.02\nasm.exe`。放在 `PATH` 上，或用 `NASM_PREFIX` / `-NasmPrefix` 也可以。

```powershell
git --version
py -3 --version
.\nasm-3.02\nasm.exe -v
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
```

### 准备和编译

`tools\Prepare-GpuUnlock.ps1` 检查本机工具和源码目录，再把本包链接或复制进 EDK2，并暂存已经放好的 firmware。`-Arch` 为 `Turing`（默认）、`Ampere` 或 `Ada`。一次编译只带一个注入后端。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Prepare-GpuUnlock.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Build-GpuUnlockEfi.ps1 -Arch Turing
```

Prepare 检查 Windows x64、VS2022 C++（17.x）、Python 3.6+、NASM 2.15.05+、git，以及 EDK2（`edksetup.bat`、`MdePkg`、`MdeModulePkg`、`MdeLibs.dsc.inc`、`CompilerIntrinsicsLib`、BaseTools）。它还检查公共 loader、三个 `Arch\` 目录、头文件、tools、`assets`、`bench`、选中的 INF，以及 `VERSION`、`TU_LOADER_VERSION`、`Loader.inf`、`GpuUnlockPkg.dec` 里的版本字符串。`BASE_NAME` 仍是 `TuringUnlockLoader`。本机输出是 `UnlockLoader-<Arch>.efi`。

它建立 junction `edk2\GpuUnlockPkg` → 本目录 `GpuUnlockPkg`（`mklink /J` 失败则整包复制），清掉旧的 `edk2\TuringUnlockPkg` junction，补上缺少的 `Conf\*.txt`，并创建 `.\build`。

```powershell
.\tools\Prepare-GpuUnlock.ps1 -FetchEdk2
.\tools\Prepare-GpuUnlock.ps1 -FetchNasm
```

所有 `[fail]` 修掉，直到打印 `Prepare complete.`

编译会先跑 Prepare（同一棵树已经成功时才用 `-SkipPrepare`），然后 `edksetup.bat VS2022` 和 `build -a X64 -t VS2022 -b DEBUG -p GpuUnlockPkg\GpuUnlockPkg.dsc`。EFI 拷到 `.\build\UnlockLoader-<Arch>.efi`。

```powershell
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ampere
.\tools\Build-GpuUnlockEfi.ps1 -Arch Ada
.\tools\Build-GpuUnlockEfi.ps1 -Arch Turing -Edk2Path C:\edk2 -NasmPrefix .\nasm-3.02\ -Target RELEASE -ToolChain VS2022
```

`iasl`、CLANG、Cygwin 的提示可以忽略。成功后再编一次是增量的。菜单里的版本必须和 `GpuUnlockPkg\Include\TuConfig.h` 里的 `TU_LOADER_VERSION` 一致（现在是 `0.2.12`）。

Ada 用 AD102 的 Linux 事务（four-PLM V67，然后 FLR）。Ampere 用 GA102 的 Linux 事务（V67、选择器，然后 FLR）。Turing 写选择器，不做 FLR。

先不替换 bootmgr，用 EFI shell 冒烟：

```
fs0:
cd EFI\turing-unlock
TuringUnlockLoader.efi noload
```

`noload` 停在菜单之后。`inject` / `skip` 跳过菜单。

本地编译时，firmware 的生成命令和 [取得 EFI 和 firmware](#取得-efi-和-firmware) 相同。`tools\Build-GpuUnlockFirmware.ps1` 会跑 `firmware\<arch>\fetch_artifacts.py`。URL 和 SHA-256 写在 `firmware\<arch>\artifacts-610.43.03.json`。哈希对不上的文件不会收下。下载的东西不会拿来执行。

| 输入 | 来源 | 用途 |
| --- | --- | --- |
| `NVIDIA-kernel-module-source-610.43.03.tar.xz` | NVIDIA open kernel module 源码 | Booter / GSP boot / SEC2 镜像 |
| `nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst` | 该包的 CachyOS 存档 | `gsp_tu10x.bin` 或 `gsp_ga10x.bin`，以及 `NVIDIA-LICENSE` |

V67 payload 在本地生成，不是 NVIDIA 可再分发文件。缓存：`.cache\firmware\`（gitignore）。不要提交 `*.bin` 或 `NVIDIA-LICENSE`。

### 清理

删除本机 EFI 拷贝和 EDK2 的 `Build\GpuUnlock*` 目录。不会卸载 ESP 上的安装，也不会删除 EDK2 或 NASM。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Arch Turing
powershell -ExecutionPolicy Bypass -File .\tools\Clear-GpuUnlock.ps1 -Firmware
```

`-Firmware` 还会删掉 `assets\firmware\`。`-WhatIf` 只打印路径。

### Hyper-V 虚拟机

在没有 NVIDIA GPU 的第二代虚拟机里测过安装、跳过和进入 Windows。没有在虚拟机里做实机注入。

1. 建第二代虚拟机，安装 Windows 10/11 x64。
2. 关机。设置 → 安全 → 取消 **启用安全启动**。
3. 把 `build\UnlockLoader-Turing.efi` 和 `tools\` 拷进虚拟机。
4. 在虚拟机里用管理员 PowerShell 跑 ESP 安装。
5. 重启。菜单应显示 `GPU Unlock Loader 0.2.12 (Turing)`。回车或超时后应进入 Windows。

没有直通 GPU 时，Inject 打印 `gpu_count=0`，不写 MMIO。

### 编译排障

| 现象 | 处理 |
| --- | --- |
| Prepare `[fail] NASM` 或 `[fail] EDK2` | `Prepare-GpuUnlock.ps1 -FetchNasm` 和/或 `-FetchEdk2` |
| 构建缺 NASM | 解压出 `nasm-3.02\nasm.exe`，或传 `-NasmPrefix` |
| `LNK2001 memset` | 用本树重新编译。DSC 已链接 `CompilerIntrinsicsLib` |
| `memset_ms.c` / `memcpy_ms.c` 报 `C2371` `size_t` | 换 `edk2-stable202608`。202411 在 x64 上把 `size_t` 定义成 32 位 |
| 缺 `GenFw.exe` | 在 x86 Native Tools 提示符里运行 `edksetup.bat Rebuild` |
| `error 0004: File read failure` 指向 `Loader.inf` | INF/DSC/DEC 必须是 ASCII。EDK2 按系统 ANSI 代码页打开它们 |
| 编译成功但仍打印 `Cannot find BaseTools Bin Win32` | 当前 tools 下这行无害。`EDK_TOOLS_BIN` 会在 `edksetup.bat` 之前设好 |

### 布局

```
.                          本目录 = 仓库根
  GpuUnlockPkg/
    Application/Loader/    菜单、chainload、PCI
    Arch/Turing/           TU102 注入（-Arch Turing）
    Arch/Ampere/           GA102 注入（-Arch Ampere）
    Arch/Ada/              AD102 注入（-Arch Ada）
    Include/               公共头文件
  tools/                   Prepare / 编 EFI / 编 firmware / Clear / Install / U 盘 / Uninstall / Protect / 拷日志 / 隐藏 ESP 盘符
  logs/                    从 ESP 拷出的 unlock.log（gitignore）
  firmware/                分架构 blob 生成器（turing、ampere、ada）
  assets/config.ini
  assets/firmware/<Arch>/  生成的 blob（gitignore）
  bench/gemm_fp16.py
  bench/gemm_bf16.py
  build/                   UnlockLoader-<Arch>.efi（gitignore）
  nasm-3.02/               本地下载，不入库
  edk2/                    本地下载，不入库
  LICENSE                  GPL-2.0-only
  NOTICE                   署名
  VERSION                  0.2.12
  README.md                英文
  README_zh.md             中文
```

---

## 许可证

本树源码是 **GPL-2.0-only**，与三个上游 Linux 模块相同。见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。

NVIDIA firmware 仍受 NVIDIA 许可约束，不在 git 里。

---

## 更新记录

`0.2.12` 按 TOTORONG AD103 Windows unlocker 调整 handoff：Inject 前尽量 LoadImage（preload）`bootmgfw`；保留 FLR 前 `driver_release`；FLR 后 `ConnectController` 重连 GOP，再观察选择器，最后 StartImage 预加载镜像（失败则回退到原来的 LoadImage 路径）。Ada/Ampere 成功路径均写 `post-GOP-reconnect` 日志。解锁 payload 未改。

文档（同版本树）：补充 RTX 4080 在 0.2.12 inject 上的 MMA ~2× 发射率验证（`10de:2704`，`skip_flr=0`）；实验性章节写明快速启动 / `HiberbootEnabled` 注册表、会话级解锁，以及用 `mma_bench_sm89` 做 Ada 解锁核对。

`0.2.11` 在 Ada/Ampere PCIe FLR 前增加控制台/GOP/display-child 断开（Latence 风格 `driver_release`），以及可选 `skip_flr=1` 用来隔离主 GPU FLR 重启。Disconnect 覆盖 TextOut、GPU 下的 GraphicsOutput、每个 PciIo BY_CHILD（无 GOP 的显示子设备；上游 AD103 Windows #39 / #92），再断 GPU PCI 句柄。#92 上 Windows 2705 在 FLR/GOP 重连时空白约 1 秒——不是整机断电重启；快速启动应关闭。Ada 解锁事务相对 0.2.10 未改。

`0.2.10` 按 Linux `gpu-unlock-ada`（`unlock_610` / `gpu_unlock_command_restore`）加固 UEFI PCIe FLR / 配置恢复：Initiate FLR 之后选择性还原 BAR，COMMAND 恢复为 MEMORY on / BME off，核对 BAR + identity，并清 DevSta TRPND。identity 不一致时跳过盲目的 256 字节配置写。Ada 解锁事务未改。

`0.2.9` PCI 扫描在 skip 和 inject 的 `unlock.log` 里写入每张 GPU 的 BDF（`Found <seg:bus:dev.fn>  10de:<id>`）。0.2.9 之前的 skip 日志只有设备 ID。README 写明 RTX 4080 实验性操作：开关 + 精确 `targets` BDF（`targets=all` 永不放行 4080）。

`0.2.8` 为实验性 RTX 4080（`10de:2704`）和 RTX 4080 SUPER（`10de:2702`）增加 Ada 准入。两者都要 config.ini 开关（`rtx4080_experimental` / `ad103_experimental`）以及 `targets` 里的精确 PCI BDF（不能靠 `targets=all`）。已有实验性 Ada ID（`2685`、`2705`、`2805`）用同样风格的门。解锁路由用显式 `ad102_family()` 映射到固定的 AD102 资产；没有按 ID 的 fuse/SM/VBIOS 表。只读 readout 的 Ada ID 不再被强制注入。

`0.2.7` 用上下键选择注入、跳过和 U 盘上的启动项。过程日志写入 `unlock.log`。屏幕保留菜单、结果和错误。`verbose` 默认是 `0`。`tools\Hide-GpuUnlockEsp.ps1` 去掉内部 EFI 系统分区的盘符。

`0.2.6` 保留 ESP 路径：loader 所在卷上有签名 `bootmgfw` 备份时，仍然只在这块卷上还原该文件并启动 Windows。USB 模式（有 `usb-boot` 标记，且没有签名备份）会连接 NVMe 和 SATA 控制器，然后列出其它 FAT 卷上的 EFI 加载器，等用户输入序号。列表里可以有 Windows Boot Manager、发行版的 `shimx64.efi` 或 `grubx64.efi`，以及 `systemd-bootx64.efi`。某一卷上这些都没有时，才列入 `\EFI\BOOT\bootx64.efi`。注入、跳过和紧急启动都会进这张列表。选中的文件用那块卷自己的设备路径启动。这次扫描不写 `bootmgfw.efi`。`New-GpuUnlockUsb.ps1` 只往 FAT32 卷上拷文件，不会格式化任何磁盘。全系列都建议用 U 盘。ESP 安装仍然可用。

`0.2.5` 增加一条不改内部 ESP 的 U 盘启动路径，并修正 Ampere 的 SEC2 完成条件。chainload 前，loader 在该卷有签名 `bootmgfw` 备份时记 `boot mode=esp`，有 `\EFI\turing-unlock\usb-boot` 时记 `boot mode=usb`，两者都没有则是 `boot mode=unmarked`。

Ampere 遵循 Linux GA10x 事务：SEC2 成功是 feature PLM 变成 `0xffffffff`，不是之后再 halt。loader 0.2.4 在 GA104 RTX 3070 Ti（`10de:2482`）上已经打开 PLM，CPUCTL 仍是 `0`。多等的 5 秒 halt 返回 `-110`，SS0/SS1 停在出厂值，失败路径的 FLR 让 Windows 照常启动，GEMM 没有变化。Ada 仍然等待 halt，然后核对四组 AD102 权限字。写选择器之前，WPR_CFG `0x1fa7cc` 必须是 `0xfffff0ff`，WPR `0x1fa7c4` 和 feature PLM 必须是 `0xffffffff`。FBPA `0x9a0148` 只记录。这四组字在进固件前和 FLR 之后还会再采样。SEC2 启动前会把 V67 的 DMA 副本和文件再比一次。某张卡失败时日志是 `unlock 10de:<id> result=<errno>`，`inject finished status=` 不再是 `0`。chainload 仍会继续。3070 Ti 复测走的就是这条事务。那份日志横幅仍是 `version=0.2.4`（EFI 在版本字符串改成 0.2.5 之前编的）。

`0.2.4` 对应 [#1](https://github.com/mirabarukaso/nv_unlock_win/issues/1) 里的构建问题：EDK2 固定到 `edk2-stable202608`，仓库带上 `assets\config.ini`，loader 在启动下一级加载器前把一行 `handoff` 写入 `unlock.log`。
