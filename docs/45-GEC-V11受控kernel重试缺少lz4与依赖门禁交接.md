# GEC V11 受控 kernel 重试缺少 lz4 与依赖门禁交接

更新日期：2026-09-03

## 1. 已通过的前置门禁

在隔离 SDK 视图 `/home/china/rkav-gec-v11-sdk-view-20260903-112850` 中，携带固定 host shim PATH 重试
`./build.sh kernel`。实际命令显示：

```text
make -C .../kernel/ -j2 ... rockchip_linux_defconfig
```

`rockchip_linux_defconfig` 成功写出 `.config`。这证明：

- `python -> python3` shim 已使 kernel `gcc-wrapper.py` 正常运行；
- `nproc=1` shim 经 SDK 的 `nproc + 1` 逻辑后实际产生 `-j2`；
- 候选 SDK 配置仍选择 `rk3568-gec-v11-gmac1-candidate`。

## 2. 本次实际失败

SDK 随后的 `check-kernel.sh` 在第 93 行调用 `lz4`，Ubuntu 返回 `command not found`。内层 hook 返回 1；
顶层 `shell_exit` 仍为 0，因此继续以日志和产物判断。构建在正式 Image/DTB 阶段之前停止，以下产物均不存在：

- `kernel/arch/arm64/boot/Image`；
- 候选 DTB；
- `kernel/boot.img`；
- `output/firmware/boot.img`。

证据日志为：

```text
evidence/sdk-kernel-shim-20260903-141621/kernel.log
SHA-256 f0a430fc80cfc7f9d929fc7eead73ed69976208125c1e9a006614d2ff1197736
```

## 3. 边界与下一步

原 SDK device 工作树仍干净；隔离 device 与 kernel 仅保留候选 defconfig/DTS。未部署、未刷写、未操作
开发板。

后续核对确认 `check-kernel.sh` 要求 `lz4 -h` 含 `favor-decSpeed`。Ubuntu 已有 `/usr/bin/lz4`，软件包
`1.9.4-1build1.1`、运行时 v1.9.4，门禁为 `PASS`；安装命令没有新增或升级软件包。下一步仍使用同一
host shim 和 `-j2` 约束进行一次受控重试。
