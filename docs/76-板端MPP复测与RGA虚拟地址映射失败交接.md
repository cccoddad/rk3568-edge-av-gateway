# 板端 MPP/RGA 复测与 RGA 映射修复交接（含最终通过）

更新日期：2026-09-14
状态：**已关闭——板端 MPP/RGA 10 秒三硬件短测通过（退出码 0、零错误/恢复/丢弃、H.264 可解码）**。
本文件是当前最新会话交接；按固定顺序，下一步是板端 RTSP 实机验证。

## 1. 当前结论

2026-09-14 在真实板端完成了 `docs/67` 第 5 节的 MPP/RGA 复测，并连环修复四个独立问题后通过：

- **最终结果**：10 秒三硬件运行退出码 0；视频采集 247 帧全部编码（247/247 包），音频 499 块，
  推理 43 请求/43 结果；错误、恢复、工作线程错误、队列丢弃、停止残留全为 0；`dmesg` 增量空；
  生成 3,029,031 字节 H.264（`ffprobe` 247 帧、1280x720、9 个 I 帧，全量解码零错误，抽帧画面
  色彩正常）。
- **修复后候选**：ELF SHA-256 `c236d6e1be318c922d9193fc42cfb6b03adabe0c49a0ef2a02ee0a1e6420f87f`，
  最高 GLIBC 2.34（板端 2.35），源码修复已进入仓库 `src/media/mpp/mpp_rga_video_encoder.cpp`。
- **问题归档**：P115（配置键拼写）在板端确认关闭；P122（RGA/MPP 运行时路径）新增并关闭。

## 2. 板端接入方式（本轮新打通，供后续会话复用）

- SSH(22) 仍是 P032 的 banner 超时；COM5 串口线当前未插（Windows 显示 Code 45 幽灵设备）。
- 本轮改用板端 **ADB over TCP 5555**：Windows 侧 Android SDK platform-tools 执行
  `adb connect 192.168.50.2:5555` 成功；`adb devices` 同时列出 USB gadget transport
  `603ac2909d6146a7` 与 TCP transport，两者 `uname`/`hostname` 完全一致，确认为同一块板。
- 该通道用于只读检查、`adb push` 部署、`adb pull` 取证与受控测试执行；未停止/重启任何
  板端服务，未刷写，未改板端系统文件（仅临时校时）。
- 注意：Windows 侧 adb 会剥离命令中的双引号，含 `|`、`\r` 等的 shell 片段要写成脚本文件
  后 `adb push` 执行，不要内联拼命令。

## 3. 运行前门禁（只读）

| 检查项 | 结果 |
|---|---|
| 板端身份 | `Linux RK356X 4.19.232 aarch64`，root shell，5.10 未刷入 |
| 遗留进程 | `pidof rkav-gateway` 为空；无 rkav 相关进程 |
| 设备节点 | `/dev/mpp_service`、`/dev/rga`、`/dev/dri/card1`、`renderD129` 存在 |
| MPP 运行库 | `/usr/lib/librockchip_mpp.so.0` SHA-256 `49c9cf2b…ddcbca`，与 docs/27 一致 |
| RGA 运行库 | `/usr/lib/librga.so.2.1.0` SHA-256 `21b65ea2…f0bd8`，与 docs/27 一致 |
| 摄像头/麦克风 | `1-1.1 0c45:636f UGREEN Camera 2K`；`/dev/video9`；ALSA card 2 `pcmC2D0c` |
| 模型 | `/userdata/rkav/yolov5-runtime-712d661/yolov5s-rk3568.rknn`（8,688,000 字节） |
| 磁盘 | `/userdata` 空闲 7.8 GB；旧失败目录 `mpp-rga-abi-19700101-021545-755` 完整保留 |
| RTC/时间 | 板端时间为 1970（老问题）；每次运行前用 `date -s @<epoch>` 临时校时 |

期间板卡因插拔摄像头断电重启过一次（uptime 归零、时间为 1970），已重新校时并复核设备枚举，
不影响结论。

## 4. 四个根因与修复（按发现顺序）

1. **P115 配置键修复确认通过**：候选不再被 `configure_encoder` 拒绝，管道进入运行态。
   这是第一次在板端跑过 MPP 配置阶段，也是后续问题的前提。
