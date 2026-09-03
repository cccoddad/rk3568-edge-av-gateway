# GEC V11 GMAC1 候选正式 kernel DTB 构建与 Image 前门禁交接

更新日期：2026-09-03

## 1. 隔离工作树

在 `/home/china/rkav-gec-v11-kernel-candidate-20260903-093839/kernel-5.10` 创建固定提交
`6a5f8ede8...` 的 detached Git worktree。原始 SDK 的 `kernel-5.10` 在创建前和结束后均为干净状态，
`git diff --check` 通过。候选 worktree 中唯一未跟踪文件为本轮新建的
`arch/arm64/boot/dts/rockchip/rk3568-gec-v11-gmac1-candidate.dts`，符合隔离修改预期。

## 2. 正式 DTB 门禁

使用 SDK AArch64 GCC/LD 与独立 `O=` 输出目录：

- `rockchip_linux_defconfig`：退出码 0；
- kernel 目标 `rockchip/rk3568-gec-v11-gmac1-candidate.dtb`：退出码 0；
- 正式 DTB 反编译：退出码 0；
- 正式 DTB SHA-256：`2d6daaff1b56e1eda5b2ac22162be5cc7d0128406f56a5ada45e24380394818a`；
- 反编译结果确认 GEC V11 model/compatible、GMAC0 disabled、GMAC1 `clock_in_out = "input"`、
  GPIO3 PB5 复位和 `tx_delay/rx_delay = 0x41/0x1e`。

候选源 DTS SHA-256 为
`631e67fab202ff8c1685855e29cf4277ce84f89033eeded95c36d25b701cafd6`，与独立 DTC 验证输入相同。

## 3. Warning 边界

正式反编译日志仍含 EVB1 基线的 unit-address、graph-child-address 和保留内存命名 warning。候选仅覆盖
根 model/compatible 与已有 GMAC 标签，没有新增 unit-address、graph 节点或保留内存，因此这些 warning
不能归因于候选；仍将在 kernel Image/DTB 最终构建时保存并比较。

## 4. 下一步

在该隔离 worktree 的 Rockchip DTB Makefile 增加候选 DTB 条目，再用相同配置构建 `Image` 与候选 DTB。
此举只证明正式 kernel 构建链和候选 DTB 可生成；不构建 rootfs、不打包/刷写 boot 分区、不启动开发板。
Image 通过后，才读取 SDK 的打包脚本、分区与可恢复旧介质信息，制定首次上板步骤。
