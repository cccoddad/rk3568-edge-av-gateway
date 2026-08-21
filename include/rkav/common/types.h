// 文件作用：定义视频帧、音频帧、检测结果和编码包等跨模块公共数据模型。
// 主要知识点：媒体格式、stride、PTS/DTS、time_base、共享缓冲区和数据不变量。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rkav/common/buffer.h"
#include "rkav/common/result.h"

namespace rkav {

// 管道内部统一使用微秒级单调时间，不使用可能被系统校时修改的墙上时钟。
using TimestampUs = std::int64_t;

enum class PixelFormat { kUnknown, kMjpeg, kYuyv422, kNv12, kRgb888, kBgr888 };
enum class SampleFormat { kS16LE, kF32LE };
enum class MemoryKind { kCpu, kDmaBuf, kMppBuffer };
enum class StreamKind { kVideo, kAudio };
enum class Codec { kUnknown, kMockVideoChecksum, kMockAudioChecksum, kH264, kAac };

struct Rational {
    std::int32_t numerator{1};    // 时间基分子，例如 1/90000 中的 1。
    std::int32_t denominator{1};  // 时间基分母，例如 1/90000 中的 90000。
};

struct FrameMemory {
    // CPU 内存用于 Mock；真实硬件可用 dma_buf_fd 描述 DMA-BUF，从而减少像素复制。
    MemoryKind kind{MemoryKind::kCpu};
    int dma_buf_fd{-1};  // Linux DMA-BUF 文件描述符；CPU 内存时保持 -1。
};

struct VideoFrame {
    std::uint64_t sequence{0}; // 从 0 开始的采集顺序号。
    TimestampUs pts_us{0};     // 统一单调时间轴上的显示时间，单位微秒。
    int width{0};              // 有效图像宽度，单位像素。
    int height{0};             // 有效图像高度，单位像素。
    int stride{0};  // 每行实际占用字节数，可能大于 width * 每像素字节数。
    PixelFormat format{PixelFormat::kUnknown}; // 像素排列格式。
    std::shared_ptr<Buffer> buffer;             // 像素数据共享所有权。
    FrameMemory memory;                         // CPU/DMA/MPP 内存来源描述。
};

struct AudioFrame {
    std::uint64_t sequence{0}; // 从 0 开始的音频块顺序号。
    TimestampUs pts_us{0};     // 本块首样本时间，单位微秒。
    int sample_rate{0};        // 每秒每声道样本数，单位 Hz。
    int channels{0};           // 声道数量。
    int samples_per_channel{0};  // 本块中每个声道各有多少个采样点。
    SampleFormat format{SampleFormat::kS16LE}; // 样本存储类型。
    std::shared_ptr<Buffer> buffer; // 交错 PCM 数据，例如 L0,R0,L1,R1...
};

struct RectF {
    float x{0.0F};      // 左上角横坐标。
    float y{0.0F};      // 左上角纵坐标。
    float width{0.0F};  // 矩形宽度。
    float height{0.0F}; // 矩形高度。
};

struct Detection {
    int class_id{0};          // 模型类别编号。
    float confidence{0.0F};   // 置信度，通常范围 0.0 到 1.0。
    RectF box_in_source;      // 已映射到原始图像坐标的目标框。
};

struct DetectionBatch {
    std::uint64_t frame_sequence{0}; // 来源视频帧序号。
    TimestampUs source_pts_us{0};    // 来源视频帧 PTS，单位微秒。
    TimestampUs completed_at_us{0};  // 推理完成时间，用于计算延迟和结果年龄。
    std::vector<Detection> items;    // 本帧的全部检测目标。
};

struct EncodedPacket {
    StreamKind kind{StreamKind::kVideo}; // 包属于视频流还是音频流。
    std::uint64_t source_sequence{0};    // 来源帧或音频块序号。
    std::int64_t pts{0}; // 显示时间戳；单位由 time_base 决定。
    std::int64_t dts{0}; // 解码时间戳；单位由 time_base 决定。
    Rational time_base;  // pts/dts 每增加 1 对应的真实时间长度。
    bool key_frame{false};          // 视频是否关键帧；Mock 音频固定为 true。
    Codec codec{Codec::kUnknown};   // payload 的编码格式。
    std::shared_ptr<Buffer> buffer; // 编码 payload 共享所有权。
};

// 以下校验函数是所有真实后端和 Mock 后端共同的数据边界保护。
const char* ToString(PixelFormat format) noexcept;
const char* ToString(SampleFormat format) noexcept;
const char* ToString(StreamKind kind) noexcept;
const char* ToString(Codec codec) noexcept;

/// 校验视频尺寸、stride、格式、PTS 和 Buffer 大小是否一致。
Result<void> ValidateVideoFrame(const VideoFrame& frame);
/// 校验音频参数、样本布局、PTS 和 Buffer 大小是否一致。
Result<void> ValidateAudioFrame(const AudioFrame& frame);
/// 校验编码包的 payload、时间基以及媒体类型与编码格式是否匹配。
Result<void> ValidatePacket(const EncodedPacket& packet);

}  // namespace rkav
