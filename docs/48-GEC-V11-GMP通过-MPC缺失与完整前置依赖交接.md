# GEC V11 GMP 通过、MPC 缺失与完整前置依赖交接

更新日期：2026-09-03

## 1. GMP 已通过

Ubuntu 成功新增 `libgmp-dev` 与 `libgmpxx4ldbl`，安装退出码为 0，额外占用约 1.7 MB。实际软件包版本为
`2:6.3.0+dfsg-2ubuntu6.1`；包含 `gmp.h` 的最小 C 编译探针返回 0。

证据目录为 `evidence/sdk-kernel-precheck-gmp-20260903-160444`，主要哈希为：

- 安装日志：`0688c591f12d3f176f41542bf3bdf02c95605b64e5bae58f60e42fc524ae3094`；
- GMP 探针对象：`8c5982ca4a0b31329d78cc8e3d747985c9f1072fffde5d05baacbb4346582595`；
- SDK 前置检查日志：`3c64b09c3868f070b74d3a275023a2fac4f46fefed12544e6d67b5ff3bec913c`。

## 2. 当前唯一已暴露阻塞

独立运行 `check-kernel.sh` 后，脚本在 MPC 头文件检查处返回 1，并提示安装 `libmpc-dev`。这证明 GMP
问题已解决，但整个前置检查尚未通过。

## 3. 已知完整检查范围与时间边界

完整 `check-kernel.sh` 已读取，明确检查：Python、flex、OpenSSL、GMP、MPC、ncurses 和 lz4。下一步批量
安装或确认对应 Ubuntu 包，再单独运行前置检查，不再为每个依赖重复进入 kernel 构建。该操作随后已完成，
完整前置检查通过，详见第 49 号交接。

当前仅以主机 normal kernel 构建为目标，完成度约 85%；依赖补齐后预计 30–90 分钟可得到 kernel/DTB/boot
构建结论。若目标是开发板安全启动 5.10 并恢复摄像头、音频、网口、NPU、MPP/RGA 等完整业务，当前约
35%，仍涉及完整板级 DTS、可恢复镜像、首次启动枚举和用户态 ABI 验证，顺利情况下预计 2–5 个工作日。

## 4. 边界

本阶段未重跑 kernel、未生成 Image/DTB/boot，未部署、未刷写、未操作开发板。原 SDK 工作树保持干净。
