# GEC V11 隔离 SDK 候选配置通过与 kernel 构建门禁交接

更新日期：2026-09-03

## 1. 配置门禁已通过

在隔离 SDK 视图 `/home/china/rkav-gec-v11-sdk-view-20260903-112850` 中，使用显式命令：

```text
./build.sh rk3566_rk3568:rockchip_rk3568_gec_v11_gmac1_candidate_defconfig
```

SDK 成功建立 `.chip`，并生成 `output/.config` 与 `output/defconfig`。配置结果为：

```text
RK_DEFCONFIG="rockchip_rk3568_gec_v11_gmac1_candidate_defconfig"
RK_KERNEL_ARCH="arm64"
RK_KERNEL_CFG="rockchip_linux_defconfig"
RK_KERNEL_DTS_NAME="rk3568-gec-v11-gmac1-candidate"
RK_KERNEL_DTS_DIR="kernel/arch/arm64/boot/dts/rockchip"
```

配置日志路径为 `evidence/sdk-config-chip-20260903-114125/config.log`，SHA-256 为
`4c5461b0eff00818cbfd8dbc17e0bec5256addc09f22d83c6d99568ba00957a7`。日志没有 `ERROR`、`FATAL` 或
“No available defconfig”线索，配置门禁为 `PASS`。

日志开头的 `.chip` `find` 提示发生在链接建立前的脚本扫描阶段；其后明确打印了“Switching to chip:
rk3566_rk3568”和“Switching to defconfig”，不构成配置失败。Kconfig 主机工具的 `sprintf` 编译告警来自
SDK 配置工具本身，未阻断输出 `.config`，且与候选 DTS 无关。

## 2. 保护状态

- 原 SDK `device/rockchip` 工作树仍干净；
- 隔离 device 克隆仅有候选 defconfig；
- 候选 kernel worktree 仅有预期的候选 DTS；
- 本阶段未编译 kernel、未生成 `.img`、未打包 FIT/boot/rootfs 镜像，未部署或操作开发板。

## 3. 下一步

只在此隔离 SDK 视图执行 `./build.sh kernel`，先核对 SDK 实际并发参数，再保留完整日志、退出状态、
候选 `.img`、候选 DTB、`output/firmware/boot.img` 链接与 SHA-256。此操作是主机侧 kernel 构建，仍不是
刷写或板端启动；若构建失败，先保留证据并停止，不进入完整镜像打包。
