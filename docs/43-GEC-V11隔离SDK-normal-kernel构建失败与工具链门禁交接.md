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

## 2. 已定位的根因

SDK kernel helper 自动选择：

```text
aarch64-none-linux-gnu-gcc
```

kernel `scripts/gcc-wrapper.py` 对其报告 “unknown assembler invoked”，随后
`scripts/Kconfig.include` 报 “Sorry, this assembler is not supported”。失败发生在设备树编译之前，因此不能
归因于候选 DTS、GMAC1 配置或开发板硬件。

此前显式 DTB 和 Image 门禁使用 SDK 预置的另一套工具链
`aarch64-rockchip1031-linux-gnu-gcc` 并通过汇编器探测。这是下一步的验证方向，但尚未证明它是 SDK 的
正式选择方式。

## 3. 并发偏差

即使调用前设置 `MAKEFLAGS=-j2`，SDK 实际执行仍带 `-j3`。SDK helper 显式控制 jobs，外部 MAKEFLAGS
不可靠。此次失败发生在早期配置阶段，不由并发引起；但在后续重试前必须定位可配置的 jobs 参数。

## 4. 保护状态与下一步

- 原 SDK device 工作树保持干净；
- 隔离 device 克隆仅有候选 defconfig；
- 候选 kernel worktree 仅有预期候选 DTS；
- 未生成可刷写完整镜像，未部署、未操作开发板。

下一步只读检查 `kernel-helper`、构建 helper 和配置符号中有关 `RK_KERNEL_TOOLCHAIN`、`CROSS_COMPILE`、
`gcc-wrapper.py` 与 jobs 的来源。确认正规的覆盖点后，才在隔离视图进行一次受控重试。
