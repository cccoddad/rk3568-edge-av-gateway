# GEC V11 隔离 SDK normal kernel 构建失败与工具链门禁交接

更新日期：2026-09-03

## 1. 失败操作与证据

在隔离 SDK 视图 `/home/china/rkav-gec-v11-sdk-view-20260903-112850` 中，候选配置门禁通过后执行了
`./build.sh kernel`。这是主机侧构建，不是刷写、部署或板端启动。

构建前已确认：

```text
RK_DEFCONFIG="rockchip_rk3568_gec_v11_gmac1_candidate_defconfig"
RK_KERNEL_DTS_NAME="rk3568-gec-v11-gmac1-candidate"
```

日志保存为 `evidence/sdk-kernel-20260903-114521/kernel.log`，SHA-256 为
`f2edc84df7984c03f34560907d69cc4ffd61d83a0c381491c1bc30057c42f825`。内层 make 在
`rockchip_linux_defconfig` 返回 2；顶层仍显示 `shell_exit=0`，故该返回码不构成成功证据。候选 DTB 和
`output/firmware/boot.img` 均不存在，门禁结论为 `KERNEL_GATE=FAIL`。

## 2. 已定位的根因（修正）

SDK kernel helper 自动选择：

```text
aarch64-none-linux-gnu-gcc
```

表面上 kernel `scripts/gcc-wrapper.py` 使 `scripts/Kconfig.include` 报 “unknown assembler invoked”。但
两套 SDK GCC 直接传给 `as-version.sh` 均得到 `GNU 23601`，且都解析到同一 SDK assembler；直接运行
`gcc-wrapper.py` 对两者均返回 127，stderr 为 `/usr/bin/env: python: No such file or directory`。该 wrapper
的 shebang 依赖 `python`，而 Ubuntu 24.04 只有 `python3`；`as-version.sh` 丢弃 stderr 后才显出误导性的
汇编器错误。失败发生在设备树编译之前，因此不能归因于候选 DTS、GMAC1 配置或开发板硬件。

因此不能把另一套 `aarch64-rockchip1031-linux-gnu-gcc` 当成修复方案。下一步验证方向是隔离视图中
`python -> python3` 的局部兼容 shim，先复测同一 wrapper，不修改系统或替换 SDK 默认 GCC。

## 3. 并发偏差

即使调用前设置 `MAKEFLAGS=-j2`，SDK 实际执行仍带 `-j3`。源码已确认 `kernel-helper` 使用
`-j$(( $(nproc) + 1 ))`，因此 2 核虚拟机产生 `-j3`。外部 MAKEFLAGS 不可靠。此次失败发生在早期配置阶段，
不由并发引起；后续将在隔离视图提供局部 `nproc` 限流 shim，而不修改 SDK 原脚本或系统级命令。

## 4. 保护状态与下一步

- 原 SDK device 工作树保持干净；
- 隔离 device 克隆仅有候选 defconfig；
- 候选 kernel worktree 仅有预期候选 DTS；
- 未生成可刷写完整镜像，未部署、未操作开发板。

隔离视图已建立 `python -> python3` 与返回 1 的 `nproc` 局部 shim；同一 `gcc-wrapper.py` 配合同一 SDK
默认 GCC 的复测返回 0。下一步进行一次携带该 shim 的受控 normal kernel 构建重试。
