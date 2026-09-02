# LubanCat 5.10.209 SDK 公开性与 GEC V11 迁移评估

更新日期：2026-09-02

## 1. 本文用途

本文记录 RK3568 实时音视频边缘分析网关对 LubanCat Linux 5.10.209 路线的只读离线评估。评估只读取
本仓库源码和公开 Git 仓库元数据；未同步完整 SDK、未构建镜像、未连接或修改开发板，也未覆盖既有
MPP/RGA 失败证据。

当前结论：**LubanCat 公开仓库可以取得固定的 Linux 5.10.209 内核和通用 RK3568 构建骨架，但没有
找到一套可由公开 manifest 直接复现的“GEC V11 板级配置 + 5.10.209 + MPP/RGA/RKNN 用户态组件”完整
SDK。5.10 迁移可继续评估和准备，但现在不能下载若干公开分支后直接上板。**

## 2. 已核对的固定来源

| 来源 | 固定版本 | 核对结果 |
|---|---|---|
| [LubanCat manifests](https://github.com/LubanCat/manifests) | `5ba5600065b31f2836dd764dd71d365a755ac359` | 公开清单可读取；当前 generic 发布指向 2026-07-29 |
| [LubanCat kernel](https://github.com/LubanCat/kernel) | `6a5f8ede873d63e305c46c2b6ed1dcf792917564` | Makefile 为 Linux 5.10.209；存在 Rockchip MPP 和 RKNPU 内核驱动目录 |
| [LubanCat device_rockchip](https://github.com/LubanCat/device_rockchip) | `a1642d64206d5afc54ad6c0c0f0232844f0e9e81` | 有 LubanCat/EVB RK3568 配置；文件路径中无 GEC V11 配置资产 |
| [GecEdu-RK3568](https://github.com/Leon19960120/Gecedu-RK3568) | `main`：`4f9cd0af934d5aa57861aa65c1b56d88fa38a75e`；5.10 分支：`410d4328a7e0e6a124cb54b73329d4730680ed26` | 文档记录 GEC V11 已启动 5.10.209，但仓库不含目标 DTS/DTSI、完整 defconfig 或可应用补丁 |
| [LubanCat buildroot](https://github.com/LubanCat/buildroot) | manifest 写 `4c038732919b526ea263fd2316feac97dc082213` | GitHub API 报提交不存在，直接 fetch 报 `not our ref` |
| LubanCat Buildroot 公开 5.10 标签 | `linux-5.10-gen-rkr1`，commit `3ccfbe398058e2eaac13096ecbbb9c907f66ebd4` | 包规则存在，但 MPP/RGA/RKNPU2 均引用 SDK 邻接的本地 external 源码，未在规则中固定 MPP/RGA commit |

内核树存在驱动，只证明内核侧实现可得，不证明用户态头文件、动态库和驱动 ABI 已匹配。GEC 文档中的
运行记录证明迁移曾成功启动，也不等于公开仓库已经包含那棵外部 SDK 工作树。

## 3. Manifest 引用链结论

### 3.1 当前 RK356x 专用入口仍是 4.19

`rk356x_linux_release.xml` 当前内容为 `rk356x_linux/rk356x_linux4.19_stable.xml`。因此不能用该入口取得
5.10.209。

### 3.2 Generic 路线提供 5.10.209 基础骨架

`lubancat_linux_generic.xml` 最终包含 `lubancat_linux_generic_20260729.xml`，其中固定：

- `device_rockchip`：`a1642d642...`；
- `kernel-5.10`：`6a5f8ede...`，实际版本 5.10.209；
- 另有独立 `kernel-6.1`、U-Boot、rkbin、工具链以及 Debian/Ubuntu 根文件系统项目。

但 2024-07 至 2026-07 的全部 dated generic manifest 都未列出 Buildroot、MPP、RGA、RKNN Toolkit2 或
RKNPU2 用户态项目。当前两个 LubanCat RK3568 Buildroot 配置还明确首选 `RK_KERNEL_PREFERRED="6.1"`，
并使用 `rk356x-lubancat-generic`，不能直接当作 GEC V11 的 5.10 配置。

### 3.3 Full 路线不是公开的 5.10 媒体 SDK

2025-03 至 2026-07 的 dated full manifest 都包含 `buildroot-2024.04-rkr5.xml`。该文件中的 MPP、RGA、
RKNN Toolkit2 和 RKNPU2 均指向私有 `ssh://git@gitlab.ebf.local/...`，标签为
`linux-6.1-stan-rkr5`。这既不是公开来源，也不是 5.10 版本线。

旧 `include/rk356x_buildroot.xml` 留有 MPP `9ffb11ca...`、RGA `1e2f0dbb...`、RKNPU2
`c0303684...` 等提交，但当前没有顶层 manifest 引用它，`rk-github` 远端定义也已缺失；其中 MPP 提交
可公开解析；RGA 因远端定义缺失且同名公开仓库不可访问，RKNPU2 提交也不能按该清单复现。它只能作为
4.19 时代的历史线索，
不能作为 5.10.209 的兼容依据。

## 4. GEC V11 板级资产缺口

GEC 公开文档记录的目标配置是：

```text
RK_DEFCONFIG=rockchip_rk3568_gec_defconfig
RK_KERNEL_CFG=rockchip_linux_defconfig
RK_KERNEL_DTS=kernel/arch/arm64/boot/dts/rockchip/rk3568-gec-v11-linux.dts
```

公开运行记录还显示目标 model 为 `Rockchip RK3568 GEC DDR4 V10 Board`，并记录 UART、eMMC、显示、
以太网、USB、音频、GPU 和 RKNPU 内核驱动已起来；RKNN 用户态推理和真实 camera sensor 链仍标为待
验证。

但是 GEC 主分支和 5.10 文档分支均没有：

- `rockchip_rk3568_gec_defconfig` 的完整内容；
- `rk3568-gec-v11-linux.dts`、`rk3568-gec-v11.dtsi`、`rk3568-evb-gec.dtsi`；
- 可以在固定 LubanCat 提交上重放的 patch series；
- 与该运行镜像匹配的 MPP/RGA/RKNN 用户态版本清单。

LubanCat 固定 5.10.209 内核的 `arch/arm64/boot/dts/rockchip` 目录共 766 个条目，`gec` 名称命中为
0；固定 `device_rockchip` 提交的全部路径同样为 0。缺口不能靠选择一个同为 RK3568 的 EVB/LubanCat
DTS 自动消除，因为 DDR、PMIC、时钟、显示、以太网 PHY、USB、音频和外设引脚可能不同。

## 5. 对本网关的直接影响

本网关 MPP/RGA 后端通过 `dlopen` 使用板端 `librockchip_mpp.so.0` 和 `librga.so.2.1.0`。交叉构建至少
需要 MPP 的 `rk_mpi.h`、`rk_venc_cfg.h`，以及 RGA 的 `im2d.h`；RKNN 还需要 `rknn_api.h` 和
`librknnrt.so`。现有构建门禁能检查文件存在、AArch64 架构、动态依赖和 GLIBC 上限，不能单独证明 MPP
配置键、结构体、控制命令和内核驱动语义一致。

因此有两种不同目标：

1. 若继续使用当前 4.19 镜像，只需取得与 `RK356X_Linux_V1.3.2` 运行库匹配的最小头文件/sysroot，
   不必先升级整个系统。
2. 若迁移到 5.10.209，则必须把 bootloader、GEC DTS、内核、根文件系统、MPP/RGA、RKNN Runtime
   和交叉 sysroot 作为同一个版本化系统验证，不能只换内核或只换头文件。

## 6. 迁移难度与可执行路线

难度评估为**中高**。源码下载和离线构建可以由开发机独立完成；主要风险是公开 GEC 板级源文件缺失，
以及媒体/NPU 用户态版本没有被 5.10 manifest 固定。推荐继续按以下门禁推进：

1. 固定 LubanCat generic 2026-07-29 的 manifest、5.10.209 kernel、device、U-Boot、rkbin 和工具链，
   保存 commit 与 SHA-256；不把 full manifest 的私有 6.1 媒体项目混入。
2. 优先向 GEC/LubanCat 来源取得上述 GEC defconfig、DTS/DTSI 和对应 patch；若无法取得，再以现有
   4.19 running DT、厂商提取 DTS 和 GEC 验证文档做可审阅的重新移植，不能伪称原厂配置。
3. 为 5.10.209 单独锁定公开 MPP、RGA、RKNN Runtime/RKNPU2 组合，并保存源码提交、头文件哈希、
   库 SONAME 和 sysroot；先在离线环境构建最小 smoke test。
4. 构建 boot/rootfs 镜像后，先检查目标架构、内核版本、DTB/FIT 内目标 model、模块与用户态库清单、
   GLIBC/GLIBCXX、MPP/RGA/RKNN 依赖和镜像哈希。
5. 涉及开发板前必须由用户明确确认，并先备份可恢复镜像、确认串口和恢复模式。首次只做启动与只读
   枚举，不启动网关；后续每个摄像头、ALSA、RKNPU、RGA、MPP 测试仍保持唯一进程和唯一证据目录。

在 5.10 的 MPP/RGA 最小编码测试实际产生并验证 H.264 elementary stream 前，仍不进入 RTSP、网络
恢复、systemd、SIGTERM、日志轮转或 2/12 小时长稳；该码流也不能写成 MP4、AAC 或 RTSP 成品。

## 7. 当前精确停止位置

- 已完成公开 manifest、固定内核、固定 device、GEC 主/5.10 分支和 Buildroot 公开标签的只读核对。
- 只在 `out/lubancat-5.10-eval-20260902` 保存 Git 元数据；该目录由项目忽略，不进入提交。
- 未同步完整 SDK、未下载镜像、未构建、未部署、未修改板端配置，也未启动任何板端进程。
- 当前 4.19 MPP/RGA 失败候选及板端唯一失败目录保持原状，禁止盲目重跑。
- 下一工作单元是固定并取得公开 5.10 基础源码，同时继续寻找或重建 GEC V11 板级 patch；任何上板
  动作仍需用户明确确认。

## 8. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [本次板端准备、ABI 短测与下一步交接](31-本次板端准备、ABI短测与下一步交接.md)
- [MPP/RGA 首次板端 ABI 短测失败与下一步交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)
- [MPP/RGA 运行时盘点与 SDK 门禁](27-MPP-RGA运行时盘点与SDK门禁.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
