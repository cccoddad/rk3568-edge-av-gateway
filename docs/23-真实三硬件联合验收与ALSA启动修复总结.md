# 真实三硬件联合验收与 ALSA 启动修复总结

更新日期：2026-08-28

## 1. 文档用途

本文是 2026-08-27 至 2026-08-28 长会话的正式实现总结和下一次会话交接文件。它记录从
主程序接入 MJPEG 解码、AArch64 交叉构建和真实摄像头 + RKNN 验收，到真实 ALSA 加入后
定位启动 XRUN、部署修复版、完成 30 分钟三硬件长稳及修正验收规则的全过程。

下一次会话不要根据聊天记录猜测进度，应先阅读本文、
[项目当前开发状态](19-项目当前开发状态.md)和
[MJPEG 解码预处理接入与验收](22-MJPEG解码预处理接入与验收.md)。

本文只属于 RK3568 实时音视频边缘分析网关。其他 Windows/Qt 项目的方案、源码、数据库和
进度不属于本网关的实现或验收范围，不能用来改变本文定义的后续顺序。

## 2. 当前一句话结论

主程序已经跑通真实 `/dev/video9` 1280x720 MJPEG 摄像头、`hw:2,0` 48 kHz 双声道 USB
麦克风、libjpeg-turbo 解码和 RKNN YOLOv5 NPU 推理的 10 秒联合链路。ALSA 启动阶段的
确定性 XRUN 已完成根因定位和代码修复，修复版取得 500 个音频块、49 次 JPEG 解码、49 次
RKNN 结果和 49 次 NPU 中断，错误、恢复、工作线程错误和队列丢弃均为 0。

30 分钟联合长稳第一次运行约 20 分钟后被终端 `Ctrl+C` 人为中断；独立后台重试随后完整运行，
网关正常退出，业务守恒、NPU 中断、RSS、线程、温度、错误和队列指标全部通过。原控制器误把
有上限的解码延迟滑动窗口样本数当成累计解码次数，因而写出 `failed:1`。项目未覆盖该原始记录，
而是在五份原始证据 SHA-256 校验通过后按正确累计指标重验收，30 分钟结论为通过。

## 3. 本轮主要实现操作

### 3.1 接入主程序 MJPEG 解码预处理

新增稳定的 `IVideoDecoder` 接口和 `JpegVideoDecoder` 实现，固定使用 libjpeg-turbo
3.1.4.1：

- 源码 SHA-256：
  `ecae8008e2cc9ade2f2c1bb9d5e6d4fb73e7c433866a056bd82980741571a022`；
- 使用 TurboJPEG 内存 API，把 V4L2 MJPEG payload 解码为紧密 RGB888；
- 校验 JPEG 头、尺寸、最大像素数、整数溢出和内存分配；
- 保留原视频帧的 sequence、单调 PTS 和来源尺寸；
- 单帧损坏 JPEG 作为可恢复 codec 错误跳过，不终止整条实时流；
- 增加 `video_decode` 延迟指标；
- 新增 `rk3568-rknn-mjpeg.json` 和 `rk3568-rknn-mjpeg-alsa.json`。

解码放在 5 FPS 推理抽帧之后，而不是对约 30 FPS 的所有视频帧做 CPU RGB 展开：

```text
V4L2 MJPEG
-> 推理限帧
-> capacity=1 keep_latest 队列
-> JPEG 解码为 RGB888
-> RKNN resize / 推理 / YOLOv5 后处理
```

原始 MJPEG 仍交给视频编码分支。该设计只证明推理支路成本受控；后续真实 H.264 视频编码
仍需按照总体架构建立 FFmpeg 软件媒体基线，再评估 MPP/RGA 全帧硬件路径。

### 3.2 完成 PC 回归和 AArch64 兼容构建

- Windows Debug：43/43 测试通过；
- Windows Release：43/43 测试通过；
- 真实 UGREEN 摄像头 JPEG 样本解码为 1280x720、RGB888，Debug/Release 均通过；
- AArch64 交叉构建通过；
- 最终 ELF 最高要求 GLIBC 2.34，低于板端 GLIBC 2.35；
- 动态依赖保持在板端已有的 `librknnrt.so`、`libm`、`libc` 和加载器范围；
- libjpeg-turbo 静态进入程序，不依赖板端未知版本 `libjpeg.so`。

