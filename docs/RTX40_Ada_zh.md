# Ada / RTX 40 系（Windows）

Ada 解锁复用 **AD102-family** firmware payload（与公版 RTX 4090 相同的一组 blob），没有按 SKU 单独的 fuse/SM/VBIOS 表。Loader 会不会解锁某张卡，由 `config.ini` 的开关和 `targets` 决定。

返回主 [README](../README_zh.md)。已验证的 4080 MMA 数字与操作说明都在本文（代友测试 / 代发）。

## 已验证摘要（loader 0.2.12）

| GPU | PCI ID | 结果 |
| --- | --- | --- |
| RTX 4090 | `10de:2684` | Ada 基线路径。Windows Inject 正常。**性能需重新验证** — 此前发布的 Windows GEMM 数字已**撤回**（解锁后的 4080 0.2.12 MMA 已超过旧的 4090 数据）。 |
| RTX 4080 | `10de:2704` | 实验路径。需 `rtx4080_experimental=1` + 精确 BDF。`mma_bench_sm89`：`f16/f32` / `bf16/f32` / `fp8/e4m3` 约 **2×**，对照项基本持平。 |

解锁只在**当前会话**有效。重启或断电后会清掉。

## 本树里的 PCI ID（来自 `GpuUnlockPkg/Arch/Ada`）

| 卡 / 说明 | Device ID | Config 开关 | `targets` 规则 | 代码状态 |
| --- | --- | --- | --- | --- |
| **RTX 4090** | `10de:2684` | *（无 — Ada 基线 ID）* | `all` 或列出的 BDF | 支持 / Inject 正常；**性能需重新验证** |
| **RTX 4090 D** | `10de:2685` | `rtx4090d_experimental=1` | `all` 或列出的 BDF | 实验性 opt-in；**本树未实测** |
| **RTX 4080** | `10de:2704` | `rtx4080_experimental=1` | **必须写精确 BDF**（`all` 永不放行） | 实验性；0.2.12 MMA **已验证** |
| **RTX 4080 SUPER** | `10de:2702` | `ad103_experimental=1` | **必须写精确 BDF** | 实验性；此处未测 |
| **AD103 canary**（头文件中的 4070 Ti SUPER ID） | `10de:2705` | `ad103_experimental=1` | **必须写精确 BDF** | 实验 canary；此处未测 |
| **AD106**（头文件中的 4060 Ti 16GB ID） | `10de:2805` | `ad106_experimental=1` | **必须写精确 BDF** | 实验性；源码注明 **未在硅上测试** |

公版 **RTX 4090**（`2684`）是唯一的 Ada **基线** ID：不需要实验开关。表里其余都要开对应 flag。Exact ID（`2704` / `2702` / `2705` / `2805`）还必须把 PCI 地址写进 `targets`；即便开了开关，`targets=all` 也不会解锁。**4090 D** 是非 exact 实验项：开开关后 `targets=all`（或列出 BDF）即可。

- **共用 U 盘 / 其他代卡：** Ada 实验开关默认 `0`，Turing/Ampere loader 不读取；`skip_flr=0` 即照常 FLR。`targets` 写精确 BDF 只针对 exact 实验 ID（4080/4080 SUPER 等）。Turing、Ampere 和公版 4090 用 `targets=all`（或该卡自己的 BDF），别把 4080 的 BDF 留在给其他卡用的 U 盘上。

上表以外的 Ada 零售 ID（例如多数 4070 / 4060 SKU）**不在**准入列表。不要自行假设支持。

## `config.ini` 示例

**RTX 4090**（基线 — 若 `targets=all`，默认 U 盘配置通常已够；**性能需重新验证**）：

```
targets=all
# 10de:2684 不需要实验开关
skip_flr=0
```

**RTX 4090 D**：

```
rtx4090d_experimental=1
targets=all
skip_flr=0
```

**RTX 4080**（换成你的 BDF）：

