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

## 4. 失败时记录的下一步

以下是第一次失败时的原定动作，现已完成并由下一节记录结果。

## 5. 依赖补齐后的只读重试结果

已在 Ubuntu 主机安装并核验 `flex 2.6.4`、`bison 3.8.2` 和 `dtc 1.7.0`，随后从同一固定
工作树重新执行隔离分析。为绕过该内核树的 `scripts/gcc-wrapper.py` 对外部 assembler 的误判，
本次仅在命令环境中显式指定 SDK 自带的 `aarch64-rockchip1031-linux-gnu-gcc/ld`，没有修改 SDK。

- 分析目录：`/home/china/rkav-gec-dtb-analysis-20260902-205443`
- `rockchip_linux_defconfig`：退出码 0
- 官方 EVB1 DTB：退出码 0，185071 字节
- factory DTS 转 DTB：退出码 0
- 反编译与语义对比：退出码 0
- 对比摘要：官方 1060 节点、factory 709 节点、共有 676 节点；共有节点中 340 个存在语义差异
- 归档：`rkav-gec-dtb-analysis-20260902-205443.tar.gz`
- 归档 SHA-256：`34168ecf40be380e32418c56ddbfdf4fa922ad37b945feec000834b9a52eef83`

这一步证明 5.10.209 主机源码、DTC 工具链和公开 factory DTS 可以完成可重复的离线分析；它
没有证明 GEC V11 的最终 5.10 DTS 已经恢复，也没有证明任何板端驱动、显示链路、网口或媒体
功能已在新内核上运行。对比中显示、GMAC、I2C、SDMMC、USB VBUS、无线唤醒等差异应先按
“板级连线候选 / BSP 漂移 / DTB 生成伪差异”分类，再形成候选 DTS。

下一步仍只做 Ubuntu 侧只读分类和候选输入整理；不构建 Image/rootfs、不申请 5.10 板端启动，
也不重跑旧 4.19 MPP/RGA 失败候选。