2. **RGA 不接受堆虚拟地址（第一条板端失败证据）**：
   `wrapbuffer_virtualaddr` 包装解码器堆 RGB 与 MPP 缓冲映射地址，内核报
   `rga2_mmu: RGA2 failed to get pte, result = 1352, pageCount = 2026`、
   `rga2 map src0 memory failed`。修复：用 `mpp_buffer_get_fd_with_caller` 取 MPP 缓冲的
   dma-buf fd，源/目标都用 `wrapbuffer_fd_t` 包装；源 RGB 先拷入 MPP ION 缓冲
   （第一版代价是多一次约 2.76 MB/帧的 CPU 拷贝，后续可用解码器直出 ION 消除）。
3. **librga 的 wstride 单位是像素，不是字节**：原代码把字节 stride 3840 传入，RGA 按
   3 倍宽度访问（`pageCount = 2026` 页 ≈ 3840×720×3 字节 = 8.3 MB 正是证据），内核出现
   `rga2: Rga err irq! INT[701],STATS[1]`。修复：传 `frame.stride / 3` 并校验可被 3 整除。
   对接 Rockchip 开发指南原文“wstride 以像素为单位”。
4. **MPP 输入超时 0 造成半配置状态**：初始化时输入是默认阻塞（走 `mpp_enc_start_v2`），
   之后 `MPP_SET_INPUT_TIMEOUT=0` 会让 `put_frame` 改走 `put_frame_async` 的队列，但异步
   编码线程只在初始化时输入就是非阻塞才启动，队列无人消费，提交被拒
   （`submit_frame: MPP rejected the converted NV12 frame`）。修复：只设置输出超时 0，
   输入保持默认阻塞。
5. **flush 把 EOS 空包当错误**：排空时 MPP 用带 EOS 标志的空包表示结束，原逻辑先判空包
   直接报错（`flush: MPP returned an empty packet while draining`），退出码 1。修复：先读
   EOS 标志，空包且 EOS 视为排空完成；退出码变为 0。

## 5. 最终通过证据

- 唯一结果目录：`/userdata/rkav/mpp-rga-fd-fix3-20260914-1136-5821`。
- 部署文件哈希（板端复核一致）：ELF `c236d6e1…0f87f`、部署配置 `00021513…e4ddb`（仅输出路径与
  构建产物不同）、provenance `3c34c387…c3221`、`DEPLOYED.sha256` 与 `evidence.sha256` 7/7 `OK`。
- 运行：`--validate-config` EXIT=0；`./run_retest.sh` 输出 `RUN_DONE exit=0 size=3029031`。
- 关键计数（最终 `application_stopped`）：`video_captured_total 247`、`video_packets_total 247`、
  `audio_captured_total 499`、`inference_requests_total 43`、`inference_results_total 43`、
  `packets_routed_total 746`、`packets_consumed_total 746`、`errors_total 0`、
  `recoveries_total 0`；`video_encode`/`sink_h264`/`audio_encode`/`inference` 队列 `dropped 0`。
  `expired_detections_total 1` 为首帧结果超过 `max_result_age_ms` 的策略计数，非错误。
