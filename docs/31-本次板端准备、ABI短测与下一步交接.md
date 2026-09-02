# 本次板端准备、ABI 短测与下一步交接

更新日期：2026-09-02

## 1. 本文用途

本文总结本段会话完成的板端准备、网络与 USB 硬件核验、MPP/RGA 新候选重建、首次 ABI 短测、失败证据
锁定及文档归档。它只适用于 RK3568 实时音视频边缘分析网关。

当前最新结论：**首次 MPP/RGA ABI 短测未通过；不得重跑、不得进入 RTSP 或 systemd。下一步是取得
匹配 `RK356X_Linux_V1.3.2` BSP/SDK 后重新构建。**

## 2. 本次完成的操作

### 2.1 物理连接、网络和 USB 枚举

- Windows 主网卡与 USB 千兆网卡分别使用两根网线；USB 网卡与板端 `eth0` 均协商为 1 Gbps。
- Windows 到板端 `169.254.141.127` 四次 ping 全部成功，ARP MAC `de:5f:36:04:cb:95` 与板端 `eth0`
  一致，证明专用直连链路正常。
- 板端 USB 设备 `0c45:636f` 已枚举；`/dev/video9` 是 UGREEN Camera 2K 的视频节点；ALSA card 2 的
  capture 节点为 `hw:2,0` / `/dev/snd/pcmC2D0c`。
- 未使用的 MIPI CSI `No link` 日志不代表 USB UVC 摄像头异常。

### 2.2 新候选构建与传输准备

- 旧 Ubuntu `/tmp/rkav-mpp-rga-container-20260828-212452-13839` 已被清理，旧候选 ELF 不可复核；未将
  新产物写成旧哈希。
- 经授权下载并固定公开候选输入：MPP `0986d01294d5c2449c14cf13af9b740368c33967`、RGA
  `2b32edcb97b601b25683e2941d888c8515da6d55`、nlohmann/json
  `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`。
- 在 Ubuntu 持久目录 `/home/china/rk3568-work/rkav-mpp-rga-controller-20260902-110614-3688` 保存
  控制脚本、输入副本、来源清单和容器结果，避免再依赖 `/tmp`。
- Ubuntu 22.04 Docker / GCC/G++ 11.4 完成 40/40；新候选为 AArch64，最高 `GLIBC_2.34`，动态依赖仅含
  `librknnrt.so`、`libm.so.6`、`libc.so.6` 和加载器；ELF SHA-256 为：

```text
86131dcc0ce9adf66a4840f3c43d39035ce25e795f81d3fb580010aefbce289e
```

- Windows 在专用 USB 网卡启动临时 HTTP 服务；板端 `wget` 成功读取 `SHA256SUMS` 后，才下载候选程序、
  配置和来源清单。下载文件均经 SHA-256 验证，传输结束后 HTTP 服务已关闭。

### 2.3 板端短测前置门禁

- 未发现遗留 `rkav-gateway`，没有启动第二个实例。
- `/dev/video9`、`/dev/snd/pcmC2D0c`、`/dev/mpp_service`、`/dev/rga` 与 RKNN 模型均存在；`/userdata`
  可用空间约 7.8 GiB。
- 板端 MPP/RGA 库哈希仍为 `49c9cf2b...ddcbca`、`21b65ea2...f0bd8`。
- 新建唯一目录 `/userdata/rkav/mpp-rga-abi-19700101-021545-755`，配置副本的输出路径改为该目录内的
  `mpp-h264-evidence.h264`；`--validate-config` 通过。1970 日期来自既有 RTC 未校时问题，PID `755`
  仍保证本次目录唯一。

### 2.4 首次 MPP/RGA ABI 短测

唯一 `rkav-gateway` 实例计划运行 10 秒，却在约 0.8 秒退出，退出码为 1。V4L2 已成功打开
`/dev/video9`（1280x720 MJPEG@30），ALSA 已成功打开 `hw:2,0`（48 kHz、双声道、S16_LE、20 ms）。

首个失败为：

```text
mpp_rga_video_encoder.configure_encoder: MPP rejected H.264 encoder configuration
[category=codec, native_code=-1, retryable=false]
```

视频、音频、推理、路由、包消费、错误和恢复计数均为 0；未生成 H.264 elementary stream。新增 USB
内核提示 `cannot get freq at ep 0x84` 已保留，但 ALSA 已成功 Open，不能将其写成主失败根因。

## 3. 证据和结论

失败目录的 `failure-evidence.sha256` 已覆盖候选 ELF、配置、日志、退出码、运行前后中断/dmesg、输入
哈希、下载清单和库版本字符串，14/14 为 `OK`。

MPP 库字符串带有 `RK356X_Linux_V1.3.2` 构建路径，RGA 库为 `rga_api version 1.3.2_[0]`。这证明当前
“公开分支头文件 + GLIBC 兼容”的新候选未通过旧 BSP MPP 编码配置的运行时兼容门禁。它不证明摄像头、
麦克风、网线、RKNN 或 H.264 码流运行成功；也不等于 MP4、AAC 或 RTSP。

本轮新增实际问题已归档：P092（`/tmp` 清理）、P093（多机执行环境混淆）、P094（端到端下载优先于
主机自测）；P091 记录 MPP 编码配置拒绝。

## 4. 当前停止位置与下一步

1. 保留板端失败目录和 Ubuntu 持久控制目录；不要删除、覆盖或使用当前公开头文件候选盲目重跑。
2. 索取与板端 `RK356X_Linux_V1.3.2`、MPP/RGA 运行库、Linux 4.19 驱动匹配的 BSP/SDK、MPP/RGA
   头文件和 AArch64 sysroot。
3. 获得 SDK 后先在 Ubuntu 固定来源、commit/版本和 SHA-256；只在持久目录新建构建结果，并执行
   AArch64、动态依赖与 GLIBC<=2.35 门禁。
4. 经用户明确确认后，先在板端只读确认无遗留网关、设备/模型/库哈希不变；在新的唯一结果目录下载、
   复核、改写唯一 `.h264` 输出路径并做配置校验。
5. 届时仅启动一个 10 秒实例。通过条件为：退出码 0；视频、音频、推理请求和结果均大于 0 且守恒；
   H.264 elementary stream 非空、有 Annex-B 起始码和 IDR；错误、恢复、工作线程错误、队列丢弃和
   停止残留为 0；无新增 MPP/RGA/IOMMU/NPU fault、timeout、panic 或 error。
6. 只有上述短测通过后，才进入 RTSP 与网络恢复；之后才是 systemd、SIGTERM、日志轮转及 2/12 小时
   长稳。不得把 H.264 elementary stream 写成 MP4、AAC 或 RTSP 成品。

## 5. 本次文档更新与 Git

- [项目当前开发状态](19-项目当前开发状态.md)已链接到本文。
- [面试版问题汇总](06-项目问题汇总-面试版.md)新增 P092-P094。
- [通俗版问题汇总](07-项目问题汇总-通俗版.md)新增 P092-P094。
- [首次 ABI 短测失败交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)保留首次失败的详细原始结论。

## 6. 相关文档

- [项目总体代码架构](01-项目总体代码架构.md)
- [MPP/RGA 运行时盘点与 SDK 门禁](27-MPP-RGA运行时盘点与SDK门禁.md)
- [MPP/RGA 候选后端实现与兼容构建交接](28-MPP-RGA候选后端实现与兼容构建交接.md)
- [首次 ABI 短测失败交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
