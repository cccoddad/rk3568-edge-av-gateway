# GEC V11 隔离 SDK 视图与候选 defconfig 准备交接

更新日期：2026-09-03

## 1. 已完成的隔离准备

在 Ubuntu 24.04 的 ext4 文件系统中，已创建唯一目录：

```text
/home/china/rkav-gec-v11-sdk-view-20260903-112850
```

该视图不修改原始 LubanCat SDK：

- `device/rockchip` 由原 SDK 的提交 `a1642d64206d5afc54ad6c0c0f0232844f0e9e81` 本地克隆而来；
- `kernel-5.10` 是到既有隔离 kernel worktree 的链接；
- 该 kernel worktree 保留唯一已知未跟踪文件
  `arch/arm64/boot/dts/rockchip/rk3568-gec-v11-gmac1-candidate.dts`，SHA-256 为
  `631e67fab202ff8c1685855e29cf4277ce84f89033eeded95c36d25b701cafd6`；
- 原 SDK 的 device 工作树状态为空；没有创建或修改原 SDK 的 `output/`、`.chip` 或 `kernel` 链接。

隔离 device 克隆中新增的唯一候选文件为：

```text
.chips/rk3566_rk3568/rockchip_rk3568_gec_v11_gmac1_candidate_defconfig
```

其基线是官方 EVB1 defconfig，仅将 `RK_KERNEL_DTS_NAME` 改为：

```text
rk3568-gec-v11-gmac1-candidate
```

`RK_PARAMETER="parameter-buildroot-fit.txt"` 与 `RK_USE_FIT_IMG=y` 保持 EVB 基线值。

## 2. 已确认的 SDK 行为

`mk-config.sh` 会在首次配置时创建 `output/.config`、`output/defconfig`，并建立 `.chip` 链接；
`mk-kernel.sh` 按优先级选出 `RK_KERNEL_PREFERRED=5.10`，将 `kernel` 链接到 `kernel-5.10`，随后执行
`$KMAKE "$RK_KERNEL_DTS_NAME.img"`。因此这些写操作必须限制在隔离视图中。

## 3. 仍未验证与禁止事项

- 尚未在隔离视图执行 SDK 配置、`mk-kernel.sh`、kernel `.img` 构建、FIT/boot 镜像打包或 rootfs 构建；
- 未验证 SDK 正常路径是否能生成候选 `.img`，更没有得到可刷写升级镜像；
- 未部署、未连接或操作开发板，未启动网关；
- GEC 显示、存储、USB、Wi-Fi、摄像头和用户态 MPP/RGA/RKNN 的 5.10 匹配仍未完成，不能把本阶段描述为
  整板 5.10 迁移成功。

## 4. 下一步

只在该隔离视图执行显式 GEC defconfig 配置，然后走 SDK 正常 `kernel` 构建路径，保留命令、退出码、
候选 `.img`、DTB、日志和 SHA-256。只有该门禁通过，才评估受控的完整镜像打包；首次上板仍需用户确认。
