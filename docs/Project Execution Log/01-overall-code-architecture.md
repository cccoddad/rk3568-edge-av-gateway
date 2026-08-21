# RK3568 实时音视频边缘分析网关：整体代码架构

## 1. 文档目的

本文档定义项目在硬件完整时的最终代码架构，是后续编码、测试、硬件接入和性能优化的统一依据。

“硬件完整”默认包括：

- RK3568 开发板，建议 4 GB 内存，已安装与板卡 BSP 匹配的 64 位 Linux。
- USB UVC 摄像头，目标输入为 MJPEG 1280×720@30 FPS，不支持时可降级为 YUYV 640×480@30 FPS。
- USB Audio Class 麦克风，目标输入为 48 kHz、单声道、S16_LE PCM。
- microSD/eMMC 系统存储，可选 NVMe 录像存储。
- 有线网络，用于 SSH、RTSP 推流、日志和状态查看。
- 散热器/风扇，用于长时间运行与性能测试。
- HDMI 和 3.3 V USB-TTL 为调试设备，不是程序运行时的强依赖。

当前只有开发板时的实施范围见 [02-board-only-development.md](./02-board-only-development.md)。

## 2. 项目目标与边界

### 2.1 最小完整功能

系统应实现如下稳定数据链路：

1. 通过 V4L2 采集 USB 摄像头视频。
2. 通过 ALSA 采集 USB 麦克风 PCM 音频。
3. 对视频进行解码、缩放、颜色转换和 letterbox 预处理。
4. 通过 RKNN Runtime 在 RK3568 NPU 上运行目标检测模型。
5. 将检测结果正确映射回原始图像，绘制检测框和文字。
6. 通过 Rockchip MPP 进行 H.264 硬件编码。
7. 通过 FFmpeg/libavcodec 进行 AAC-LC 音频编码。
8. 将音视频封装为 MP4，并发布到 RTSP 服务器。
9. 持续输出 FPS、延迟、丢帧、队列深度、XRUN、温度和重连次数等指标。
10. 设备断开、网络中断或编码错误时可恢复或安全降级，不出现死锁和无限内存增长。

### 2.2 第一版不强制实现

- MIPI CSI 摄像头、I2S 麦克风和 4G 模块。
- WebRTC、RTMP、多路摄像头和多模型并发。
- DMABUF 全链路零拷贝。
- 训练自有模型和云端管理平台。
- Web 前端管理界面。

这些是稳定主链路完成后的扩展，不应影响第一版交付。

## 3. 核心架构原则

### 3.1 稳定接口，可替换后端

业务层只依赖抽象接口，不直接依赖 `/dev/video0`、`hw:1,0`、RKNN 或 MPP 的具体 API。Mock 实现、PC 软件实现和 RK3568 硬件实现必须遵守同一套接口契约。

### 3.2 有界队列和显式背压

所有跨线程数据通过有界队列传递。队列满时必须执行已配置的策略，不允许默认无限增长。

- 实时视频优先保持“新鲜”，允许丢弃过时帧。
- 音频优先保持连续性，不能像视频一样任意丢帧；发生 XRUN 后需记录并重建 PCM 状态。
- 录像优先保证文件可播放，直播优先保证低延迟，两者不使用完全相同的阻塞策略。

### 3.3 单一时间域

内部 PTS 统一使用单调时钟，单位为微秒。不得使用可能被 NTP 校时修改的 wall clock 作为音视频同步基准。

时间库负责将内部微秒 PTS 转换为 FFmpeg `time_base`、MPP 时间戳或其他后端格式。

### 3.4 明确的数据所有权

每个帧/包必须明确谁拥有内存、何时释放、是否可修改。第一版允许安全拷贝；零拷贝优化必须在功能正确和性能基线完成后进行。

### 3.5 可观测性是主链路的一部分

日志、指标和健康状态不是最后补充的功能。每个模块都必须报告输入、输出、处理耗时、错误与恢复次数。

## 4. 总体数据流