- `dmesg.before` 与 `dmesg.after` 逐字节相同（无新增内核日志）。
- H.264：3,029,031 字节，以 `00 00 00 01 67 42 C0 1F …`（Annex-B + SPS）开头；
  `ffprobe` 报 247 帧、1280x720、yuv420p、9 个 I 帧；`ffmpeg -f null -` 全量解码零错误；
  抽帧 PNG 人工确认画面色彩与内容正常（图存 `D:\share\`，不入库）。
- 证据副本：`D:\share\mpp-rga-fd-fix3-20260914-1136-5821`（与板端 `evidence.sha256` 一致）。

## 6. 本轮全部结果目录（失败证据同样保留）

| 目录 | 内容 | 结论 |
|---|---|---|
| `/userdata/rkav/mpp-rga-retest-20260914-105633-6922` | P115 修复候选复测 | MPP 配置阶段通过；RGA 堆虚拟地址映射失败（P122 第 2 条） |
| `/userdata/rkav/mpp-rga-fd-fix-20260914-1116-5673` | 第一版 fd 修复 | RGA err irq（stride 单位错误）+ MPP 提交被拒（输入超时半配置） |
| `/userdata/rkav/mpp-rga-fd-fix2-20260914-1126-4172` | 第二版 fd + 像素 stride + 阻塞输入 | 完整 10 秒码流 3.2 MB；仅 flush EOS 空包失败 |
| `/userdata/rkav/mpp-rga-fd-fix3-20260914-1136-5821` | 最终版（+EOS 修复） | **通过**：exit 0、零错误/丢弃、码流可解码 |

Windows 侧对应副本：`D:\share\mpp-retest-20260914-105633-6922`、
`D:\share\mpp-rga-fd-fix2-20260914-1126-4172`、`D:\share\mpp-rga-fd-fix3-20260914-1136-5821`。
候选包（WinDownloads）：`rkav-mpp-rga-fd-fix-candidate-20260914-1115-2649`（第一版）、
`rkav-mpp-rga-fd-fix2-candidate-20260914-1126-4172`、`rkav-mpp-rga-fd-fix3-candidate-20260914-1136-5821`（最终版）。

构建链（Ubuntu 24.04 虚拟机 `/home/china/rk3568-work`，Docker 镜像
`rkav/aarch64-rknn-build:ubuntu22.04`，GCC 11.4）：

| 轮次 | 源码归档 SHA-256 | 编码器源码 SHA-256 | ELF SHA-256 |
|---|---|---|---|
| 第一版 fd | `7eef1b34…ea17` | `f5184bf1…0ace` | `8a43cda1…b747` |
| 第二版 +stride/阻塞 | `48d01196…c8de` | `2cc2cc7e…7804` | `ff3a7427…753d` |
| 第三版 +EOS（最终） | `0594c32c…3079` | `118be7f4…4feb` | `c236d6e1…0f87f` |

## 7. 边界

- 本次只验证 10 秒短测判据；30 分钟/2 小时长稳、USB 断连、板端 RTSP、ZLMediaKit 阶段 2、
  systemd 托管均未在本轮执行，不得用本轮结果替代。
- RGA fd 路径第一版对每帧 RGB 多一次 CPU 拷贝（约 2.76 MB），是"先正确后优化"的已知代价；
  解码器直出 ION 缓冲的零拷贝优化需要另行验证。
- 未刷写、未重启、未改板端系统文件；旧失败证据目录与哈希保持锁定。
- `zboot`/5.10 刷写路线维持 docs/62/63 的阻断状态，与本轮无关。

## 8. 下一步（固定顺序）

1. **板端 RTSP 实机验证**：用本轮 MPP H.264 输出为源，推流到板端 RTSP 接收端（ffmpeg listen
   模式）或 ZLMediaKit，VLC/ffplay 拉流；验证服务器重启后的断连恢复。端口/分辨率/编码参数
   变更前先与用户确认，使用唯一结果目录并保留前后证据。
2. **板端 ZLMediaKit 阶段 2 部署**：解包 `rkav-zlmediakit-aarch64-20260913-232946.tar.gz`，
   按 docs/74 第 5 节执行；含 WebRTC 浏览器播放与端到端延迟测量。
3. **systemd 与长稳**：板端托管 + 2/12 小时长稳（与 ZLM 联动）。
4. 之后才回到 30 分钟三硬件长稳收口之上的其它遗留项（USB 进程内热重连等仍按原顺序）。

## 9. 涉及名词

- **RGA MMU**：RGA 硬件访问内存前把用户缓冲区映射进自己的地址空间；映射失败即
  `failed to get pte`。
- **dma-buf fd**：内核为共享缓冲区签发的"取件凭证"；RGA/MPP 都能直接按 fd 取内存。
- **wstride（像素单位）**：librga 的宽度步幅参数按像素计，RGB888 每像素 3 字节，传字节数
  会放大 3 倍。
- **MPP 阻塞/异步 I/O**：`MPP_SET_INPUT_TIMEOUT=0` 会切换 `put_frame` 到异步队列；该模式
  必须在初始化前就确定，否则队列无消费者。
- **EOS 空包**：MPP 排空结束时发出的带结束标志、长度为 0 的包。

## 10. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [MPP 配置键拼写根因定位与修复交接](67-MPP配置键拼写根因定位与修复交接.md)
- [MPP/RGA 候选后端实现与兼容构建交接](28-MPP-RGA候选后端实现与兼容构建交接.md)
- [MPP/RGA 首次板端 ABI 短测失败与下一步交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)
- [本次板端准备、ABI 短测与下一步交接](31-本次板端准备、ABI短测与下一步交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P122