```
rtx4080_experimental=1
targets=0000:01:00.0
skip_flr=0
```

**RTX 4080 SUPER / AD103 canary `2705`**：

```
ad103_experimental=1
targets=0000:01:00.0
skip_flr=0
```

**AD106 `2805`**：

```
ad106_experimental=1
targets=0000:01:00.0
skip_flr=0
```

## 操作注意（4080 / 主显示 GPU）

本节侧重在 RTX 4080 上验证过的主显示 GPU 操作。完整 ID/开关表见上文。

`10de:2704`（RTX 4080）和 `10de:2702`（RTX 4080 SUPER）是实验性 Ada ID。它们复用 AD102-family 的 payload，没有单独的 fuse/SM/VBIOS 表。准入需要 config 开关 **和** `targets` 里的 **精确** PCI BDF。即便开了开关，`targets=all` 也不会放行。

U 盘上 `config.ini` 示例：

```
rtx4080_experimental=1
targets=0000:01:00.0
skip_flr=0
```

把 `0000:01:00.0` 换成你这张卡的 BDF。4080 SUPER 用 `ad103_experimental=1`，不要用 `rtx4080_experimental`。

- 解锁只在**当前会话**有效。重启或断电后会清掉，下次启动要再选 Inject。
- Inject（0.2.12+）会尽量先 preload `bootmgfw`，再断开 TextOut/GOP/display child、Initiate FLR（`skip_flr=0` 时）、重连 GOP 并复查选择器，最后 StartImage 预加载镜像。正常 Windows 路径预期是控制台短暂空白（约 1 秒）后进入系统，不是再来一次完整 POST（见上游 #92 / TOTORONG AD103）。若仍整机重启，设 `skip_flr=1` 跳过 Initiate FLR（仍会 disconnect/reconnect），用来判断重启是否由 FLR 触发。
- **两次 inject 测试之间请关闭 Windows 快速启动。** 快速启动是混合关机；上一次 inject 的会话状态可能在“软重启”后看起来不对。
  - 界面：设置 → 系统 → 电源和电池 → 其他电源设置 → 选择电源按钮的功能 →（更改当前不可用的设置）→ 取消勾选 **开启快速启动**。
  - 注册表：`HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power\HiberbootEnabled`（DWORD）。`0` = 关闭快速启动，`1` = 开启。若该值**不存在**，常见消费级 Windows 会把快速启动当作**默认开启**。确认：

```
reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power" /v HiberbootEnabled
```

    强制关闭（管理员）：

```
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power" /v HiberbootEnabled /t REG_DWORD /d 0 /f
```

    能开界面就优先用界面；用注册表核对实际值。