```text
                    +---------------------+
USB UVC Camera ---> | V4L2 Video Capture  |
                    +----------+----------+
                               |
                         Raw/Compressed Frame
                               |
                    +----------v----------+
                    | Decode / Normalize  |  FFmpeg/MPP + RGA
                    +----------+----------+
                               |
                         Normalized Frame
                               |
              +----------------+----------------+
              |                                 |
      +-------v--------+                +-------v--------+
      | RKNN Inference|                | Frame Scheduler |
      +-------+--------+                +-------+--------+
              |                                 |
          Detections                             |
              +----------------+----------------+
                               |
                    +----------v----------+
                    | Overlay / OSD       |  CPU or RGA
                    +----------+----------+
                               |
                    +----------v----------+
                    | MPP H.264 Encoder   |
                    +----------+----------+
                               |
                         EncodedPacket
                               |
                               +--------------------+
                                                    |
USB Microphone --> ALSA Capture --> Resample --> AAC Encoder
                                                    |
                               +--------------------+
                               |
                    +----------v----------+
                    | Packet Router       |
                    +-----+-----------+---+
                          |           |
                    MP4 Recorder   RTSP Publisher ---> MediaMTX/VLC/ffplay

All modules -------------------> Metrics / Health / Structured Logging
```

检测通常无法跟上 30 FPS，因此推理支路与视频编码支路解耦。每个推理结果必须携带 `frame_sequence` 和原始 `pts_us`，叠加时只能使用符合设定时效的结果，不得把不知对应关系的旧结果画到新帧。

## 5. 建议仓库结构

```text
.
|-- CMakeLists.txt
|-- cmake/
|   |-- Options.cmake
|   `-- Toolchains/aarch64-linux-gnu.cmake
|-- app/
|   `-- main.cpp
|-- config/
|   |-- rk3568.json
|   `-- mock.json
|-- deploy/
|   |-- rkav-gateway.service
|   `-- mediamtx.yml
|-- docs/
|   |-- 01-overall-code-architecture.md
|   `-- 02-board-only-development.md
|-- include/rkav/
|   |-- app/application.h
|   |-- common/{result.h,error.h,clock.h,types.h,pixel_format.h}
|   |-- config/config.h
|   |-- queue/bounded_queue.h
|   |-- capture/{video_capture.h,audio_capture.h,device_discovery.h}
|   |-- media/{video_decoder.h,video_encoder.h,audio_encoder.h,resampler.h}
|   |-- vision/{preprocessor.h,inference_engine.h,postprocessor.h,overlay.h}
|   |-- output/{packet_router.h,muxer.h,recorder.h,streamer.h}
|   |-- monitor/{metrics.h,health_monitor.h,structured_logger.h}
|   `-- platform/{temperature.h,signal_handler.h}
|-- src/
|   |-- app/
|   |-- common/
|   |-- config/
|   |-- capture/{mock,v4l2,alsa}/
|   |-- media/{mock,ffmpeg,mpp}/
|   |-- vision/{mock,rknn,rga}/
|   |-- output/{mock,ffmpeg}/
|   |-- monitor/
|   `-- platform/linux/
|-- tests/
|   |-- unit/
|   |-- integration/
|   |-- soak/
|   `-- hardware/
|-- tools/
|   |-- inspect_devices.sh
|   |-- validate_recording.sh
|   `-- collect_diagnostics.sh
`-- third_party/
```

`include/rkav` 只存放稳定的公开契约；各种平台实现放在 `src` 的对应子目录。测试不得为调用私有实现而绕开公开接口。

## 6. 核心数据模型

### 6.1 通用视频帧

```cpp
using TimestampUs = std::int64_t;

enum class PixelFormat {
    kUnknown,
    kMjpeg,
    kYuyv422,
    kNv12,
    kRgb888,
    kBgr888
};

struct VideoFrame {
    std::uint64_t sequence{};
    TimestampUs pts_us{};
    int width{};
    int height{};
    int stride{};
    PixelFormat format{PixelFormat::kUnknown};
    std::shared_ptr<Buffer> buffer;
    FrameMemory memory;
};
```

`FrameMemory` 保存内存类型和必要的释放信息，第一版只需支持 CPU 内存；后续可增加 DMA-BUF fd、MPP buffer 和 RGA handle，不改变上层数据流程。

