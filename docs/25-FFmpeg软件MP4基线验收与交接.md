# FFmpeg 软件 MP4 基线验收与交接

更新日期：2026-08-28

## 1. 文档用途

本文记录在 Ubuntu 主机上完成的 FFmpeg H.264/AAC MP4 软件媒体基线。它衔接已经归档的
30 分钟真实三硬件长稳和 USB 断连/重插恢复，前一阶段见
[USB 断连与重插恢复验收](24-USB断连与重插恢复验收.md)。

本阶段只证明可重复的 Mock 音视频源能够经过实际 FFmpeg 编码、MP4 封装和 `ffprobe` 检查。
它不改变 RK3568 摄像头、麦克风和 NPU 的独占状态，也不代表 MPP、RGA、OSD、RTSP、板端
FFmpeg 媒体输出或进程内 USB 热重连已经完成。

## 2. 一句话结论

FFmpeg 软件 MP4 基线已经通过。Ubuntu 24.04 的 FFmpeg 6.1.1 环境从全新源码包构建网关，
以 320x180、30 FPS Mock 视频和 48 kHz 双声道 Mock 音频运行 5 秒，生成真实 H.264/AAC
MP4。`ffprobe` 确认容器可解析、视频为 H.264、音频为 AAC、时长为 5.020 秒、两路起始 PTS
均为 0，并已对两个流的 PTS/DTS 单调性完成自动检查。

## 3. 实现范围

- 新增可选 CMake 开关 `RKAV_WITH_FFMPEG`，默认关闭，不影响既有 Checksum、V4L2、ALSA、
  JPEG 或 RKNN 构建路径；启用时通过 `pkg-config` 查找 `libavcodec`、`libavformat`、
  `libavutil`、`libswscale` 和 `libswresample`。
- 新增 `FfmpegVideoEncoder`：接收 RGB/BGR 帧，经 `libswscale` 转为编码所需像素格式，使用
  `libx264` 输出 H.264 packet。
- 新增 `FfmpegAudioEncoder`：接收 S16_LE PCM，经 `libswresample` 和音频 FIFO 适配 AAC 的
  编码帧大小，输出 AAC packet。采集块数与 AAC packet 数不要求一一相等，因为 AAC 通常以
  1,024 个采样为一帧，而本项目采集块为 20 ms（960 个采样）。
- 新增 `FfmpegMp4Sink`：从编码器获得 codec 参数和 time base，写入 MP4；正常结束时先写
  `software-baseline.mp4.part`，写 trailer 成功后原子改名为 `.mp4`。**trailer** 是 MP4 文件
  末尾的索引和收尾信息，缺少它的文件通常不能可靠播放。
- `EncodedPacket` 补充 duration 和流描述，MP4 必需 Sink 使用有限超时的 `block_producer`。
  Sink 出错会转成全局受控失败，不会静默丢掉媒体数据后仍报告成功。
- 新增 `config/mock-ffmpeg-mp4.json`、硬件配置样例和两个验收脚本。MP4 配置要求视频和音频
  均为 FFmpeg 编码器，拒绝把 Checksum packet 误封装为 MP4。

## 4. 兼容性与本机回归

- FFmpeg CMake API 下限为 `libavcodec >= 58`、`libavformat >= 58`、`libavutil >= 56`、
  `libswscale >= 5`、`libswresample >= 3`，覆盖 Ubuntu 22.04 的 FFmpeg 4.4 基线；本次实测为
  Ubuntu 24.04 的 FFmpeg 6.1.1。
- Linux 优先复用系统 GTest 1.11+，网络不可用时不必为既有测试访问 GitHub；Windows 保持
  固定源码 GTest，避免 MinGW 链接到 Miniconda 的 MSVC ABI 库。
- Windows Debug/Release 的 44 项单元与集成测试已经通过。Ubuntu 本次全新目录构建、CTest、
  网关运行和媒体验证均返回 0。

## 5. Ubuntu 验收证据

### 5.1 源码包与目录

```text
Archive: /mnt/hgfs/share/rkav-ffmpeg-baseline-source-20260828-142440.tar.gz
Archive SHA-256: 7b0e22b1cc5ba40d363d1b588adef5a7ea66afa74fa37695a400176d851b6558
Work: /tmp/rkav-ffmpeg-baseline-20260828-142440
Result: /tmp/rkav-ffmpeg-baseline-20260828-142440/out/ffmpeg-baseline-20260828-143013-4786
```