- 用 Ada 指令发射率探针（`mma_bench_sm89`）验证解锁，不要只看 cuBLAS FP16/BF16 GEMM。大尺寸 GEMM 常顶在功耗墙上，注入后可能几乎不动甚至略降（本卡 0.2.8 的 Skip/Inject GEMM 对比是功耗墙噪声，即便当时 loader 已打印 `AD102 FEAT UNLOCKED`）。已验证的 0.2.12 MMA 数字见 [MMA 结果（RTX 4080，0.2.12）](#mma-结果rtx-4080-0212)。

**不点 Inject 时怎么拿到 BDF**

1. **加载器菜单（屏幕）。** GPU 列表里 `10de:2704` 旁边就是 `seg:bus:dev.fn`，例如 `0000:01:00.0`。菜单每秒重画，**不会**写进 `unlock.log`。
2. **Skip 的 `unlock.log`（0.2.9 起）。** PCI 扫描会写 `Found 0000:01:00.0  10de:2704  Ada`（以及 `turing_unlock: NVIDIA PCI 0000:01:00.0 10de:2704 ...`）。**0.2.8 及更早** 的 skip 日志只有 `NVIDIA PCI 10de:2704`，**没有** BDF — 那些文件请看菜单或下面的 Windows 方法。
3. **正常进 Windows 之后**（设备 ID `2704`）：
   - `nvidia-smi --query-gpu=pci.bus_id,name --format=csv` — bus id 形如 `00000000:01:00.0`，段号多出来的 0 去掉后写成 `targets=0000:01:00.0`。
   - 设备管理器 → 显示适配器 → 你的 GPU → 详细信息 → **位置信息**（PCI 总线 / 设备 / 功能）。段号一般是 `0000`。

改好 U 盘（或编辑 `EFI\turing-unlock\config.ini`）里的两行，关掉 Secure Boot 再启动，选 Inject。

### 致谢（Ada Windows handoff）

Loader **0.2.12** 的 Ada/Ampere Windows handoff（Inject 前 preload Windows Boot Manager；FLR 前 `driver_release` / TextOut+GOP+display-child 断开；FLR 后 GOP `ConnectController` 重连，再 StartImage）参考了 [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker) 公开的 Windows 路径。该项目 **代码为 GPL-2.0-only**（见其 `LICENSE`），**新增说明文档为 CC BY-NC 4.0**（见其 `LICENSE-POLICY.md`）。本树不附带其 EFI 或 firmware blob；解锁 payload 仍是既有的 AD102-family 资产。此处仅为致谢，不表示所有 4080/VBIOS 组合都适用。


## MMA 结果（RTX 4080，0.2.12）

**RTX 4080**（`10de:2704`，AD103 / AD102-family 路径），2026-10-29，Windows 11，驱动 617.42，功耗墙 320 W，76 SM，sm_89。Loader **0.2.12** Inject：`AD102 FEAT UNLOCKED`，`skip_flr=0`，`targets=0000:01:00.0`，交接 `PLM=ffffffff SS0=88888888 SS1=8`。探针：`mma_bench_sm89`（`iters=20000` `reps=3`），不是 cuBLAS GEMM。

库存 / 上一会话（无持久解锁）：

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

0.2.12 inject（`mma_bench_sm89`）：

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

| mode | 库存 best | inject best | 倍数 |
| ---: | ---: | ---: | ---: |
| f16/f16 | 195.5 | 200.1 | ~1.02×（对照） |
| f16/f32 | 98.4 | 200.0 | **~2.03×** |
| bf16/f32 | 97.8 | 199.8 | **~2.04×** |
| s8/s32 | 393.8 | 398.1 | ~1.01×（对照） |
| fp8/e4m3 | 195.8 | 403.0 | **~2.06×** |
| dp4a | 98.6 | 100.5 | ~1.02×（对照） |
| ffma | 47.4 | 48.3 | ~1.02×（对照） |

当 `f16/f32`、`bf16/f32`、`fp8/e4m3` 大约翻倍，而 `f16/f16` / `s8/s32` / `dp4a` / `ffma` 基本持平时，解锁可视为真实生效。不要只用 cuBLAS FP16/BF16 GEMM 当这张卡的解锁证明——本卡 0.2.8 的 Skip/Inject GEMM 对比是功耗墙噪声，当时 loader 已经打印过 `AD102 FEAT UNLOCKED`。

在 Ada 上，指令发射率探针与大尺寸 cuBLAS GEMM 经常差很多，因为 GEMM 往往顶在功耗墙上。解锁证明请优先用 `mma_bench_sm89`。本树里旧的 **RTX 4090** Windows GEMM 增量已撤回（**需重新验证**）；解锁后的 4080 0.2.12 MMA 已超过那些数字。

GeForce 2080 Ti 上的注入尚未在本树验证。Tensor core 满速功耗很高，可能损坏硬件。

请用 `mma_bench_sm89` 验证解锁，不要只看 cuBLAS FP16/BF16 GEMM。大尺寸 GEMM 常卡在功耗墙。上表即为 0.2.12 代测可发布记录（代友测试 / 代发）。