### 6.2 音频帧

```cpp
enum class SampleFormat { kS16LE, kF32LE };

struct AudioFrame {
    std::uint64_t sequence{};
    TimestampUs pts_us{};
    int sample_rate{};
    int channels{};
    int samples_per_channel{};
    SampleFormat format{SampleFormat::kS16LE};
    std::shared_ptr<Buffer> buffer;
};
```

建议以 20 ms 为一个音频块：48 kHz 时每通道 960 samples。AAC 编码器对帧大小有自己的要求，由 `AudioEncoder` 内部缓存和重组，不应让 ALSA 采集层迎合具体编码器。

### 6.3 检测结果

```cpp
struct Detection {
    int class_id{};
    float confidence{};
    RectF box_in_source;
};

struct DetectionBatch {
    std::uint64_t frame_sequence{};
    TimestampUs source_pts_us{};
    TimestampUs completed_at_us{};
    std::vector<Detection> items;
};
```

`box_in_source` 必须是已从模型输入空间还原到原图的坐标，避免 overlay 模块再次理解 letterbox 细节。

### 6.4 编码包

```cpp
enum class StreamKind { kVideo, kAudio };

struct EncodedPacket {
    StreamKind kind{};
    std::int64_t pts{};
    std::int64_t dts{};
    Rational time_base;
    bool key_frame{};
    Codec codec{};
    std::shared_ptr<Buffer> buffer;
};
```

封装层只接收完整的编码包和 codec configuration/extradata，不直接访问采集帧。

## 7. 稳定接口契约

下列接口名称可随实际代码风格微调，但职责边界不应改变。

```cpp
class IVideoCapture {
public:
    virtual ~IVideoCapture() = default;
    virtual Result<VideoCapabilities> Open(const VideoCaptureConfig&) = 0;
    virtual Result<VideoFrame> Read(std::stop_token) = 0;
    virtual void Close() noexcept = 0;
};

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual Result<AudioCapabilities> Open(const AudioCaptureConfig&) = 0;
    virtual Result<AudioFrame> Read(std::stop_token) = 0;
    virtual Result<void> Recover() = 0;
    virtual void Close() noexcept = 0;
};

class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;
    virtual Result<ModelInfo> Load(const ModelConfig&) = 0;
    virtual Result<DetectionBatch> Infer(const VideoFrame&) = 0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual Result<CodecInfo> Open(const VideoEncoderConfig&) = 0;
    virtual Result<std::vector<EncodedPacket>> Encode(const VideoFrame&) = 0;
    virtual Result<std::vector<EncodedPacket>> Flush() = 0;
};

class IAudioEncoder {
public:
    virtual ~IAudioEncoder() = default;
    virtual Result<CodecInfo> Open(const AudioEncoderConfig&) = 0;
    virtual Result<std::vector<EncodedPacket>> Encode(const AudioFrame&) = 0;
    virtual Result<std::vector<EncodedPacket>> Flush() = 0;
};
```

所有 `Open()` 都必须返回“实际协商成功的能力”，而不是简单重复配置。例如请求 1280×720@30 MJPEG，设备实际返回 640×480@30 YUYV 时，必须向上层显式报告并由应用决定是否接受降级。

## 8. 模块职责

### 8.1 Application

- 加载和验证配置。
- 根据编译特性和配置工厂创建 Mock/Linux/Rockchip 后端。
- 按固定顺序启动管道，在任一步失败时反向回滚。
- 响应 SIGINT/SIGTERM，统一发起停止。
- 不包含 V4L2 ioctl、ALSA PCM、RKNN 或 MPP 调用。

### 8.2 Capture

- 完成设备枚举、能力协商、开启流和超时读取。
- V4L2 使用 MMAP 时，每次 DQBUF 后必须在约定时机 QBUF，不能将驱动缓冲区无限期传到异步下游。
- ALSA 设备不使用可变的 card 序号作为长期标识，应支持按设备名、USB VID/PID 或 udev 别名选择。

### 8.3 Media

