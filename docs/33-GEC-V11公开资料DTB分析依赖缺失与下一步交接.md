# GEC V11 公开资料 DTB 分析依赖缺失与下一步交接

更新日期：2026-09-02

## 1. 本次操作

在 Ubuntu 24.04 中使用固定的 LubanCat 5.10.209 工作树、GecEdu 公开仓库 `4f9cd0af...` 的 factory DTS，
以及公开 `dtb_cmp2.py`，准备进行官方 EVB1 与 factory DTS 的只读三方分析。输入门禁全部通过，未修改 SDK。

## 2. 失败证据

隔离配置目录为 `/home/china/rkav-gec-dtb-analysis-20260902-203020`。执行
`rockchip_linux_defconfig` 时，内核 Kconfig 主机工具需要 `flex`，Ubuntu 返回
`/bin/sh: 1: flex: not found`，配置命令退出码为 2。主机同时没有 `dtc`，所以官方 EVB1 DTB 编译、
factory DTS 转 DTB、反编译和语义对比均未执行；脚本用 125 表示前置条件未满足。

失败目录已打包到共享目录：
`rkav-gec-dtb-analysis-20260902-203020.tar.gz`，SHA-256 为
`3e737f5a21cf56079ea73c763df16c1bb2caf4080fb53ee8a62101347c6e5954`。

## 3. 状态与边界

- 5.10.209 kernel 提交、device 提交和 GecEdu evidence 提交均已锁定。
- `kernel-5.10`、`device/rockchip` 工作区为空，`git diff --check` 通过。
- 未生成 DTB，未生成 GEC 候选 DTS，未构建 Image/rootfs，未连接或操作开发板。
- 这次失败是 Ubuntu 主机依赖缺失，不是 GEC DTS、内核版本或交叉工具链错误。

## 4. 下一步

在 Ubuntu 主机补齐 `flex`、`bison` 和 `device-tree-compiler` 后，从同一固定工作树重试同一隔离分析。
完成 DTB 对比前，不构建 GEC 镜像、不申请 5.10 板端启动，也不重跑旧 4.19 MPP/RGA 失败候选。
