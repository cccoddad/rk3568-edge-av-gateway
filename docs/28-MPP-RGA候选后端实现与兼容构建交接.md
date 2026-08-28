# MPP/RGA 候选后端实现与兼容构建交接

更新日期：2026-08-28

## 1. 当前结论

本轮已实现 RGA 色彩转换加 MPP H.264 候选编码后端，并完成 Windows 非 JPEG Debug 回归和
Ubuntu 24.04 AArch64 源码编译。原生交叉产物要求 `GLIBC_2.36`、`GLIBC_2.38`，超过 RK3568
板端的 GLIBC 2.35，兼容门禁按设计拒绝通过。因此没有部署该文件、没有启动网关、没有占用摄像头、
麦克风、NPU、RGA 或 MPP。

下一步不是重跑原生 GCC 13 构建，而是使用项目已有 Ubuntu 22.04 容器产生 ABI 兼容候选产物。
容器构建尚未执行，结果未知。

## 2. 实现范围与边界

- 新增 `MppRgaVideoEncoder`：运行时通过 `dlopen` 加载板端 `librockchip_mpp.so.0` 和
  `librga.so.2.1.0`，不把厂商二进制提交进仓库。
- RGA 将 RGB/BGR 转为 NV12；MPP 将 NV12 编为 H.264 Annex-B elementary stream。IDR NAL 用于
  识别关键帧，因为公开 MPP 头文件没有稳定公开的 intra packet 标志。
- 新增 `H264ElementarySink`，先写 `.part`，正常关闭后才改名为 `.h264`；音频包可被消费以保持
  管道收尾，但不会写入 H.264 elementary stream。
- 新增真实 V4L2、ALSA、RKNN、MPP/RGA 配置模板。它只是未来短测模板，当前没有生成板端 H.264、
  MP4、AAC、RTSP 或真实硬件 OSD 成品。
- 公开 `mpp develop`/`librga main` 头文件是候选编译输入，不等同于 `RK356X_Linux_V1.3.2` 的
  精确 SDK。结构体布局和函数 ABI 仍必须由板端短测证明。

## 3. 已有证据

| 检查 | 结果 |
|---|---|
| Windows 独立 Debug（`RKAV_WITH_JPEG=OFF`） | 45/45 通过，覆盖新增 H.264 sink、配置和应用装配；JPEG 后端本轮未重测 |
| Ubuntu 24.04 原生 AArch64 配置 | 离线依赖配置通过，0.6 秒 |
| Ubuntu 24.04 原生 AArch64 编译 | 40/40 成功，MPP/RGA 目标完成静态库链接 |
| ELF 架构和动态依赖 | AArch64；仅动态依赖 `librknnrt.so`、`libm.so.6`、`libc.so.6` 和加载器 |
| GLIBC 门禁 | 失败：最高 `GLIBC_2.38`，板端上限 `GLIBC_2.35` |

原生失败证据目录保留在 Ubuntu：

```text
/tmp/rkav-mpp-rga-cross-20260828-210049-12662
```

源包 `rkav-mpp-rga-source-20260828-2030.tar.gz` 的 SHA-256 为
`fcd422dede40ba4fabb830748d39ddd820afadb031bd76a8dee69deac952ed35`。这次失败复现了
既有 P068：编译成功不等于旧 Buildroot 可运行，不能通过降低检查或忽略 GLIBC 版本绕过。

## 4. Ubuntu 22.04 容器构建入口

新源码包位于 Windows 下载目录，VMware Ubuntu 的挂载路径为：

```text
/mnt/hgfs/WinDownloads/rkav-mpp-rga-source-20260828-2140.tar.gz
SHA-256: 2312401aca7dcc75a4f3dda17fcacbd0b0b7783cbfda21acb596473550554b5c
启动器: /mnt/hgfs/WinDownloads/rkav-run-mpp-rga-container-20260828-2140.sh
启动器 SHA-256: b044539be3807648ba90150ac71620e0810366bad11565a8d3272404bc726511
```

启动器会校验源码包、创建唯一 `/tmp/rkav-mpp-rga-container-*` 目录、保存
`container-build.log`，并在 Docker 内使用 Ubuntu 22.04、GCC/G++ 11 和板端 GLIBC 2.35
上限门禁。它会创建或复用本机 Docker 镜像及缓存，但不会部署或启动 RK3568 网关。

## 5. 停止位置与下一步

1. 在 Ubuntu 执行容器启动器，先得到 `mpp_rga_container_build=passed` 或第一处明确错误。
2. 若容器产物通过，复核 ELF、SHA-256、GLIBC 上限和候选头文件来源。
3. 在用户明确同意后，先只读确认板端无遗留 `rkav-gateway`，再用唯一结果目录部署并进行短时
   三硬件 + RGA/MPP 测试。
4. 只有 H.264 elementary stream、RGA 指标、MPP 驱动日志和错误/队列状态均通过后，才可进入
   MPP/RGA 阶段的文档收口；MP4/RTSP 仍按固定后续顺序处理。

## 6. 专有词

- **Annex-B elementary stream**：只有 H.264 视频码流本身的 `.h264` 文件，不含 MP4 容器或 AAC
  音频。
- **ABI**：二进制接口。即使函数名字相同，头文件和运行库的结构体布局或调用约定不同也可能运行失败。
- **GLIBC 门禁**：读取 ELF 所要求的系统 C 库符号版本，拒绝比板端更高的版本，防止部署后才出现
  `GLIBC_x.y not found`。
- **Docker 容器**：隔离的构建环境。本项目用 Ubuntu 22.04 容器固定较旧的交叉工具链，不是把
  Docker 或编译器安装到开发板。

## 7. 相关文档

- [项目总体代码架构](01-项目总体代码架构.md)
- [项目当前开发状态](19-项目当前开发状态.md)
- [CPU OSD 软件 MP4 验收与交接](26-CPU-OSD软件MP4验收与交接.md)
- [MPP/RGA 运行时盘点与 SDK 门禁](27-MPP-RGA运行时盘点与SDK门禁.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
