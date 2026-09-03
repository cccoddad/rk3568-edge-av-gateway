# GEC V11 完整 kernel 前置检查通过与实编交接

更新日期：2026-09-03

## 1. 依赖安装结果

Ubuntu 一次确认安装 SDK `check-kernel.sh` 明确列出的主机依赖。新增软件包为：

- `libmpc-dev` `1.3.1-1build1.1`；
- `libmpfr-dev` `4.2.1-1build1.1`；
- `libncurses-dev` `6.4+20240113-1ubuntu2.2`。

`flex`、`lz4`、`libssl-dev` 和 `libgmp-dev` 已是满足要求的版本。安装退出码为 0。

## 2. 验证结果与证据

OpenSSL、GMP、MPC、ncurses 的最小头文件编译探针全部返回 0；随后在固定 host shim PATH 下直接执行
SDK `check-kernel.sh`，退出码为 0，最终 `ALL_KERNEL_PRECHECK_GATE=PASS`。

证据目录为 `evidence/sdk-kernel-precheck-all-20260903-160912`：

- 安装日志 SHA-256：`24b7748640f5181a643abab23dc343f5771a4f01a1d456411ae40b5de0a45911`；
- 软件包版本清单 SHA-256：`9614d43896cb8fc4891dc1e4d463b764138f2da88b814fe2cde9aa97a549f47d`；
- 前置检查日志为空，SHA-256 为标准空文件值
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`。

空日志表示脚本没有打印错误并以 0 退出，不表示证据丢失；退出码、头文件探针和包版本清单共同构成门禁。

## 3. 当前边界与下一步

原 SDK device 工作树仍干净；隔离 device/kernel 仅有预期候选 defconfig/DTS。尚未在完整前置检查通过后
重跑 normal kernel，因此仍没有新的 Image、候选 DTB 或 boot 产物；未部署、未刷写、未操作开发板。

后续使用固定 Python 与 `nproc=1` shim，以实际 `-j2` 完成 normal kernel 构建，结果见第 50 号交接；
完整开发板 5.10 业务迁移仍不是本阶段已完成能力。