- 将采集格式解码/转换为统一内部格式，建议编码主链路使用 NV12，模型根据需要使用 RGB/BGR。
- MPP 负责 H.264 硬编码，FFmpeg 负责 AAC 音频编码与格式封装。
- 编码器必须支持 `Flush()`，停止时排空延迟包。

### 8.4 Vision

- `Preprocessor` 产生模型输入和 `TransformMetadata`，其中保存 scale、padding、通道顺序和量化信息。
- `InferenceEngine` 只负责模型加载和推理调用。
- `Postprocessor` 负责解码输出 tensor、反量化、置信度过滤、NMS 和坐标还原。
- `Overlay` 不依赖 RKNN tensor，只接收通用的 `DetectionBatch`。

### 8.5 Output

- `PacketRouter` 对编码包进行序列化分发，防止录像与推流同时修改同一个 packet。
- `Recorder` 使用临时文件名录制，正常写 trailer 后原子更名；异常退出不得覆盖上一个完整文件。
- `Streamer` 对网络错误执行指数退避重连，不阻塞本地采集和录像。

### 8.6 Monitor

- 日志至少包含时间、级别、模块、事件、设备标识、错误码和恢复动作。
- 指标对多线程写入必须是线程安全的；性能主链路优先使用原子计数器，避免全局大锁。
- 健康监测使用“最近成功时间”和连续失败次数判断卡死，不仅判断线程是否存活。

## 9. 线程模型和队列策略

| 线程/执行单元 | 输入 | 输出 | 建议队列 | 队列满时 |
|---|---|---|---:|---|
| VideoCapture | V4L2 | 采集帧 | 4 | 丢最旧帧，计数 |
| Decode/Normalize | MJPEG/YUYV | NV12/RGB | 2–4 | 丢过时视频帧 |
| Inference | 模型输入 | DetectionBatch | 1–2 | 只保留最新待推理帧 |
| Overlay/VideoEncode | 图像+最新有效结果 | H.264 | 2–4 | 记录降级；直播可丢视频帧 |
| AudioCapture | ALSA PCM | AudioFrame | 按 200–500 ms 容量配置 | 不静默丢弃；触发告警/恢复 |
| AudioEncode | AudioFrame | AAC | 按编码延迟确定 | 优先保证音频连续 |
| Mux/Stream | H.264 + AAC | MP4/RTSP | 8–16 packets | 分录像与直播策略 |

队列长度必须可配置并暴露实时深度和高水位指标。如果队列长期达到高水位，说明下游处理能力不足，不应通过继续加长队列隐藏问题。

## 10. 启动、停止与错误恢复

### 10.1 启动顺序

1. 加载配置并完成 schema/语义校验。
2. 初始化日志、指标和信号处理。
3. 枚举设备，打印请求能力与实际能力。
4. 加载 RKNN 模型并验证输入/输出 tensor 信息。
5. 打开视频、音频采集设备。
6. 初始化预处理、编码器、封装器和推流器。
7. 从最下游向最上游启动消费线程，最后启动采集。
8. 进入 `RUNNING`，开始健康监测。

### 10.2 停止顺序

1. 设置全局停止令牌，禁止生产新数据。
2. 关闭或中断可能长时间阻塞的设备读取。
3. 关闭队列并唤醒所有等待线程。
4. 等待采集、推理和编码线程退出。
5. Flush 音视频编码器。
6. 将剩余 packet 写入封装器，写入 MP4 trailer。
7. 关闭 RTSP 连接、释放 MPP/RKNN/RGA/FFmpeg 资源。
8. 写入最终指标、停止原因和退出码。

### 10.3 摄像头恢复状态机

```text
RUNNING
  -> CAMERA_ERROR
  -> RELEASE_DEVICE
  -> RECONNECT_WAIT
  -> REINITIALIZING
  -> RUNNING

连续恢复失败达阈值
  -> DEGRADED_AUDIO_ONLY / RECORDING_DISABLED / FATAL
```

恢复过程必须有最大频率和退避，避免设备不存在时无限快速重试。

## 11. 配置模型

所有环境差异通过配置注入，不在业务代码中写死。一个完整配置示例：