### 3.3 通过 TF 卡部署并校验产物

由于板端 TF 卡挂载包含 `noexec`，部署包不能直接从 `/media/sdcard0` 运行。固定流程为：

1. Windows/Ubuntu 生成部署目录和 `SHA256SUMS`；
2. 复制到 TF 卡并安全弹出；
3. 板端重新执行 `sha256sum --check SHA256SUMS`；
4. 复制到 `/userdata/rkav/...`；
5. 对程序执行 `chmod 0755`；
6. `sync` 后卸载 TF 卡；
7. 只从板端持久目录运行。

当前修复版程序路径为：

```text
/userdata/rkav/gateway-alsa-start-fix-20260827/rkav-gateway
```

程序 SHA-256 为：

```text
9d29d4327e731ecff3b5835c14f5fb70c9e58d556fcd0eb85298005601da6623
```

### 3.4 完成真实 MJPEG + RKNN 10 秒和 60 秒验收

10 秒结果：

| 指标 | 结果 |
|---|---:|
| 视频采集/包 | 269 / 269 |
| JPEG 解码/RKNN请求/RKNN结果 | 49 / 49 / 49 |
| NPU 中断增量 | 49 |
| 解码 p50/p95/p99 | 32.471 / 60.856 / 63.042 ms |
| 推理 p50/p95/p99 | 144.332 / 154.692 / 159.296 ms |
| 错误/恢复/队列丢弃 | 0 / 0 / 0 |

60 秒结果：

| 指标 | 结果 |
|---|---:|
| 完整测量时长 | 61.170 秒 |
| 视频采集/包 | 1,660 / 1,660，约 27.137 FPS |
| JPEG解码/RKNN请求/RKNN结果 | 291 / 291 / 291 |
| NPU 中断增量 | 291 |
| 错误/恢复/队列丢弃 | 0 / 0 / 0 |
| 工作态 RSS | 约 45,128–57,796 KiB |
| 温度峰值 | 51.875 摄氏度 |
| 新增 NPU/IOMMU 错误 | 0 |

解码样本、推理请求、推理结果和 NPU 中断一一对应，证明调用了真实 NPU，不是仅靠日志计数。

### 3.5 独立确认 USB 麦克风能力

板端枚举结果：

```text
ALSA card 2: U2K / UGREEN Camera 2K
capture node: /dev/snd/pcmC2D0c
ALSA address: hw:2,0
format: S16_LE
channels: 2
sample rate: 48000
```

`arecord` 完成 2 秒真实采集且退出码为 0。内核可能输出：

```text
usb 1-1.1: 3:1: cannot get freq at ep 0x84
```

这是当前 UAC 设备/旧驱动查询采样频率时的兼容警告。它不能单独证明采集失败，必须结合
退出码、音频块计数、XRUN、PTS 连续性和新增内核日志判断。

### 3.6 定位真实三硬件第一次联合运行的启动 XRUN

第一次使用真实摄像头、真实 ALSA 和 RKNN 联合运行时，最终业务数据基本守恒，但出现：

```text
alsa_audio.poll: ALSA capture overrun: Broken pipe
category=xrun
native_code=32
retryable=true
```

同时 `recoveries_total=1`。时间戳证据显示：

- ALSA 后端约在 `05:52:18.616` 打开并启动；
- Application 和工作线程约在 `05:52:18.815` 才运行；
- 中间间隔约 199 ms；
- ALSA 缓冲约为 4 个 20 ms period，总容量约 80 ms。

因此根因不是麦克风不可用，而是 `AlsaAudioCapture::Open()` 过早执行
`SNDRV_PCM_IOCTL_START`。后续 RKNN 初始化耗时超过音频缓冲容量，读取线程开始前硬件缓冲
已经溢出，返回 `EPIPE`/XRUN。

### 3.7 修复 ALSA 启动生命周期

代码修改：

- `Open()` 只完成硬件参数协商和 `PREPARE`，不再启动 PCM；
- 增加 `started_` 状态；
- 第一次 `Read()` 才执行 `SNDRV_PCM_IOCTL_START`；
- 首个音频 PTS 与真正启动时刻同时建立；
- `Recover()` 和 `Close()` 正确维护 `started_`；
- 避免 RKNN 等慢后端初始化时间消耗音频缓冲。

