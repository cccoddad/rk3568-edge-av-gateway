# GEC V11 SDK DTB 选择链与隔离候选打包准备交接

更新日期：2026-09-03

## 1. 已定位的 SDK 选择链

LubanCat 5.10 SDK 的 RK3566/RK3568 芯片配置位于
`device/rockchip/.chips/rk3566_rk3568`。公开 EVB 基线文件
`rockchip_rk3568_evb1_ddr4_v10_defconfig` 定义：

```text
RK_KERNEL_DTS_NAME="rk3568-evb1-ddr4-v10-linux"
```

`device/rockchip/common/configs/Config.in.kernel` 将该名称与 `RK_KERNEL_DTS_DIR` 组合成内核 DTS/DTB
路径。`mk-kernel.sh` 使用 `$KMAKE "$RK_KERNEL_DTS_NAME.img"` 构建，并以同名 DTB 生成最终启动用的
`rk-kernel.dtb`；extboot 模式还会复制目录下的 DTB 集合。

因此内核的显式 DTB 目标虽可生成候选文件，但不能单独决定 SDK 打包实际选择哪个 DTB。此前尝试在
kernel DTS Makefile 中定位 EVB1 条目没有命中，也不应继续猜测该文件是唯一配置点。

## 2. 已确认边界

- 原 SDK kernel/device 工作树未修改；
- 隔离 kernel worktree 已有并已编译候选 DTS 与 Image；
- 尚未创建隔离 device worktree 或 GEC defconfig；
- 未执行 SDK `mk-kernel.sh`、未构建 rootfs/boot 镜像、未部署或操作开发板。

## 3. 下一步

只读核验 EVB defconfig 的所有 kernel/boot 变量、`Config.in.kernel` 的默认路径和 `build.sh` 的配置
初始化。随后创建唯一命名的 device Git worktree，复制 EVB defconfig 为 GEC 候选并仅替换
`RK_KERNEL_DTS_NAME`，再搭建只含 kernel/device 覆盖的隔离 SDK 视图。只有在 SDK 正常 `mk-kernel`
路径能生成并自检候选 DTB 后，才评估可恢复镜像打包和首次上板启动。