```json
{
  "runtime": {
    "mode": "hardware",
    "log_level": "info",
    "shutdown_timeout_ms": 8000
  },
  "video": {
    "backend": "v4l2",
    "device_selector": { "udev_alias": "rkav-camera" },
    "width": 1280,
    "height": 720,
    "fps": 30,
    "preferred_formats": ["MJPEG", "YUYV"],
    "capture_timeout_ms": 1000,
    "queue_capacity": 4
  },
  "audio": {
    "backend": "alsa",
    "device_selector": { "name_contains": "USB" },
    "sample_rate": 48000,
    "channels": 1,
    "format": "S16_LE",
    "frame_duration_ms": 20,
    "queue_capacity_ms": 300
  },
  "inference": {
    "backend": "rknn",
    "model_path": "/opt/rkav/models/detector.rknn",
    "input_width": 320,
    "input_height": 320,
    "color_order": "RGB",
    "confidence_threshold": 0.35,
    "nms_threshold": 0.45,
    "max_fps": 15,
    "max_result_age_ms": 200
  },
  "video_encoder": {
    "backend": "mpp",
    "codec": "h264",
    "bitrate_bps": 3000000,
    "gop": 30,
    "profile": "high"
  },
  "audio_encoder": {
    "backend": "ffmpeg",
    "codec": "aac",
    "bitrate_bps": 128000
  },
  "recording": {
    "enabled": true,
    "directory": "/var/lib/rkav/recordings",
    "segment_seconds": 600,
    "minimum_free_bytes": 2147483648
  },
  "streaming": {
    "enabled": true,
    "url": "rtsp://127.0.0.1:8554/rkav",
    "reconnect_min_ms": 500,
    "reconnect_max_ms": 10000
  },
  "monitoring": {
    "metrics_interval_ms": 1000,
    "health_timeout_ms": 3000
  }
}
```

配置校验必须拒绝不可能或自相矛盾的值，如负队列长度、0 FPS、不支持的像素格式、录像目录不可写等。

## 12. 编译和特性开关

建议使用下列 CMake 选项：

```text
RKAV_BUILD_TESTS=ON
RKAV_ENABLE_MOCK=ON
RKAV_WITH_V4L2=OFF/ON
RKAV_WITH_ALSA=OFF/ON
RKAV_WITH_FFMPEG=OFF/ON
RKAV_WITH_RKNN=OFF/ON
RKAV_WITH_RGA=OFF/ON
RKAV_WITH_MPP=OFF/ON
RKAV_ENABLE_ASAN=OFF/ON
RKAV_ENABLE_UBSAN=OFF/ON
```

默认配置必须能在没有 Rockchip SDK 的 x86_64 Linux 上编译 Mock 版。启用 RKNN/RGA/MPP 时再检测板卡 SDK 的头文件、动态库和版本。

不允许为了让 PC 通过编译，在业务源码中到处散落 `#ifdef RK3568`。平台差异应被限制在后端实现和工厂组装处。

## 13. 测试架构

### 13.1 单元测试

- 有界队列：FIFO、超时、关闭唤醒、丢弃策略和多生产者/消费者。
- 时钟和 time-base 转换：单调性、溢出边界和四舍五入。
- letterbox 和坐标还原：横图、竖图、边界框和裁剪。
- NMS：空输入、同类重叠、不同类重叠和置信度边界。
- 配置：默认值、缺失键、非法值和版本迁移。
- 状态机：正常运行、重连、降级、恢复和停止。

### 13.2 Mock 集成测试

- 30 FPS Mock 视频 + 48 kHz Mock 音频运行 10 分钟。
- 人为将 Mock 推理延迟设为 100 ms，验证视频采集不被拖死、队列有界且丢帧计数正确。
- 注入摄像头读取失败、网络中断和磁盘空间不足，验证状态机。
- 随机时刻发送停止请求，验证所有线程在超时内退出。

### 13.3 硬件集成测试

