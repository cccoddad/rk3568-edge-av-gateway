# GEC V11 候选 v2 构建通过与 fdtget 假失败交接

更新日期：2026-09-03

## 1. v1 证据保护

创建 v2 前，已把 v1 的 Image、DTB、FIT boot 和 firmware 链接文本复制到唯一证据目录
`evidence/iodomain-v2-20260903-170206`。前三项 SHA-256 仍分别为
`e66dca717b321e0c8c4b74aa67786cc97d3c642de7896af14eb6b4b7e9543b86`、
`2d6daaff1b56e1eda5b2ac22162be5cc7d0128406f56a5ada45e24380394818a` 和
`c0e91cb222b51e4bbbc2209ae6e99d8fe7cdfa0f9e9e273f746e6230d73b2569`。

## 2. 候选 v2 创建与构建

新 DTS `rk3568-gec-v11-gmac1-iodomain-candidate.dts` include v1，只覆盖 `vccio_acodec` 为 3300000 微伏，
并把 `vccio4/6-supply` 改为 `vcc_1v8`。源码 SHA-256 为
`d03b3215def67d171172f9b63b60cb70f61daf9cbd26e73b7ef221aa2f035f8c`。

独立 defconfig SHA-256 为 `15817a34680680fc63984c92f2cc721dd1b415e7a29542cbf90e81ac626aa9c2`。
patch 检查、应用和 SDK 配置门禁均返回 0；SDK 以 `-j2` 增量构建并报告 `build_kernel succeeded`。

v2 产物为：

| 产物 | 大小 | SHA-256 |
|---|---:|---|
| v2 DTB | 185067 | `dd5307628b670cf5aa9c921c5960f4f6b2807b4a23dd701c322dfb07ee1fb288` |
| Image | 37102080 | `e66dca717b321e0c8c4b74aa67786cc97d3c642de7896af14eb6b4b7e9543b86` |
| v2 FIT boot | 37520896 | `d40ee38f1a742ca61ffabec6245fbef7d97fa26fb59bc64dc24cb4f0c94278c5` |

## 3. 已通过的结构映射与假失败

从 v2 DTB `/__symbols__` 读取到：`vccio1` 与 acodec phandle 均为 `2f`，`vccio4/6` 与 1.8V phandle 均为
`31`，`vccio5/7` 与 3.3V phandle 均为 `32`。供电引用已符合 factory 目标。

但验证命令使用 `fdtget -td` 读取 regulator 微伏值。本机 fdtget 不支持 `d` 类型并两次报告
`invalid type string`，导致 min/max 为空，最终 `structured_gate=1`、`IODOMAIN_V2_GATE=FAIL`。这是验证
命令假失败，不是 DTS/构建失败。

## 4. 证据与下一步

配置日志、构建日志、结构值日志 SHA-256 分别为
`e69d3e42daabdb245aa38c44265697fecdef5f9f750c2d0385c71ae9f4bcd9ed`、
`8607d882b80030fd669ae9997bc7636ec34679a062b1831a8a6584af07b71135` 和
`86c1a6b0a59a6dbcfa97cfe822b4ad9bc8d8be296f6a62fd855528373dc2d6b7`。

下一步只对现有 v2 DTB 使用 `fdtget -tu` 读取 unsigned 微伏值，并重新执行结构门禁；不重新构建、不部署、
不刷写、不操作开发板。
