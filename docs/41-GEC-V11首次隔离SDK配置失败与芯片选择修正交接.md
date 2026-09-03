# GEC V11 首次隔离 SDK 配置失败与芯片选择修正交接

更新日期：2026-09-03

## 1. 操作与失败证据

在隔离 SDK 视图 `/home/china/rkav-gec-v11-sdk-view-20260903-112850` 中，首次调用只传入
`rockchip_rk3568_gec_v11_gmac1_candidate_defconfig`。该视图和原 SDK 均未构建、未打包、未部署，开发板
未操作。

配置日志保存为：

```text
evidence/sdk-config-20260903-113438/config.log
SHA-256 d7357700041387ad1fff278ee82466c9ab9d748abd810ca2e335feaf4f506d5a
```

日志显示 `.chip` 不存在，`00-config.sh` 的 `init_hook` 失败，内部 exit code 为 1；随后缺失
`output/.config`、`output/defconfig`、`device/rockchip/.chip` 与顶层 `kernel`。这些产物缺失证明配置失败。

## 2. 原因与不可靠返回码

`mk-config.sh` 的 `choose_defconfig()` 需要先通过 `.chip` 知道芯片目录。单独传 defconfig 文件名时，首次
初始化没有芯片上下文，因而无法在 `.chips/rk3566_rk3568` 中发现候选。`build.sh` 源码注释明确支持
`芯片:defconfig` 形式。

本次外层经 `tee` 收集到的 `config_exit=0` 与日志中的内部 exit code 1 相冲突，不能依赖顶层返回码。后续
验收固定采用三项证据：日志无错误、目标配置产物存在且值正确、命令退出状态与前两项一致。

## 3. 保护状态

- 原 SDK `device/rockchip` 的 Git 状态为空；
- 隔离 device 克隆仅有未跟踪候选 defconfig；
- 候选 kernel worktree 仅有预期的未跟踪候选 DTS，SHA-256 为
  `631e67fab202ff8c1685855e29cf4277ce84f89033eeded95c36d25b701cafd6`；
- 未删除失败日志或隔离视图，未重试。

## 4. 下一步

先只在隔离视图中按 `rk3566_rk3568:rockchip_rk3568_gec_v11_gmac1_candidate_defconfig` 的显式格式执行配置，
检查生成的 `.chip`、`kernel`、`output/.config` 和 `RK_KERNEL_DTS_NAME`。通过后才可进行隔离 SDK 正常
kernel 构建；仍不打包完整镜像、不部署、不上板。