压缩包 SHA-256、解压后的两个 Shell 脚本语法和控制器均通过；`baseline_exit_code=0`。
结果目录保存了 `software-baseline.mp4`、运行配置、`gateway.log`、`ffprobe-validation.log`、
CTest 输出、依赖版本和 `SHA256SUMS`。`SHA256SUMS` 对网关程序、配置、MP4、网关日志和
验收日志逐项复核均为成功。

### 5.2 `ffprobe` 结果

| 项目 | 结果 | 验收含义 |
|---|---:|---|
| 视频编码 | `h264` | 不是 Checksum 占位 packet |
| 音频编码 | `aac` | 不是 PCM 或 Checksum 占位 packet |
| 视频规格 | `320x180` | 与 Mock 基线配置一致 |
| 音频规格 | `48000x2` | 48 kHz、双声道，与配置一致 |
| 容器时长 | `5.020000 s` | 与 5 秒目标相差 20 ms，低于 0.5 秒容差 |
| 视频起始 PTS | `0.000000 s` | 视频从统一时间轴起点开始 |
| 音频起始 PTS | `0.000000 s` | 起始音画偏移为 0 ms |
| PTS/DTS | 自动检查通过 | 两个流均无缺失或倒退的时间戳 |

**PTS** 是播放时间戳，决定播放器何时展示或播放一帧；**DTS** 是解码时间戳，决定解码器何时
取用 packet。两者单调可避免封装文件在播放器中出现时间倒退；起始 PTS 相同只证明初始同步，
不替代未来真实摄像头和麦克风链路的长期音画同步测试。

### 5.3 网关业务指标

| 指标 | 结果 |
|---|---:|
| 视频采集/视频 packet | 151 / 151 |
| 音频采集/音频 packet | 251 / 237 |
| 推理请求/结果 | 151 / 151 |
| 路由/MP4 Sink 消费 | 388 / 388 |
| 错误/恢复 | 0 / 0 |
| 视频编码队列丢弃/残留 | 0 / 0 |
| 音频编码队列丢弃/残留 | 0 / 0 |
| 推理队列丢弃/残留 | 0 / 0 |
| MP4 Sink 队列丢弃/残留 | 0 / 0 |

音频 251 个 20 ms 采集块没有要求产生 251 个 AAC packet，原因见第 3 节 AAC 帧重分块说明；
关键守恒关系是 MP4 Sink 的 388 个已路由 packet 全部消费，没有丢弃或停止后残留。

## 6. 首次失败与修正

首次全新 Ubuntu 验收已经完成 CMake 配置、28 个目标编译链接、网关运行和 MP4 写入，但
`validate_mp4.sh` 使用变量名 `index`。Ubuntu 默认的 `mawk` 把它与内置字符串函数
`index()` 冲突，解析阶段报语法错误，控制器以 `baseline_exit_code=1` 结束。该失败不是编码器
错误，也不是时间戳倒退；原始目录
`/tmp/rkav-ffmpeg-baseline-20260828-141436/out/ffmpeg-baseline-20260828-141655-4229` 已保留。

修正仅将循环变量改为 `field_index`，不改变任何阈值或媒体数据。修正后的新包在全新目录运行，
本章证据即为最终通过结果。相关问题登记为 P087。

## 7. 当前精确停止位置

已完成：真实三硬件长稳、USB 断连安全退出、重插后重启、Ubuntu FFmpeg H.264/AAC MP4
软件基线。当前下一步严格按
[整体代码架构](01-项目总体代码架构.md)实现检测框和文字 OSD，并先在 Mock/软件 MP4 路径验证
检测结果与来源帧 PTS 的绑定。

尚未完成：OSD、RGA、MPP H.264、RK3568 实机 MP4 媒体输出、RTSP 与网络恢复、systemd、
SIGTERM 媒体文件收尾验证、日志轮转和 2 小时/12 小时最终长稳。不能把本机软件基线表述为
上述能力已经完成。

## 8. 相关文档

- [整体代码架构](01-项目总体代码架构.md)
- [项目当前开发状态](19-项目当前开发状态.md)
- [USB 断连与重插恢复验收](24-USB断连与重插恢复验收.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