修复后重新执行 Windows 43 项回归，并明确在 Ubuntu 增量交叉构建日志中看到
`alsa_audio_capture.cpp.o` 被重新编译和程序重新链接，避免误用旧目标文件。

### 3.8 修复版真实三硬件 10 秒验收

修复版配置：

```text
/userdata/rkav/gateway-alsa-start-fix-20260827/rk3568-rknn-mjpeg-alsa.json
```

最终结果：

| 指标 | 结果 |
|---|---:|
| 完整测量时长 | 11.020 秒 |
| 视频采集/视频包 | 276 / 276 |
| 音频采集/音频包 | 500 / 500 |
| JPEG解码/RKNN请求/RKNN结果 | 49 / 49 / 49 |
| NPU 中断增量 | 49（450 -> 499） |
| 路由/消费包 | 776 / 776，等于 276 + 500 |
| 错误/恢复/工作线程错误 | 0 / 0 / 0 |
| 队列丢弃/停止后残留 | 0 / 0 |
| 温度 | 46.111 -> 48.333 摄氏度 |
| 新增 USB/ALSA/NPU/IOMMU 错误 | 0 |

自动验收标记：

```text
alsa_start_fix_10s=passed
```

修复前唯一一次启动 XRUN 消失，说明延迟启动方案已在真实硬件上生效。

## 4. 本轮新增或修改的主要代码

### 4.1 新增

- `include/rkav/media/video_decoder.h`
- `include/rkav/media/jpeg_video_decoder.h`
- `src/media/jpeg/jpeg_video_decoder.cpp`
- `tests/unit/jpeg_video_decoder_test.cpp`
- `config/rk3568-rknn-mjpeg.json`
- `config/rk3568-rknn-mjpeg-alsa.json`
- `docs/22-MJPEG解码预处理接入与验收.md`
- 本文档

### 4.2 修改

- CMake/libjpeg-turbo 依赖、构建选项和 AArch64 构建脚本；
- Application 的解码器创建、推理前解码、错误恢复和解码指标；
- 配置校验和部署包内容；
- ALSA Capture 的 `Open/Read/Recover/Close` 生命周期；
- Windows 测试脚本和相关项目状态文档。

当前工作区包含尚未提交的实现。下一次会话不得使用 `git reset --hard`、`git checkout --` 或
其他覆盖命令清除这些改动。应先执行 `git status --short` 和 `git diff --check`，确认工作区。

## 5. 30 分钟长稳最终结果

### 5.1 第一次运行

前台版本运行到约 20 分钟时收到终端 `Ctrl+C`。中断前观测：

- RSS 稳定在约 57,852 KiB；
- 线程数稳定为 9；
- 温度约从 53.1 升至 55.0 摄氏度；
- 没有触发 80 摄氏度温度保护。

这轮只能记录为“人为中断”，不能写成程序失败，也不能写成 30 分钟通过。

### 5.2 后台重试结果

为了避免日志查看时误按 `Ctrl+C` 再次终止网关，已创建独立结果目录并使用
`setsid + nohup` 启动后台控制器：

```text
/userdata/rkav/gateway-alsa-start-fix-20260827/real-mjpeg-alsa-rknn-30m-retry-1
```

完整结果为：

- 网关退出码 0，温度保护未触发；
- 50,237 帧视频和 89,983 个音频块全部生成对应 packet；
- 8,714 次 RKNN 请求、结果和 NPU 中断增量完全相等；
- 140,220 个预期 packet 全部完成路由和消费；
- 错误、恢复、工作线程错误、队列丢弃和停止后残留均为 0；
- 179 个资源样本，工作态 RSS 早期/晚期均为 57,652 KiB，线程数始终为 9；
- 最高温度 57.222 摄氏度，未达到 80 摄氏度保护阈值；
- `dmesg` 增量没有新的 USB 断连、XRUN 或 NPU/IOMMU 错误。

### 5.3 自动验收假失败与修正规则

原控制器读取到 `video_decode.samples=3594` 后执行：

```sh
test "$decode_samples" -ge 8500
test "$decode_samples" -eq "$requests"
```

