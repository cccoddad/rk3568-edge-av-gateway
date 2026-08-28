# MPP/RGA 运行时盘点与 SDK 门禁

更新日期：2026-08-28

## 1. 文档用途

本文记录 RGA 优化和 MPP H.264 接入开始前的实际盘点结果。它定义实现前的 SDK 门禁，避免把
板端已有运行库误当作已经具备可交叉编译的开发环境。

## 2. 当前结论

RK3568 板端已有 MPP/RGA 驱动和运行库，RKVENC 已完成内核探测；但 Ubuntu 构建机和目标板均未
发现与该 BSP 匹配的开发头文件、AArch64 sysroot 或 BSP 源码。因此当前尚未实现 MPP H.264、RGA
后端、RK3568 实机 MP4 或真实硬件 OSD。此处停止是兼容性门禁，不是程序或硬件故障。

## 3. 已保存的板端证据

| 项目 | 结果 |
|---|---|
| 内核 | `Linux 4.19.232`，AArch64 |
| MPP 设备节点 | `/dev/mpp_service` |
| RGA 设备节点 | `/dev/rga` |
| MPP 运行库 | `/usr/lib/librockchip_mpp.so.0`，SHA-256 `49c9cf2b0a8f78d8318dd40ad5714512986267e3b3e285d41781937c46ddcbca` |
| RGA 运行库 | `/usr/lib/librga.so.2.1.0`，SHA-256 `21b65ea2ec6cfa21bea19b25533c66261c4acb7d5bd63a854e4881c8d8ff0bd8` |
| RGA 运行库字符串 | `rga_api version 1.3.2_[0]` |
| MPP 构建线索 | 库字符串含 `RK356X_Linux_V1.3.2` 与 `rockchip-mpp-develop` 构建路径 |
| 内核驱动 | `mpp_service ... probe success`、`mpp_rkvenc ... probing finish`、`rga2: Driver loaded successfully ver:3.2.63318` |
| MPP 信息工具 | `/usr/bin/mpp_info_test` 正常退出但未输出可用于源码匹配的版本 |

启动日志里的 `power_model`/动态系数警告来自 RKVENC 探测过程，但最终探测完成；它们是残余 BSP
配置风险，不能在没有编码负载证据时直接判定为 MPP 编码失败。

## 4. 已排除的错误路径

- 不在运行中的网关旁启动第二个 `rkav-gateway`。本次盘点前已确认 `gateway_running=0`，且没有
  运行编码、推理或 RGA 压力测试。
- 不从当前公开 MPP/RGA 分支直接抄头文件链接板端旧运行库。公开分支会演进，不能替代
  `RK356X_Linux_V1.3.2` 的精确 SDK。
- 不在板端安装开发包或替换运行库。此举可能破坏已通过的 V4L2、ALSA、RKNN 和 USB 验收基线。

## 5. 构建机盘点

对 `/mnt/hgfs/share`、`/opt`、`/usr/local/src` 的只读搜索未发现：

- MPP：`rk_mpi.h`、`mpp_enc_cfg.h`、`mpp_buffer.h`、`mpp_frame.h`、`librockchip_mpp.so`；
- RGA：`im2d.h`、`RgaApi.h`、`rga.h`、`librga.so`；
- `RK356X_Linux_V1.3.2` BSP 或可用的 AArch64 sysroot。

## 6. 后续门禁与所需交付物

在修改 MPP/RGA 业务代码前，必须取得以下之一：

1. 完整 `RK356X_Linux_V1.3.2` BSP 源码及其 Buildroot 输出；或
2. 与上述两项运行库和内核匹配的 AArch64 SDK/sysroot，至少包含 MPP/RGA 头文件、目标动态库、
   依赖库与版本/提交信息。

拿到后先校验头文件、目标库和板端库的版本关系；再把 SDK 路径作为本地构建参数，不把厂商二进制、
模型、板端日志或个人配置提交进 Git。实际接入仍按：MPP/RGA 编译门禁 -> Mock/单元回归 ->
板端唯一网关短测 -> 证据保留 -> 文档更新的顺序执行。

## 7. 专有词

- **MPP**：Rockchip Media Process Platform，RK3568 上用于硬件视频编解码的用户态库。
- **RGA**：Rockchip 图形加速器，可承担缩放、色彩转换和像素叠加。
- **SDK**：编译程序所需的头文件、库、工具和版本信息集合。
- **sysroot**：交叉编译时模拟目标系统根目录的头文件和库目录。
- **ABI**：应用程序二进制接口，规定函数调用、结构体内存布局等；版本不匹配可能编译成功但运行失败。

## 8. 相关文档

- [项目总体代码架构](01-项目总体代码架构.md)
- [项目当前开发状态](19-项目当前开发状态.md)
- [CPU OSD 软件 MP4 验收与交接](26-CPU-OSD软件MP4验收与交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
