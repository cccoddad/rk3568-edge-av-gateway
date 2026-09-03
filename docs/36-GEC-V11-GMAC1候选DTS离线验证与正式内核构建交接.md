# GEC V11 GMAC1 候选 DTS 离线验证与正式内核构建交接

更新日期：2026-09-03

## 1. 已验证的候选

在 SDK 工作树外创建目录 `/home/china/rkav-gec-v11-gmac1-candidate-20260903-093015`，以固定
5.10 `rk3568-evb1-ddr4-v10-linux.dts` 为 include 基线，生成只包含以下 GEC V11 有线管理改动的
候选 DTS：

- model：`Rockchip RK3568 GEC DDR4 V10 Board`；
- compatible：`rockchip,rk3568-gec-v11`、`rockchip,rk3568`；
- GMAC0：`status = "disabled"`；
- GMAC1：`clock_in_out = "input"`、复位 GPIO3 PB5、`tx_delay = 0x41`、`rx_delay = 0x1e`。

候选 DTS SHA-256 为
`631e67fab202ff8c1685855e29cf4277ce84f89033eeded95c36d25b701cafd6`。

## 2. 离线门禁结果

使用 SDK 自带 AArch64 GCC 预处理，再用 DTC 编译和反编译：

- 预处理：退出码 0；
- DTS -> DTB：退出码 0；DTB SHA-256：
  `2d6daaff1b56e1eda5b2ac22162be5cc7d0128406f56a5ada45e24380394818a`；
- DTB -> DTS：退出码 0；
- 反编译文本确认目标 model、GMAC0 禁用以及 GMAC1 的 `input`、GPIO3 PB5、`0x41/0x1e` 属性；
- SDK kernel Git 工作树 `status --short` 为空，`diff --check` 通过。

预处理日志为空。DTC 和反编译日志含有官方 EVB1 基线已有的重复 unit-address、图节点单子节点格式和
保留内存 unit 名称 warning；候选没有新增语法错误、引用解析失败或编译失败。warning 仍须在正式
kernel DTB 构建时再次对比，不能因为独立 DTC 成功就省略该门禁。

## 3. 未覆盖内容

本候选没有更改显示 route/1024x600 面板序列、USB VBUS、eMMC/SDMMC、Wi-Fi/蓝牙、MIPI 摄像头、
I2C 传感器、RGA/RKVENC/NPU 或用户态媒体库。它只能作为首次有线管理启动输入，不能宣称显示、
USB 摄像头、USB 音频、NPU 或 H.264 已在 5.10 上可用。

## 4. 下一步

在 kernel Git 的隔离 worktree 中加入候选顶层 DTS 和 `arch/arm64/boot/dts/rockchip/Makefile` DTB
条目；使用独立 `O=` 目录走正式 kernel DTB 构建，再比较正式 DTB 的 model/GMAC1 属性和 warning。
离线正式 DTB 通过后，才评估 kernel Image、boot 分区布局和可恢复的首次上板方案。

用户已授权后续板端操作，但开发板仍处于断电状态。首次板端动作前必须先保留 4.19 可恢复介质，
并只读检查遗留进程和当前系统状态；本次未部署或操作开发板。