第一条在 `set -e` 下立即终止脚本并写出 `status=failed:1`。`video_decode.samples` 是用来计算
p50/p95/p99 的滑动窗口占用量，最多保留 4,096 个样本，满后删除最旧四分之一；它不是累计
解码计数。累计工作量应以 `inference_requests_total=8714` 为准，并要求请求、结果和 NPU 中断
三者相等。成功的 8,714 次 RKNN 请求也证明对应 MJPEG 已成功解码为 RGB 输入。

项目保留原始 `status=failed:1`、`controller.log` 和控制脚本，并新增：

- `original-evidence.sha256`：覆盖 `gateway.log`、`controller.log`、`resources.csv`、
  `dmesg.delta.log` 和 `soak-controller.sh`；五项复核均为 `OK`；
- `corrected-validation.log`：记录错误规则、正确累计指标和所有人工复核结果；
- 修正规则后的标记：
  `three_hardware_soak_30m=passed_after_validation_rule_correction`。

## 6. 结果解释与证据边界

这次不需要为了得到一个新的 `status=passed` 而重复跑 30 分钟。重复运行不会改变旧脚本的语义
错误，而且会额外占用摄像头、麦克风和 NPU。正确做法是保留原始自动验收失败、保存校验清单，
再以修正后的规则复核同一份不可变证据。

未来 2 小时/12 小时控制器不能再要求解码延迟窗口样本数等于累计推理次数，应改为：

```sh
test "$requests" -ge "$expected_min_requests"
test "$decode_samples" -gt 0
test "$decode_samples" -le 4096
test "$requests" -eq "$results"
test "$npu_delta" -eq "$results"
```

其中“累计计数器”用于证明总工作量，“滑动窗口”用于计算近期延迟分位数，两者用途不同，不能
互相替代。本结论只关闭 30 分钟真实摄像头 + ALSA + RKNN 长稳，不外推到 USB 断连、真实
H.264/AAC、MP4、OSD、MPP/RGA、RTSP 或 2 小时/12 小时最终长稳。

## 7. 30 分钟通过后的固定开发顺序

后续严格以 [整体代码架构](01-项目总体代码架构.md) 为准，不引入其他项目的需求：

1. 30 分钟真实三硬件证据和验收规则修正已归档；
2. 下一步进行三硬件运行中的 USB 断连/降级验证；该操作会中断采集并涉及物理拔插，必须先向用户
   说明影响并取得明确确认；
3. 建立 FFmpeg 软件媒体基线，先生成可播放的 H.264/AAC MP4，验证 PTS、时长和音画同步；
4. 完成检测框和文字 OSD；
5. 使用 MPP 替换软件 H.264 编码，并根据指标引入 RGA 颜色转换/缩放；
6. 在本地 MP4 正确后接入 RTSP，验证同网段播放、断网和重连；
7. 完成 systemd 托管、SIGTERM、日志轮转和最终 2 小时/12 小时长稳。

当前 Checksum 编码器仍不是真实 H.264/AAC，项目也尚无 MP4 或 RTSP 成品输出。未完成上述
媒体阶段前，简历和文档不能把它们写成既成事实。

## 8. 本轮问题索引

本轮新增问题已经追加到两份长期问题台账：

- P079：固定 libjpeg-turbo 版本和跨平台构建边界；
- P080：JPEG 解码放置位置导致的 CPU 成本问题；
- P081：ALSA 在 RKNN 初始化前过早启动导致确定性 XRUN；
- P082：终端日志无法完整复制导致验收证据缺失；
- P083：`Ctrl+C` 中断长稳被误认为程序失败；
- P084：后台 `tail -f` 长时间无输出被误认为卡死；
- P085：延迟滑动窗口样本数被误当成累计解码数，导致长稳自动验收假失败。

详细专业表达见 [项目问题汇总：面试版](06-项目问题汇总-面试版.md)，通俗解释见
[项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)。

## 9. 相关文档

- [整体代码架构](01-项目总体代码架构.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
- [真实音视频联调总结与下一步](12-真实音视频联调总结与下一步.md)
- [RKNN YOLOv5 主程序后端接入](17-RKNN-YOLOv5主程序后端接入.md)
- [项目当前开发状态](19-项目当前开发状态.md)
- [MJPEG 解码预处理接入与验收](22-MJPEG解码预处理接入与验收.md)
