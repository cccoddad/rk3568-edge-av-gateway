# GEC V11 DTB 可复核差异基线与板级候选分类交接

更新日期：2026-09-03

## 1. 可信基线

Ubuntu 24.04 使用固定 LubanCat 5.10.209 内核提交 `6a5f8ede...`、SDK 自带 AArch64 GCC/LD，
在独立目录 `/home/china/rkav-gec-dtb-rebuild-20260902-213048` 完成以下主机侧操作：

- `rockchip_linux_defconfig`：退出码 0；
- `rockchip/rk3568-evb1-ddr4-v10-linux.dtb`：退出码 0，185071 字节；
- 公开 factory 4.19 DTS 生成的 DTB：137363 字节；
- 两份 DTB 反编译为 DTS：退出码均为 0；
- 标准 `diff -u`：退出码 1，表示两个 DTS 存在差异，差异文件为 262767 字节。

关键 SHA-256：

- 官方 5.10 EVB1 DTB：`02a8da3925164e91eab27bc784f05007925efc7e464eff83fe83f9b75fa8f3f8`；
- factory 4.19 DTB：`d793ee4486112e36409ae46594988b5b358d8961bb48243e229736df2293a243`；
- DTS 差异文件：`cee1a5ea13e98cbf3b25ce7d3ae061edada4dc2b0af7f56992b4c1879e6ed79c`。

此步骤只生成主机侧隔离构建输出；没有改动 SDK Git 工作树、没有构建 Image/rootfs、没有部署或
操作开发板。

## 2. 已识别的候选范围

差异文本已证实下列类别存在需要人工分类的变化：GMAC 外部时钟与 RGMII、DSI/HDMI/面板、摄像头
传感器端点、I2C 复用、SD/eMMC 引脚、USB VBUS、电源调节器、Wi-Fi 唤醒、音频卡和 NPU 供电引用。

其中 GPIO、pinctrl、`clock_in_out`、PHY 延迟、`vbus-supply`、显示 route/endpoint、面板复位和
传感器 endpoint 是板级连线候选；`compatible` 差异、phandle 数值、媒体 IP 的供电引用等也可能来自
4.19 与 5.10 BSP 的表示差异，不能直接拷贝。RGA、RKVENC 和 NPU 节点出现在差异中，不等于这些
硬件已经在 5.10 上验证可用。

## 3. 已降级的旧摘要

`/home/china/rkav-gec-dtb-analysis-20260902-205443` 的终端摘要曾报告官方 DTB 和语义对比成功，
但其保留目录只找到 factory DTB、源文件和日志，缺失官方 DTB、反编译 DTS 与差异文本。该摘要无法
独立审计，不能作为成功证据；本文件第 1 节的 `213048` 输出取代它。详见 P098。

## 4. 下一步与边界

下一步只读提取 5.10 EVB1 与 factory DTS 中的 GMAC、显示、存储、USB、Wi-Fi、I2C 和摄像头节点，
并以公开移植笔记为第三来源，把每项标成“板级候选”“BSP 漂移”或“未确定”。在这一分类完成前：

- 不创建或应用 GEC 候选 DTS；
- 不构建完整镜像或 rootfs；
- 不部署、刷写、启动或操作开发板；
- 不把旧 4.19 MPP/RGA ABI 失败与 5.10 用户态 ABI 混为已解决。

只有候选 DTS、内核配置、启动介质和与 5.10 驱动匹配的 MPP/RGA/RKNPU2 用户态库都完成可复核门禁后，
才可申请用户明确确认进行第一次 5.10 上板启动。
