// 文件作用：声明整个音视频管道的组装、线程模型、生命周期和健康监控中心。
// 主要知识点：依赖倒置、jthread/stop_token、原子状态机、有界队列、优雅排空和错误汇聚。
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "rkav/capture/audio_capture.h"
#include "rkav/capture/video_capture.h"
#include "rkav/common/clock.h"
#include "rkav/config/config.h"
#include "rkav/media/audio_encoder.h"
#include "rkav/media/video_decoder.h"
#include "rkav/media/video_encoder.h"
#include "rkav/monitor/metrics.h"
#include "rkav/output/packet_router.h"
#include "rkav/queue/bounded_queue.h"
#include "rkav/vision/inference_engine.h"

namespace rkav {

enum class ApplicationState { kCreated, kInitializing, kRunning, kStopping, kStopped, kFailed };

// Application 只负责后端组装和生命周期，不包含任何具体硬件 SDK 类型。
class Application {
   public:
    /// 保存强类型配置并注入时钟；clock 为空时自动创建 SteadyClock。
    explicit Application(AppConfig config, std::shared_ptr<IClock> clock = nullptr);
    /// 析构时使用 Abort 兜底，保证所有线程和后端都被回收。
    ~Application();

    /// 按后端、路由、worker 顺序启动；只能调用一次。
    Result<void> Start();
    /// 线程安全地记录第一个停止原因并唤醒 Wait；允许多线程重复调用。
    void RequestStop(std::string reason) noexcept;
    /// 阻塞到停止请求，随后按正常/异常模式回收资源并返回最终结果。
    Result<void> Wait();

    /// 返回当前生命周期状态。
    [[nodiscard]] ApplicationState state() const noexcept;
    /// 返回是否已经收到任意停止请求。
    [[nodiscard]] bool stop_requested() const noexcept;
    /// 收集各队列当前状态并返回完整指标 JSON。
    [[nodiscard]] nlohmann::json MetricsSnapshot();
    /// 返回运行期间最先记录的致命错误。
    [[nodiscard]] std::optional<Error> fatal_error() const;

   private:
    /// 根据配置创建并依次打开采集、推理和编码后端；失败时反向回滚。
    Result<void> CreateAndOpenBackends();
    /// 创建所有启用的 Sink 并启动 PacketRouter。
    Result<void> CreateRouter();
    /// 先创建消费者线程，再创建采集和监控线程。
    Result<void> StartWorkers();
    /// 按采集、处理、输出三阶段停止并 join 全部线程。
    void StopWorkers(CloseMode mode) noexcept;
    /// 仅保存第一个致命错误，并请求全局停止。
    void ReportFatal(Error error) noexcept;
    /// 把三个处理队列及 Router 队列状态写入指标仓库。
    void CollectQueueMetrics();
    /// 检查 Running worker 距离上次成功进展是否超过阈值。
    [[nodiscard]] std::optional<Error> DetectStalledWorker(TimestampUs now_us) const;

    /// 视频采集线程：一帧同时分发给推理和视频编码队列。
    void VideoCaptureLoop(std::stop_token stop);
    /// 音频采集线程：负责 XRUN 恢复并保证音频不静默丢块。
    void AudioCaptureLoop(std::stop_token stop);
    /// 推理线程：消费最新视频帧并发布不可变检测结果快照。
    void InferenceLoop(std::stop_token stop);
    /// 视频编码线程：检查检测结果年龄、编码并投递视频包。
    void VideoEncodeLoop(std::stop_token stop);
    /// 音频编码线程：编码连续 PCM 块并投递音频包。
    void AudioEncodeLoop(std::stop_token stop);
    /// 监控线程：按独立周期输出指标、转发 Router 错误并检测卡死。
    void MonitorLoop(std::stop_token stop);

    AppConfig config_;               // 本次进程运行期间不再修改的配置。
    std::shared_ptr<IClock> clock_;  // 所有媒体和 worker 共享的时间源。
    MetricsRegistry metrics_;        // 全管道共享指标仓库。
    std::atomic<ApplicationState> state_{ApplicationState::kCreated};  // 生命周期状态。
    std::atomic_bool stop_requested_{false};    // 是否已经请求停止。
    mutable std::mutex status_mutex_;           // 保护停止原因和致命错误。
    std::condition_variable status_condition_;  // 唤醒正在 Wait 的主线程。
    std::string stop_reason_{"not_stopped"};    // 第一个停止原因。
    std::optional<Error> fatal_error_;          // 第一个致命错误。

    std::unique_ptr<IVideoCapture> video_capture_;  // 视频采集后端唯一所有者。
    std::unique_ptr<IAudioCapture> audio_capture_;  // 音频采集后端唯一所有者。
    std::unique_ptr<IInferenceEngine> inference_;   // 推理后端唯一所有者。
    std::unique_ptr<IVideoDecoder> video_decoder_;  // 可选压缩帧解码器唯一所有者。
    std::unique_ptr<IVideoEncoder> video_encoder_;  // 视频编码器唯一所有者。
    std::unique_ptr<IAudioEncoder> audio_encoder_;  // 音频编码器唯一所有者。
    std::unique_ptr<PacketRouter> router_;          // 编码包输出路由器。

    std::unique_ptr<BoundedQueue<VideoFrame>> inference_queue_;     // 等待推理的视频帧。
    std::unique_ptr<BoundedQueue<VideoFrame>> video_encode_queue_;  // 等待编码的视频帧。
    std::unique_ptr<BoundedQueue<AudioFrame>> audio_encode_queue_;  // 等待编码的 PCM 块。

    std::jthread video_capture_thread_;  // 视频采集 worker。
    std::jthread audio_capture_thread_;  // 音频采集 worker。
    std::jthread inference_thread_;      // 推理 worker。
    std::jthread video_encode_thread_;   // 视频编码 worker。
    std::jthread audio_encode_thread_;   // 音频编码 worker。
    std::jthread monitor_thread_;        // 指标和健康监控 worker。

    WorkerHealth video_capture_health_;  // 视频采集 worker 健康状态。
    WorkerHealth audio_capture_health_;  // 音频采集 worker 健康状态。
    WorkerHealth inference_health_;      // 推理 worker 健康状态。
    WorkerHealth video_encode_health_;   // 视频编码 worker 健康状态。
    WorkerHealth audio_encode_health_;   // 音频编码 worker 健康状态。

    mutable std::mutex detection_mutex_;  // 保护最新检测结果指针的替换和复制。
    std::shared_ptr<const DetectionBatch> latest_detection_;  // 不可变检测结果快照。
};

/// 把 Application 生命周期状态转换成稳定文本。
const char* ToString(ApplicationState state) noexcept;

}  // namespace rkav
