// 文件作用：定义 JSON 配置对应的强类型结构，并声明加载、解析、语义校验接口。
// 主要知识点：配置分层、默认值、optional 可选项、schema 版本和跨字段约束。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/queue/bounded_queue.h"

namespace rkav {

struct RuntimeConfig {
    std::string mode{"mock"};       // 当前只支持 mock；以后可扩展 hardware。
    std::string log_level{"info"};  // 最低输出日志级别。
    int shutdown_timeout_ms{5000};  // 优雅停止的期望上限，单位毫秒。
    int run_duration_seconds{0};    // 0 表示持续运行直到信号或故障。
};

struct VideoFailureConfig {
    // 从指定读取次数开始连续注入失败，用于稳定复现设备断开场景。
    std::optional<std::uint64_t> fail_after_frames;  // 从第几次 Read 开始失败。
    std::uint64_t fail_for_frames{0};                // 连续失败多少次。
    int read_delay_ms{0};                            // 额外读取延迟，单位毫秒。
};

struct VideoConfig {
    std::string backend{"mock"};               // 视频后端名称。
    std::string device{"/dev/video0"};         // V4L2 设备节点；Mock 后端忽略。
    int width{1280};                           // 帧宽，单位像素。
    int height{720};                           // 帧高，单位像素。
    int fps{30};                               // 目标帧率。
    PixelFormat format{PixelFormat::kRgb888};  // 期望像素格式。
    int capture_timeout_ms{1000};              // V4L2 等待一帧的最长时间。
    std::size_t mmap_buffer_count{4};          // V4L2 驱动 MMAP 缓冲数量。
    std::size_t queue_capacity{4};             // 视频编码队列最多容纳的帧数。
    OverflowPolicy overflow_policy{OverflowPolicy::kDropOldest};  // 满队列处理方式。
    std::string pattern{"moving_box"};                            // Mock 测试图样式。
    bool realtime{true};         // false 时不等待真实节拍，主要供单元测试使用。
    VideoFailureConfig failure;  // 视频故障注入参数。
};

struct AudioFailureConfig {
    // XRUN 是可恢复故障；disconnect_after_blocks 模拟持续失联。
    std::optional<std::uint64_t> xrun_every_blocks;        // 每 N 块注入一次 XRUN。
    std::optional<std::uint64_t> disconnect_after_blocks;  // 从第 N 块起持续失联。
};

struct AudioConfig {
    std::string backend{"mock"};   // 音频后端名称：mock 或 alsa。
    std::string device{"hw:0,0"};  // ALSA 设备，格式 hw:CARD,DEVICE；Mock 后端忽略。
    int sample_rate{48000};        // 每秒每声道采样点数，单位 Hz。
    int channels{1};               // 声道数。
    SampleFormat format{SampleFormat::kS16LE};  // PCM 样本格式。
    int frame_duration_ms{20};                  // 每个 AudioFrame 覆盖的时长。
    int capture_timeout_ms{1000};               // ALSA 等待一个 PCM 块的最长时间。
    int queue_capacity_ms{300};                 // 音频队列可缓存的总时长。
    std::string signal{"sine"};                 // Mock 信号类型：sine 或 silence。
    double frequency_hz{1000.0};                // 正弦波频率。
    double amplitude{0.25};                     // 归一化幅度，范围 0.0 到 1.0。
    bool realtime{true};                        // 是否按真实音频节拍等待。
    AudioFailureConfig failure;                 // 音频故障注入参数。
};

struct InferenceConfig {
    std::string backend{"mock"};           // 推理后端名称。
    std::string mode{"synthetic_target"};  // Mock 结果生成模式。
    std::string model_path;                // RKNN 模型路径；Mock 后端忽略。
    int input_width{320};                  // 模型输入宽度。
    int input_height{320};                 // 模型输入高度。
    int latency_ms{20};                    // Mock 单次推理延迟。
    int max_fps{0};                        // 最大推理帧率；0 表示不主动限速。
    float object_threshold{0.25F};         // YOLOv5 候选目标阈值。
    float nms_threshold{0.45F};            // YOLOv5 同类别 NMS 阈值。
    std::size_t max_detections{100};       // 单帧最多返回的检测框数量。
    std::size_t queue_capacity{1};         // 等待推理的视频帧数上限。
    OverflowPolicy overflow_policy{OverflowPolicy::kKeepLatest};  // 推理积压处理方式。
    int max_result_age_ms{200};  // 检测结果超过该年龄视为过期。
};

struct VideoEncoderConfig {
    std::string backend{"checksum"};  // 视频编码后端名称。
    int mock_keyframe_interval{30};   // 每隔多少视频帧标记一个关键帧。
    std::string codec_name{"libx264"};  // FFmpeg encoder 名称，可替换为 libopenh264。
    int bitrate_bps{3'000'000};         // H.264 目标码率。
    int gop_size{30};                    // 相邻关键帧的最大帧数。
    std::string preset{"veryfast"};     // 支持 preset 的编码器使用的速度/压缩率取舍。
};

struct AudioEncoderConfig {
    std::string backend{"checksum"};  // 音频编码后端名称。
    std::string codec_name{"aac"};    // FFmpeg encoder 名称；aac 为原生 AAC-LC。
    int bitrate_bps{128'000};          // AAC 目标码率。
};

struct OutputConfig {
    std::string type{"null"};  // 输出类型：null、jsonl 或 mp4。
    bool enabled{true};        // false 时完全跳过该输出。
    // 必需输出失败会停止整个应用；非必需输出失败时只隔离该输出。
    bool required{true};
    bool validate_timestamps{true};  // 是否检查每路 DTS 不倒退。
    std::string path;                // JSONL 等文件输出路径。
    std::size_t queue_capacity{16};  // 此输出独立队列的包数量上限。
    OverflowPolicy overflow_policy{OverflowPolicy::kDropOldest};  // 满队列处理方式。
    int push_timeout_ms{0};  // block_producer 最长等待时间；0 表示不等待。
    int write_delay_ms{0};  // 每包人工写入延迟，用于模拟慢输出。
    std::optional<std::uint64_t> fail_after_packets;  // 写入 N 包后注入故障。
};

struct MonitoringConfig {
    int metrics_interval_ms{1000};      // 周期指标日志间隔。
    int health_interval_ms{500};        // worker 健康检查间隔。
    int worker_stall_timeout_ms{3000};  // 无成功进展多久判定卡死。
};

struct AppConfig {
    int schema_version{1};              // 配置结构版本，当前只支持 1。
    RuntimeConfig runtime;              // 进程级运行参数。
    VideoConfig video;                  // 视频采集参数。
    AudioConfig audio;                  // 音频采集参数。
    InferenceConfig inference;          // 推理参数。
    VideoEncoderConfig video_encoder;   // 视频编码参数。
    AudioEncoderConfig audio_encoder;   // 音频编码参数。
    std::vector<OutputConfig> outputs;  // 一个或多个独立输出。
    MonitoringConfig monitoring;        // 指标和健康检查参数。
};

class ConfigLoader {
   public:
    /// 从文件读取 JSON，随后执行 Parse 和 Validate。
    static Result<AppConfig> LoadFromFile(const std::string& path);
    /// 把 JSON 文本转换为强类型配置；未知字段不会被静默忽略。
    static Result<AppConfig> Parse(std::string_view json_text);
    /// 检查范围以及字段间关系，不执行文件或硬件 I/O。
    static Result<void> Validate(const AppConfig& config);
};

/// 生成适合启动日志的配置摘要，故意不输出所有故障注入细节。
std::string ConfigSummary(const AppConfig& config);

}  // namespace rkav
