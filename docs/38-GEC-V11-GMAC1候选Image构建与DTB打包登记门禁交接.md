# GEC V11 GMAC1 候选 Image 构建与 DTB 打包登记门禁交接

更新日期：2026-09-03

## 1. Image 构建结果

在隔离 worktree `/home/china/rkav-gec-v11-kernel-candidate-20260903-093839` 的独立 `O=` 输出目录中，
执行 `make ... Image rockchip/rk3568-gec-v11-gmac1-candidate.dtb`，退出码为 0。

- AArch64 kernel Image：37102080 字节，SHA-256
  `8de018333ce12b174baa5d35d344f051b2e69a30baf737cc6fb454c91487b040`；
- 候选 DTB：185067 字节，SHA-256
  `2d6daaff1b56e1eda5b2ac22162be5cc7d0128406f56a5ada45e24380394818a`；
- 最终 DTB 反编译：退出码 0，model 与 GMAC1 候选属性仍正确。

长编译输出中的 `CC`、`AR`、`LD`、`OBJCOPY` 分别表示编译、归档、链接和提取内核镜像；最终出现
`arch/arm64/boot/Image` 且退出码为 0，说明 kernel Image 已生成，不是终端卡住。

## 2. P099：DTB 打包登记尚未闭合

候选 worktree 最终只显示新增候选 DTS，未显示 Rockchip DTS Makefile 修改。显式目标可由 kernel Make
直接构建，但常规 `dtbs`、SDK `build.sh` 和打包设备配置不一定会收集这个未登记文件。因此当前产物只可
作为离线构建证据，不能认定为可刷写镜像输入。

原 SDK 工作树未修改；未构建 rootfs、未打包、未部署、未操作开发板。末尾 `Ctrl+C` 发生在脚本的
“按回车后关闭终端”提示，所有构建、哈希和状态检查之后，不影响结果。

## 3. 下一步

只读定位：

1. kernel `arch/arm64/boot/dts/rockchip/Makefile` 实际 DTB 列表；
2. `device/rockchip` 与 `build.sh` 选择 kernel DTB/boot 映像的配置路径；
3. 现有 EVB1 打包使用的 DTB 文件名和设备配置。

确认后只在隔离 worktree 做候选 DTS 和 Makefile 的最小登记，使用普通 `make dtbs` 验证候选被默认
收集。通过前不构建 rootfs、不生成升级镜像，也不启动开发板。
