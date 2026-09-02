# MPP/RGA 首次板端 ABI 短测失败与下一步交接

更新日期：2026-09-02

## 1. 当前结论

首次 MPP/RGA 板端 ABI 短测已经执行，但**未通过**。新公开头文件候选通过了 Ubuntu 22.04
AArch64/GLIBC 兼容构建，却在 RK3568 的 MPP H.264 编码配置阶段被运行库以 `-1` 拒绝。V4L2
摄像头和 ALSA 麦克风都已成功打开；业务线程尚未开始采集、推理或编码，未生成 H.264 elementary
stream。不得称为 MPP/RGA、MP4、AAC、RTSP 或硬件编码完成。

当前唯一失败证据目录为：

```text
/userdata/rkav/mpp-rga-abi-19700101-021545-755
```

板端 RTC 未校时，目录日期显示 1970；目录名含运行 PID `755`，且创建时拒绝复用，仍是唯一结果目录。
不要删除、覆盖或重跑该目录。

## 2. 新候选构建与输入追溯

旧 `/tmp` 容器结果及其候选 ELF 已被 Ubuntu 清理，不能再复用或验证此前的 `136a...84399`。经明确授权，
本轮下载当前公开分支并固定输入：

| 输入 | 固定版本 |
|---|---|
| MPP `develop` | `0986d01294d5c2449c14cf13af9b740368c33967` |
| RGA `main` | `2b32edcb97b601b25683e2941d888c8515da6d55` |
| nlohmann/json | `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` |
| 源码包 | `2312401aca7dcc75a4f3dda17fcacbd0b0b7783cbfda21acb596473550554b5c` |

持久 Ubuntu 控制目录为：

```text
/home/china/rk3568-work/rkav-mpp-rga-controller-20260902-110614-3688
```

Ubuntu 22.04 Docker 构建使用 GCC/G++ 11.4，完成 40/40。候选 ELF 为 AArch64，仅动态依赖
`librknnrt.so`、`libm.so.6`、`libc.so.6` 和加载器；最高要求 `GLIBC_2.34`，低于板端 2.35。
新候选程序 SHA-256：

```text
86131dcc0ce9adf66a4840f3c43d39035ce25e795f81d3fb580010aefbce289e
```

这只是新的候选构建证据，不与已丢失的旧候选混同。

## 3. 板端前置门禁与部署准备

- 未发现遗留 `rkav-gateway` 进程。
- `/dev/video9`、`/dev/snd/pcmC2D0c`、`/dev/mpp_service`、`/dev/rga` 和 RKNN 模型均存在。
- 板端 MPP 库 SHA-256 为 `49c9cf2b0a8f78d8318dd40ad5714512986267e3b3e285d41781937c46ddcbca`；
  RGA 库 SHA-256 为 `21b65ea2ec6cfa21bea19b25533c66261c4acb7d5bd63a854e4881c8d8ff0bd8`。
- Windows 到板端的专用直连网线经 1 Gbps 链路、ARP MAC 和四次 ping 0 丢包确认；HTTP 下载探测
  `wget_exit=0` 后才下载候选包。临时 HTTP 服务随后已关闭。
- 下载包和 SHA-256 清单均已校验通过；运行时配置副本改为唯一 `.h264` 输出路径，
  `--validate-config` 通过。

## 4. 首次 ABI 短测结果

唯一 10 秒实例启动后约 0.8 秒安全退出，退出码为 1。V4L2 成功协商 `/dev/video9` 的
1280x720 MJPEG@30；ALSA 成功协商 `hw:2,0` 的 48 kHz、双声道、S16_LE、20 ms。

首个应用错误为：

```text
mpp_rga_video_encoder.configure_encoder: MPP rejected H.264 encoder configuration
[category=codec, native_code=-1, retryable=false]
```

最终 `application_stopped` 的视频、音频、推理、路由、包消费、错误和恢复计数均为 0；没有
`mpp-h264-evidence.h264`。运行前后 `dmesg` 只有一条新增 USB 音频频率提示：
`usb 1-1.1: 3:1: cannot get freq at ep 0x84`。ALSA 已完成 Open，首个失败仍是 MPP 配置拒绝，不能将
这条内核提示写成已定位根因。

## 5. 证据锁定与 ABI 结论

`failure-evidence.sha256` 已对 14 项原始材料复核，全部为 `OK`，包括候选 ELF、配置、网关日志、
退出码、运行前后中断和 dmesg、输入哈希、下载清单和库版本字符串。

MPP 库字符串含 `RK356X_Linux_V1.3.2` 的构建路径，RGA 库字符串为
`rga_api version 1.3.2_[0]`。这与公开 MPP/RGA 当前分支头文件存在明确版本代际差异。

结论是：`GLIBC_2.34` 门禁通过只证明 ELF 可被板端系统 C 库加载；当前公开头文件候选未通过旧 BSP
MPP 编码配置的运行时兼容门禁。仅从 `-1` 不能可靠判定具体是控制键、结构体布局还是 API 语义差异，
不得猜测修改并盲目重跑。

## 6. 下一步和边界

1. 索取与板端 `RK356X_Linux_V1.3.2`、MPP/RGA 运行库和 Linux 4.19 驱动匹配的 BSP/SDK、头文件与
   AArch64 sysroot。
2. 固定匹配 SDK 的版本、来源和 SHA-256 后重新构建；保留当前失败目录和持久 Ubuntu 控制目录。
3. 获得明确确认后，再次先只读检查无遗留网关，并在新的唯一结果目录执行一个 10 秒实例。
4. 只有产生非空、含 Annex-B 起始码和 IDR 的 H.264 elementary stream，且业务守恒、错误/恢复/队列
   丢弃为零、无新增 MPP/RGA/IOMMU/NPU 相关故障，才能收口 MPP/RGA 短测。
5. 在此前不得进入 RTSP、网络恢复、systemd 或最终长稳；更不得把 `.h264` elementary stream 写成
   MP4、AAC 或 RTSP 成品。

## 7. 相关文档

- [项目总体代码架构](01-项目总体代码架构.md)
- [项目当前开发状态](19-项目当前开发状态.md)
- [MPP/RGA 运行时盘点与 SDK 门禁](27-MPP-RGA运行时盘点与SDK门禁.md)
- [MPP/RGA 候选后端实现与兼容构建交接](28-MPP-RGA候选后端实现与兼容构建交接.md)
- [本次会话实现总结、问题归档与下一步交接](29-本次会话实现总结、问题归档与下一步交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