- V4L2 指定格式采集 300 帧，校验帧数、超时和文件可解码性。
- ALSA 采集 10 秒 WAV，校验样本数、采样率和可回放性。
- RKNN 使用固定图像与 PC 参考输出对比。
- MP4 使用 `ffprobe` 校验 codec、分辨率、时长、PTS/DTS 单调性和音视频时差。
- RTSP 使用 VLC/ffplay 播放，测量端到端延迟与断线重连。

### 13.4 稳定性和故障注入

- 至少 2 小时稳定运行，可扩展为 12 小时。
- 记录 CPU、内存、温度、队列深度、丢帧、XRUN、重连和 P95 延迟。
- 运行中拔插摄像头、麦克风和网线。
- 停止并恢复 RTSP 服务器。
- 将可用磁盘空间降到阈值附近。
- 发送 SIGTERM，验证 MP4 完整结尾且无死锁。

## 14. 指标与验收口径

至少输出以下指标：

```text
capture_video_fps
capture_audio_frames_per_sec
decode_fps
inference_fps
encode_fps
end_to_end_latency_ms
preprocess_latency_ms
inference_latency_ms
encode_latency_ms
dropped_video_frames_total
audio_xruns_total
queue_depth{name=...}
queue_high_watermark{name=...}
camera_reconnects_total
stream_reconnects_total
temperature_celsius
process_memory_bytes
recording_free_bytes
```

第一版验收以实测为准，不预先承诺未测的绝对延迟数字。最低验收内容是：

- 能对支持的 UVC/UAC 设备完成能力协商和采集。
- 输出可播放的 H.264/AAC MP4，无明显音画不同步。
- RTSP 可在同网段播放，断网后可自动重连。
- RKNN 结果可正确映射到原始画面。
- 持续运行 2 小时，无崩溃、无死锁、无无界内存增长。
- 拔掉摄像头时服务不崩溃，重插后恢复或进入有明确日志的降级状态。
- SIGTERM 能在配置的超时内完成优雅退出。

## 15. 分阶段实施顺序

1. **工程基础**：CMake、公共类型、Result/Error、时钟、日志、配置和单元测试。
2. **Mock 流水线**：Mock 音视频、有界队列、Mock 推理、Mock 编码/输出、指标和优雅退出。
3. **真实采集**：V4L2 与 ALSA，先各自独立测试，后双路并行。
4. **软件媒体基线**：使用 FFmpeg 完成可播 MP4，先验证 PTS 和音画同步。
5. **RKNN 推理**：固定图像、视频帧、坐标还原和 overlay。
6. **MPP/RGA 优化**：用硬件编码替换软件基线，对比 CPU、延迟和输出正确性。
7. **RTSP 和恢复**：将网络输出与本地录像解耦，加入重连和故障注入。
8. **systemd 与长稳**：守护运行、日志轮转、2/12 小时稳定性测试。

每一阶段都必须保留上一阶段的可运行基线，不得在引入 RKNN/MPP/RGA 后失去 Mock 测试能力。

## 16. 后续优化路线

在完成正确性和性能基线后，按证据选择优化项：

1. 用 RGA 替换 CPU resize/颜色转换。
2. 降低推理频率或使用最新帧策略，在准确率与延迟间取舍。
3. 复用 Buffer Pool，减少频繁堆分配。
4. 将 CPU 内存链路逐步升级为 DMA-BUF/MPP Buffer，每次只替换一段并保留回退实现。
5. 增加 NVMe 循环录像、文件保留策略和空间预留。
6. 扩展 MIPI CSI、多摄像头或新协议时，通过新后端接入，不修改已稳定的业务契约。

## 17. 代码审查必查项

- 是否把设备节点、ALSA card 号或模型路径写死。
- 是否存在无界队列或无限重试。
- 是否混用单调时钟和系统时钟。
- 是否在 V4L2 buffer 尚未拷贝/转移所有权时就 QBUF。
- 是否对 ALSA XRUN、短读和设备断开有明确处理。
- 是否使用了错误帧的检测结果。
- 是否在停止时能唤醒所有阻塞等待。
- 是否正确 flush 编码器、写 MP4 trailer 并记录退出原因。
- 新增硬件优化是否有优化前后指标对比。
- Mock 版和单元测试是否仍然可运行。
