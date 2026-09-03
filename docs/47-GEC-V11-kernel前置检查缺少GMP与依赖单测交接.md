# GEC V11 kernel 前置检查缺少 GMP 与依赖单测交接

更新日期：2026-09-03

## 1. 本次构建到达位置

在隔离 SDK 视图中再次执行 normal kernel 构建，以下前置项均已通过：

- 局部 Python shim 可见，版本 3.12.3；
- `/usr/bin/lz4` v1.9.4 的 `favor-decSpeed` 能力可见；
- 局部 `nproc` 输出 1，SDK 实际 make 命令为 `-j2`；
- 候选 defconfig 与 `RK_KERNEL_DTS_NAME` 保持正确；
- `rockchip_linux_defconfig` 返回成功并报告配置无变化。

## 2. 新阻塞与证据

`check-kernel.sh` 随后报告：

```text
Your gmp header is missing
Please install it:
sudo apt-get install libgmp-dev
```

构建仍未进入正式 Image/DTB 阶段，四个关键产物均不存在。证据为：

```text
evidence/sdk-kernel-lz4-20260903-160022/kernel.log
SHA-256 505744c2e7c479fa7a0701cee17326c34c7695c5ca112c466200417af9820e2e

evidence/sdk-kernel-lz4-20260903-160022/start-time.txt
SHA-256 f79dedad43c825c3a8de662fd616c8db44fffd810e60ece346d9d18d0556c0de
```

GMP 是构建期高精度数学库。本次问题是 Ubuntu 主机缺少开发头文件，与 GEC DTS、开发板硬件和板端
MPP/RGA/RKNN 运行库无关。

## 3. 保护状态与下一步

原 SDK 仍干净；隔离 device 与 kernel 只含预期候选 defconfig/DTS；未部署、未刷写、未操作开发板。

下一步安装 `libgmp-dev`，核验头文件与软件包版本，然后直接执行同一 SDK `check-kernel.sh` 的前置检查。
只有整个前置检查返回成功，才再次进入 normal kernel 构建，以减少重复等待。
