# GEC V11 host shim 复测通过与受控 kernel 重试交接

更新日期：2026-09-03

## 1. 已完成的复测

在隔离 SDK 视图中创建了唯一目录：

```text
/home/china/rkav-gec-v11-sdk-view-20260903-112850/host-shims-20260903-140236
```

该目录包含两个仅通过临时 `PATH` 生效的入口：

- `python` 链接到 `/usr/bin/python3.12`；
- `nproc` 固定输出 1，文件 SHA-256 为
  `c53ba0503053c8b4818b1b9a12722114cbb0f3ed2fa841e47875ff2c094609de`。

使用该 PATH 直接执行 kernel `scripts/gcc-wrapper.py` 和 SDK 默认的
`aarch64-none-linux-gnu-gcc`，wrapper 返回 0，输出 GNU assembler 2.36.1。日志位于：

```text
evidence/sdk-wrapper-shim-20260903-140236/wrapper-probe.log
SHA-256 870eec345632fdd1b6f827ea9756d752e7fa731a6850ea568a72d80b5b4d1d19
```

因此 P101 的根因与局部兼容方式均已验证；无需替换 SDK 默认 GCC，也未创建系统级 `python` 链接。

## 2. 并发约束

SDK `kernel-helper` 使用 `-j$(( $(nproc) + 1 ))`。局部 `nproc` 输出 1，因此携带同一 PATH 的下一次
kernel 构建应显示 `-j2`。这项限流还必须由实际构建日志确认，不能只凭算式判定已生效。

## 3. 保护状态与边界

- 候选 kernel 工作树仍仅有预期候选 DTS；
- 未修改原 SDK、Ubuntu 系统命令或系统 Python；
- 本阶段未编译 kernel、未打包、未部署、未刷写、未操作开发板。

## 4. 下一步

在隔离 SDK 视图中用固定的 host shim PATH 重试 `./build.sh kernel`，同时核验实际命令包含 `-j2`、日志
无错误、候选 DTB 与 boot 产物存在，并保存反编译属性和 SHA-256。若任一门禁失败，保留证据并停止。
