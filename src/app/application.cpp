// 文件作用：实现整个 Mock 音视频管道的组装、六个 worker、停止顺序和健康检测。
// 主要知识点：状态机、jthread、stop_token、生产者/消费者、有界队列、回滚和错误汇聚。
#include "rkav/app/application.h"

#include <chrono>
#include <exception>
#include <limits>
#include <utility>

#include "rkav/capture/mock_audio_capture.h"
#include "rkav/capture/mock_video_capture.h"
#if RKAV_WITH_ALSA
#include "rkav/capture/alsa_audio_capture.h"
#endif
#if RKAV_WITH_V4L2
#include "rkav/capture/v4l2_video_capture.h"
#endif
#if RKAV_WITH_RKNN
#include "rkav/vision/rknn_inference_engine.h"
#endif
#include "rkav/common/logger.h"
#include "rkav/media/checksum_encoder.h"
#if RKAV_WITH_FFMPEG
#include "rkav/media/ffmpeg_encoder.h"
#endif
#if RKAV_WITH_JPEG
#include "rkav/media/jpeg_video_decoder.h"
#endif
#include "rkav/output/sinks.h"
#include "rkav/vision/mock_inference_engine.h"

namespace rkav {
namespace {

/// 功能：构造 application 模块统一错误。
Error AppError(ErrorCategory category, std::string_view operation, std::string message,
               bool retryable = false) {
    return Error{category, 0, "application", std::string(operation), std::move(message), retryable};
}

/// 功能：把 worker 错误统一写为结构化日志，并带上 worker 名称。
void LogWorkerError(std::string_view worker, const Error& error) {
    Logger::Instance().Log(LogLevel::kError, "application", "worker_error", DescribeError(error),
                           {{"worker", std::string(worker)}});
}

/// 功能：可取消地等待固定退避时间；停止请求到达时返回 false。
bool WaitBeforeRetry(const std::shared_ptr<IClock>& clock, std::stop_token stop) {
    // 设备持续失联时必须退避，避免错误重试循环占满一个 CPU 核心。
    constexpr TimestampUs kRetryBackoffUs = 100'000;
    return clock->WaitUntil(clock->NowUs() + kRetryBackoffUs, stop);
}

}  // namespace

/// 功能：保存配置并选择外部时钟或默认 SteadyClock；此时尚未创建后端。
Application::Application(AppConfig config, std::shared_ptr<IClock> clock)
    : config_(std::move(config)),
      clock_(clock ? std::move(clock) : std::make_shared<SteadyClock>()),
      metrics_() {}

/// 功能：请求停止并用 Abort 兜底回收资源，防止调用方忘记 Wait。
Application::~Application() {
    RequestStop("destructor");
    StopWorkers(CloseMode::kAbort);
}

/// 功能：按后端、Router、worker 顺序启动；任何阶段失败都会回滚。
Result<void> Application::Start() {
    // CAS 保证生命周期只能从 Created 进入 Initializing，拒绝重复启动。
    ApplicationState expected = ApplicationState::kCreated;  // CAS 期望的唯一合法前置状态。
    if (!state_.compare_exchange_strong(expected, ApplicationState::kInitializing,
                                        std::memory_order_acq_rel)) {
        return Result<void>::Failure(
            AppError(ErrorCategory::kInvalidState, "start", "application was already started"));
    }

    // 按依赖顺序启动；任一步失败都立即反向关闭已经创建的资源。
    auto result = CreateAndOpenBackends();  // 当前启动阶段的结果，后续复用该变量。
    if (!result) {
        state_.store(ApplicationState::kFailed, std::memory_order_release);
        StopWorkers(CloseMode::kAbort);
        return result;
    }
    result = CreateRouter();
    if (!result) {
        state_.store(ApplicationState::kFailed, std::memory_order_release);
        StopWorkers(CloseMode::kAbort);
        return result;
    }
    result = StartWorkers();
    if (!result) {
        state_.store(ApplicationState::kFailed, std::memory_order_release);
        StopWorkers(CloseMode::kAbort);
        return result;
    }

    state_.store(ApplicationState::kRunning, std::memory_order_release);
    Logger::Instance().Log(LogLevel::kInfo, "application", "application_running",
                           "audio-video pipeline is running");
    return Result<void>::Success();
}

/// 功能：原子地记录第一次停止请求，并唤醒主线程 Wait。
void Application::RequestStop(std::string reason) noexcept {
    // 只接受第一个停止原因，使多线程同时报错时仍能保留最初根因。
    bool expected = false;  // CAS 只允许第一个调用者从 false 改为 true。
    if (stop_requested_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        {
            std::scoped_lock lock(status_mutex_);
            stop_reason_ = std::move(reason);
        }
        status_condition_.notify_all();
    }
}

/// 功能：等待停止请求，再选择 Drain 或 Abort 模式完成资源回收。
Result<void> Application::Wait() {
    {
        std::unique_lock lock(status_mutex_);
        status_condition_.wait(lock,
                               [this] { return stop_requested_.load(std::memory_order_acquire); });
    }
    // 正常退出排空数据；致命错误优先快速终止，避免继续传播已不可信的数据。
    const CloseMode mode =
        fatal_error().has_value() ? CloseMode::kAbort : CloseMode::kDrain;  // 最终关闭模式。
    StopWorkers(mode);

    if (auto error = fatal_error(); error.has_value()) {
        return Result<void>::Failure(*error);
    }
    return Result<void>::Success();
}

/// 功能：无锁读取当前生命周期状态。
ApplicationState Application::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

/// 功能：无锁读取停止标志。
bool Application::stop_requested() const noexcept {
    return stop_requested_.load(std::memory_order_acquire);
}

/// 功能：刷新队列指标后返回全管道 JSON 快照。
nlohmann::json Application::MetricsSnapshot() {
    CollectQueueMetrics();
    return metrics_.Snapshot();
}

/// 功能：线程安全地复制第一个致命错误。
std::optional<Error> Application::fatal_error() const {
    std::scoped_lock lock(status_mutex_);
    return fatal_error_;
}

/// 功能：根据配置创建后端、依次 Open，并建立三个处理队列。
Result<void> Application::CreateAndOpenBackends() {
    // 工厂选择集中在此处，Application 的 worker 不依赖具体 Mock 或硬件实现。
#if RKAV_ENABLE_MOCK
    if (config_.video.backend == "mock") {
        video_capture_ = std::make_unique<MockVideoCapture>(clock_);
    }
    if (config_.audio.backend == "mock") {
        audio_capture_ = std::make_unique<MockAudioCapture>(clock_);
    }
    if (config_.inference.backend == "mock") {
        inference_ = std::make_unique<MockInferenceEngine>(clock_);
    }
    if (config_.video_encoder.backend == "checksum") {
        video_encoder_ = std::make_unique<ChecksumVideoEncoder>();
    }
    if (config_.audio_encoder.backend == "checksum") {
        audio_encoder_ = std::make_unique<ChecksumAudioEncoder>();
    }
#endif
#if RKAV_WITH_FFMPEG
    if (config_.video_encoder.backend == "ffmpeg") {
        video_encoder_ = std::make_unique<FfmpegVideoEncoder>();
    }
    if (config_.audio_encoder.backend == "ffmpeg") {
        audio_encoder_ = std::make_unique<FfmpegAudioEncoder>();
    }
#endif
#if RKAV_WITH_V4L2
    if (config_.video.backend == "v4l2") {
        video_capture_ = std::make_unique<V4L2VideoCapture>(clock_);
    }
#endif
#if RKAV_WITH_ALSA
    if (config_.audio.backend == "alsa") {
        audio_capture_ = std::make_unique<AlsaAudioCapture>(clock_);
    }
#endif
#if RKAV_WITH_RKNN
    if (config_.inference.backend == "rknn") {
        inference_ = std::make_unique<RknnInferenceEngine>(clock_);
    }
#endif
    if (!video_capture_ || !audio_capture_ || !inference_ || !video_encoder_ || !audio_encoder_) {
        return Result<void>::Failure(
            AppError(ErrorCategory::kNotSupported, "create_backends",
                     "one or more configured backends are not compiled in"));
    }

    // Open 采用正向顺序，失败回滚采用严格反向顺序。
    auto video_opened = video_capture_->Open(config_.video);  // 视频后端打开/协商结果。
    if (!video_opened) {
        return Result<void>::Failure(video_opened.error());
    }
    Logger::Instance().Log(LogLevel::kInfo, "application", "video_backend_opened",
                           "video capture backend opened with negotiated capabilities",
                           {{"backend", config_.video.backend},
                            {"device", config_.video.device},
                            {"width", std::to_string(video_opened.value().width)},
                            {"height", std::to_string(video_opened.value().height)},
                            {"fps", std::to_string(video_opened.value().fps)},
                            {"format", ToString(video_opened.value().format)}});
    if (config_.inference.backend == "rknn" &&
        video_opened.value().format == PixelFormat::kMjpeg) {
#if RKAV_WITH_JPEG
        video_decoder_ = std::make_unique<JpegVideoDecoder>();
        auto decoder_opened = video_decoder_->Open();
        if (!decoder_opened) {
            video_capture_->Close();
            return decoder_opened;
        }
#else
        video_capture_->Close();
        return Result<void>::Failure(
            AppError(ErrorCategory::kNotSupported, "open_video_decoder",
                     "MJPEG input with RKNN requires the JPEG decoder feature"));
#endif
    }
    if (config_.video_encoder.backend == "ffmpeg" &&
        video_opened.value().format == PixelFormat::kMjpeg) {
#if RKAV_WITH_JPEG
        video_encode_decoder_ = std::make_unique<JpegVideoDecoder>();
        auto decoder_opened = video_encode_decoder_->Open();
        if (!decoder_opened) {
            if (video_decoder_) {
                video_decoder_->Close();
            }
            video_capture_->Close();
            return decoder_opened;
        }
#else
        if (video_decoder_) {
            video_decoder_->Close();
        }
        video_capture_->Close();
        return Result<void>::Failure(
            AppError(ErrorCategory::kNotSupported, "open_video_encode_decoder",
                     "MJPEG input with FFmpeg H.264 requires the JPEG decoder feature"));
#endif
    }
    auto audio_opened = audio_capture_->Open(config_.audio);  // 音频后端打开/协商结果。
    if (!audio_opened) {
        if (video_decoder_) {
            video_decoder_->Close();
        }
        if (video_encode_decoder_) {
            video_encode_decoder_->Close();
        }
        video_capture_->Close();
        return Result<void>::Failure(audio_opened.error());
    }
    Logger::Instance().Log(
        LogLevel::kInfo, "application", "audio_backend_opened",
        "audio capture backend opened with negotiated capabilities",
        {{"backend", config_.audio.backend},
         {"device", config_.audio.device},
         {"sample_rate", std::to_string(audio_opened.value().sample_rate)},
         {"channels", std::to_string(audio_opened.value().channels)},
         {"samples_per_frame", std::to_string(audio_opened.value().samples_per_frame)},
         {"format", ToString(audio_opened.value().format)}});
    auto model_opened = inference_->Open(config_.inference);  // 推理模型初始化结果。
    if (!model_opened) {
        audio_capture_->Close();
        if (video_decoder_) {
            video_decoder_->Close();
        }
        if (video_encode_decoder_) {
            video_encode_decoder_->Close();
        }
        video_capture_->Close();
        return Result<void>::Failure(model_opened.error());
    }
    VideoCapabilities video_encoder_input = video_opened.value();
    if (video_encode_decoder_) {
        video_encoder_input.format = PixelFormat::kRgb888;
    }
    auto video_encoder_opened = video_encoder_->Open(
        config_.video_encoder, video_encoder_input);  // 解码后实际编码输入。
    if (!video_encoder_opened) {
        inference_->Close();
        audio_capture_->Close();
        if (video_decoder_) {
            video_decoder_->Close();
        }
        if (video_encode_decoder_) {
            video_encode_decoder_->Close();
        }
        video_capture_->Close();
        return Result<void>::Failure(video_encoder_opened.error());
    }
    auto audio_encoder_opened = audio_encoder_->Open(
        config_.audio_encoder, audio_opened.value());  // 音频编码器初始化结果。
    if (!audio_encoder_opened) {
        video_encoder_->Close();
        inference_->Close();
        audio_capture_->Close();
        if (video_decoder_) {
            video_decoder_->Close();
        }
        if (video_encode_decoder_) {
            video_encode_decoder_->Close();
        }
        video_capture_->Close();
        return Result<void>::Failure(audio_encoder_opened.error());
    }

    encoded_streams_.clear();
    encoded_streams_.push_back(std::move(video_encoder_opened).value());
    encoded_streams_.push_back(std::move(audio_encoder_opened).value());

    // 所有跨线程通道都使用有界队列，容量和溢出行为来自已校验配置。
    inference_queue_ = std::make_unique<BoundedQueue<VideoFrame>>(
        config_.inference.queue_capacity, config_.inference.overflow_policy);
    video_encode_queue_ = std::make_unique<BoundedQueue<VideoFrame>>(config_.video.queue_capacity,
                                                                     config_.video.overflow_policy);
    const std::size_t audio_capacity = static_cast<std::size_t>(
        std::max(1, config_.audio.queue_capacity_ms / config_.audio.frame_duration_ms));
    // 音频不能像视频那样静默丢帧，因此固定采用阻塞生产者策略。
    audio_encode_queue_ =
        std::make_unique<BoundedQueue<AudioFrame>>(audio_capacity, OverflowPolicy::kBlockProducer);
    return Result<void>::Success();
}

/// 功能：创建配置中所有启用 Sink，并启动包路由线程。
Result<void> Application::CreateRouter() {
    router_ = std::make_unique<PacketRouter>(clock_, metrics_);
    for (const auto& output : config_.outputs) {
        if (!output.enabled) {
            continue;
        }
        auto sink = CreatePacketSink(output);  // 当前输出配置对应的具体 Sink。
        if (!sink) {
            return Result<void>::Failure(sink.error());
        }
        auto added = router_->AddSink(output, std::move(sink).value(), encoded_streams_);  // 注册结果。
        if (!added) {
            return added;
        }
    }
    return router_->Start();
}

/// 功能：创建推理、编码、采集和监控 worker；创建异常转为结构化错误。
Result<void> Application::StartWorkers() {
    try {
        // 先启动消费者再启动采集者，避免首批数据进入尚无人消费的队列。
        inference_thread_ = std::jthread([this](std::stop_token stop) { InferenceLoop(stop); });
        video_encode_thread_ =
            std::jthread([this](std::stop_token stop) { VideoEncodeLoop(stop); });
        audio_encode_thread_ =
            std::jthread([this](std::stop_token stop) { AudioEncodeLoop(stop); });
        video_capture_thread_ =
            std::jthread([this](std::stop_token stop) { VideoCaptureLoop(stop); });
        audio_capture_thread_ =
            std::jthread([this](std::stop_token stop) { AudioCaptureLoop(stop); });
        monitor_thread_ = std::jthread([this](std::stop_token stop) { MonitorLoop(stop); });
    } catch (const std::exception& exception) {
        return Result<void>::Failure(
            AppError(ErrorCategory::kResourceExhausted, "start_workers", exception.what()));
    }
    return Result<void>::Success();
}

/// 功能：按依赖顺序停止数据源、处理线程、Router 和后端。
/// 参数 mode：Drain 用于正常退出，Abort 用于致命错误。
void Application::StopWorkers(CloseMode mode) noexcept {
    const ApplicationState current =
        state_.load(std::memory_order_acquire);  // 进入停止流程时的状态快照。
    if (current == ApplicationState::kStopped || current == ApplicationState::kCreated) {
        return;
    }
    state_.store(ApplicationState::kStopping, std::memory_order_release);
    const TimestampUs shutdown_started_us = clock_->NowUs();  // 停止计时起点，单位微秒。

    // 第一阶段：停止数据源，确保不再向处理队列写入新数据。
    monitor_thread_.request_stop();
    video_capture_thread_.request_stop();
    audio_capture_thread_.request_stop();
    if (video_capture_) {
        video_capture_->Close();
    }
    if (audio_capture_) {
        audio_capture_->Close();
    }
    if (video_capture_thread_.joinable()) {
        video_capture_thread_.join();
    }
    if (audio_capture_thread_.joinable()) {
        audio_capture_thread_.join();
    }

    // 第二阶段：关闭处理队列。Drain 保留存量，Abort 立即丢弃存量。
    if (inference_queue_) {
        inference_queue_->Close(mode);
    }
    if (video_encode_queue_) {
        video_encode_queue_->Close(mode);
    }
    if (audio_encode_queue_) {
        audio_encode_queue_->Close(mode);
    }
    if (mode == CloseMode::kAbort) {
        inference_thread_.request_stop();
        video_encode_thread_.request_stop();
        audio_encode_thread_.request_stop();
    }
    if (inference_thread_.joinable()) {
        inference_thread_.join();
    }
    if (video_encode_thread_.joinable()) {
        video_encode_thread_.join();
    }
    if (audio_encode_thread_.joinable()) {
        audio_encode_thread_.join();
    }

    // 第三阶段：处理线程结束后再停止路由，保证正常退出时编码包能够写完。
    if (router_) {
        router_->Stop(mode);
        if (auto output_error = router_->fatal_error(); output_error.has_value()) {
            std::scoped_lock lock(status_mutex_);
            if (!fatal_error_.has_value()) {
                fatal_error_ = *output_error;
            }
        }
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    if (inference_) {
        inference_->Close();
    }
    if (video_decoder_) {
        video_decoder_->Close();
    }
    if (video_encode_decoder_) {
        video_encode_decoder_->Close();
    }
    if (video_encoder_) {
        video_encoder_->Close();
    }
    if (audio_encoder_) {
        audio_encoder_->Close();
    }

    // 在线程全部退出后采集最终快照，此时队列指标不会再变化。
    CollectQueueMetrics();
    const auto snapshot = metrics_.Snapshot();  // 所有 worker 结束后的最终指标。
    std::string reason;                         // 在锁内复制出的第一停止原因。
    {
        std::scoped_lock lock(status_mutex_);
        reason = stop_reason_;
    }
    Logger::Instance().Log(LogLevel::kInfo, "application", "application_stopped",
                           "pipeline stopped", {{"reason", reason}, {"metrics", snapshot.dump()}});
    const TimestampUs shutdown_elapsed_us =
        clock_->NowUs() - shutdown_started_us;  // 实际关闭耗时，单位微秒。
    if (shutdown_elapsed_us >
        static_cast<TimestampUs>(config_.runtime.shutdown_timeout_ms) * 1000) {
        Logger::Instance().Log(
            LogLevel::kWarn, "application", "shutdown_timeout_exceeded",
            "graceful shutdown exceeded the configured operational deadline",
            {{"elapsed_us", std::to_string(shutdown_elapsed_us)},
             {"configured_timeout_ms", std::to_string(config_.runtime.shutdown_timeout_ms)}});
    }
    state_.store(fatal_error().has_value() ? ApplicationState::kFailed : ApplicationState::kStopped,
                 std::memory_order_release);
}

/// 功能：仅保存第一个致命错误、增加错误指标并触发全局停止。
void Application::ReportFatal(Error error) noexcept {
    {
        std::scoped_lock lock(status_mutex_);
        // 后续级联错误不覆盖第一个错误，第一个错误通常最接近真正根因。
        if (!fatal_error_.has_value()) {
            fatal_error_ = std::move(error);
        }
    }
    metrics_.Increment(MetricCounter::kErrors);
    RequestStop("fatal_error");
}

/// 功能：把应用内部队列和全部 Sink 队列状态更新到指标仓库。
void Application::CollectQueueMetrics() {
    if (inference_queue_) {
        metrics_.UpdateQueue("inference", inference_queue_->Snapshot());
    }
    if (video_encode_queue_) {
        metrics_.UpdateQueue("video_encode", video_encode_queue_->Snapshot());
    }
    if (audio_encode_queue_) {
        metrics_.UpdateQueue("audio_encode", audio_encode_queue_->Snapshot());
    }
    if (router_) {
        router_->CollectQueueMetrics();
    }
}

/// 功能：找出超过配置时间没有成功处理数据的 Running worker。
std::optional<Error> Application::DetectStalledWorker(TimestampUs now_us) const {
    struct WorkerEntry {
        const char* name;
        const WorkerHealth* health;
    };
    const WorkerEntry workers[]{{"video_capture", &video_capture_health_},  // 被检查的 worker 表。
                                {"audio_capture", &audio_capture_health_},
                                {"inference", &inference_health_},
                                {"video_encode", &video_encode_health_},
                                {"audio_encode", &audio_encode_health_}};
    const TimestampUs timeout_us =
        static_cast<TimestampUs>(config_.monitoring.worker_stall_timeout_ms) * 1000;
    // 只检查 Running worker；正常停止或主动降级的 worker 不应被误判为卡死。
    for (const auto& worker : workers) {
        if (worker.health->state() != WorkerState::kRunning) {
            continue;
        }
        const TimestampUs last_progress_us =
            worker.health->last_progress_us();  // 该 worker 最近成功处理数据的微秒时刻。
        if (last_progress_us >= 0 && now_us - last_progress_us > timeout_us) {
            return AppError(ErrorCategory::kTimeout, "health_check",
                            std::string(worker.name) + " made no progress within " +
                                std::to_string(config_.monitoring.worker_stall_timeout_ms) + " ms");
        }
    }
    return std::nullopt;
}

/// 功能：循环读取视频帧，同时投递到推理和视频编码两个分支。
void Application::VideoCaptureLoop(std::stop_token stop) {
    video_capture_health_.MarkStarted(clock_->NowUs());
    TimestampUs last_inference_pts_us = -1;  // 上一帧主动送入推理分支的来源 PTS。
    const TimestampUs inference_interval_us =
        config_.inference.max_fps == 0 ? 0 : 1'000'000 / config_.inference.max_fps;
    while (!stop.stop_requested()) {
        auto captured = video_capture_->Read(stop);  // 本次视频读取结果。
        if (!captured) {
            if (captured.error().category == ErrorCategory::kCancelled) {
                break;
            }
            video_capture_health_.MarkError(clock_->NowUs());
            LogWorkerError("video_capture", captured.error());
            if (!captured.error().retryable) {
                ReportFatal(captured.error());
                break;
            }
            if (!WaitBeforeRetry(clock_, stop)) {
                break;
            }
            continue;
        }
        VideoFrame frame = std::move(captured).value();  // 本次成功读取的视频帧。
        metrics_.Increment(MetricCounter::kVideoCaptured);
        video_capture_health_.MarkProgress(clock_->NowUs());
        // 先按实测 NPU 吞吐抽帧，再由 keep_latest 队列处理瞬时积压。
        QueueStatus inference_status = QueueStatus::kOk;
        if (inference_interval_us == 0 || last_inference_pts_us < 0 ||
            frame.pts_us - last_inference_pts_us >= inference_interval_us) {
            inference_status = inference_queue_->Push(frame, stop);
            if (inference_status == QueueStatus::kOk) {
                last_inference_pts_us = frame.pts_us;
            }
        }
        const auto encode_status =
            video_encode_queue_->Push(std::move(frame), stop);  // 编码分支投递状态。
        if (inference_status == QueueStatus::kCancelled ||
            encode_status == QueueStatus::kCancelled || inference_status == QueueStatus::kClosed ||
            encode_status == QueueStatus::kClosed) {
            break;
        }
    }
    video_capture_health_.SetState(WorkerState::kStopped);
}

/// 功能：循环读取 PCM，处理 XRUN，并以阻塞策略投递到音频编码队列。
void Application::AudioCaptureLoop(std::stop_token stop) {
    audio_capture_health_.MarkStarted(clock_->NowUs());
    while (!stop.stop_requested()) {
        auto captured = audio_capture_->Read(stop);  // 本次 PCM 块读取结果。
        if (!captured) {
            if (captured.error().category == ErrorCategory::kCancelled) {
                break;
            }
            audio_capture_health_.MarkError(clock_->NowUs());
            LogWorkerError("audio_capture", captured.error());
            if (captured.error().category == ErrorCategory::kXrun) {
                // XRUN 先在采集后端内部恢复；成功后重读同一逻辑音频块。
                auto recovered = audio_capture_->Recover();  // 采集后端恢复结果。
                if (recovered) {
                    metrics_.Increment(MetricCounter::kRecoveries);
                    continue;
                }
                LogWorkerError("audio_capture_recover", recovered.error());
            }
            if (!captured.error().retryable) {
                ReportFatal(captured.error());
                break;
            }
            if (!WaitBeforeRetry(clock_, stop)) {
                break;
            }
            continue;
        }
        metrics_.Increment(MetricCounter::kAudioCaptured);
        audio_capture_health_.MarkProgress(clock_->NowUs());
        // 音频队列满 250 ms 说明下游已无法保持连续，必须显式失败而不是丢块。
        const auto status = audio_encode_queue_->Push(std::move(captured).value(), stop,
                                                      std::chrono::milliseconds(250));
        if (status == QueueStatus::kCancelled || status == QueueStatus::kClosed) {
            break;
        }
        if (status == QueueStatus::kTimeout) {
            ReportFatal(AppError(ErrorCategory::kResourceExhausted, "audio_queue",
                                 "audio encoder queue remained full", false));
            break;
        }
    }
    audio_capture_health_.SetState(WorkerState::kStopped);
}

/// 功能：消费最新视频帧、执行推理并原子替换最新不可变结果快照。
void Application::InferenceLoop(std::stop_token stop) {
    inference_health_.MarkStarted(clock_->NowUs());
    while (!stop.stop_requested()) {
        auto popped = inference_queue_->Pop(stop, std::chrono::milliseconds(250));  // 取帧结果。
        if (popped.status == QueueStatus::kTimeout) {
            continue;
        }
        if (popped.status != QueueStatus::kOk || !popped.item.has_value()) {
            break;
        }
        VideoFrame inference_frame = std::move(*popped.item);  // 解码后交给推理的来源帧。
        if (inference_frame.format == PixelFormat::kMjpeg) {
            if (!video_decoder_) {
                ReportFatal(AppError(ErrorCategory::kNotSupported, "decode_video",
                                     "MJPEG inference frame has no configured decoder"));
                break;
            }
            const TimestampUs decode_started = clock_->NowUs();
            auto decoded = video_decoder_->Decode(inference_frame);
            metrics_.ObserveLatency("video_decode", clock_->NowUs() - decode_started);
            if (!decoded) {
                inference_health_.MarkError(clock_->NowUs());
                LogWorkerError("video_decode", decoded.error());
                if (!decoded.error().retryable) {
                    ReportFatal(decoded.error());
                    break;
                }
                continue;
            }
            inference_frame = std::move(decoded).value();
        }
        metrics_.Increment(MetricCounter::kInferenceRequests);
        const TimestampUs started = clock_->NowUs();  // 本次推理开始时间，单位微秒。
        auto result = inference_->Infer(inference_frame, stop);  // 推理后端返回结果。
        metrics_.ObserveLatency("inference", clock_->NowUs() - started);
        if (!result) {
            if (result.error().category == ErrorCategory::kCancelled) {
                break;
            }
            inference_health_.MarkError(clock_->NowUs());
            LogWorkerError("inference", result.error());
            if (!result.error().retryable) {
                ReportFatal(result.error());
                break;
            }
            continue;
        }
        // 编码线程只读取不可变快照，缩短 detection_mutex_ 的持锁时间。
        auto shared_result = std::make_shared<const DetectionBatch>(std::move(result).value());
        {
            std::scoped_lock lock(detection_mutex_);
            latest_detection_ = std::move(shared_result);
        }
        metrics_.Increment(MetricCounter::kInferenceResults);
        inference_health_.MarkProgress(clock_->NowUs());
    }
    inference_health_.SetState(WorkerState::kStopped);
}

/// 功能：消费视频帧、统计检测结果年龄、编码并投递视频包。
void Application::VideoEncodeLoop(std::stop_token stop) {
    video_encode_health_.MarkStarted(clock_->NowUs());
    while (!stop.stop_requested()) {
        auto popped =
            video_encode_queue_->Pop(stop, std::chrono::milliseconds(250));  // 取视频帧结果。
        if (popped.status == QueueStatus::kTimeout) {
            continue;
        }
        if (popped.status != QueueStatus::kOk || !popped.item.has_value()) {
            break;
        }
        {
            std::shared_ptr<const DetectionBatch> latest;  // 锁外使用的不可变检测快照。
            {
                std::scoped_lock lock(detection_mutex_);
                latest = latest_detection_;
            }
            // 结果只能用于其来源帧或更晚的帧，并记录超过允许年龄的结果。
            if (latest && latest->frame_sequence <= popped.item->sequence &&
                popped.item->pts_us - latest->source_pts_us >
                    static_cast<TimestampUs>(config_.inference.max_result_age_ms) * 1000) {
                metrics_.Increment(MetricCounter::kExpiredDetections);
            }
        }
        VideoFrame encode_frame = std::move(*popped.item);
        if (encode_frame.format == PixelFormat::kMjpeg) {
            if (!video_encode_decoder_) {
                ReportFatal(AppError(ErrorCategory::kNotSupported, "decode_video_for_encode",
                                     "MJPEG encoding frame has no configured decoder"));
                break;
            }
            const TimestampUs decode_started = clock_->NowUs();
            auto decoded = video_encode_decoder_->Decode(encode_frame);
            metrics_.ObserveLatency("video_encode_decode", clock_->NowUs() - decode_started);
            if (!decoded) {
                video_encode_health_.MarkError(clock_->NowUs());
                LogWorkerError("video_encode_decode", decoded.error());
                if (!decoded.error().retryable) {
                    ReportFatal(decoded.error());
                    break;
                }
                continue;
            }
            encode_frame = std::move(decoded).value();
        }
        const TimestampUs started = clock_->NowUs();       // 视频编码开始时间。
        auto encoded = video_encoder_->Encode(encode_frame);  // 零到多个编码包。
        metrics_.ObserveLatency("video_encode", clock_->NowUs() - started);
        if (!encoded) {
            video_encode_health_.MarkError(clock_->NowUs());
            LogWorkerError("video_encode", encoded.error());
            ReportFatal(encoded.error());
            break;
        }
        for (auto& packet : encoded.value()) {
            metrics_.Increment(MetricCounter::kVideoPackets);
            router_->Submit(std::make_shared<const EncodedPacket>(std::move(packet)));
        }
        video_encode_health_.MarkProgress(clock_->NowUs());
    }
    // Drain 关闭时队列会自然耗尽，此时 Flush 编码器内部可能延迟的最后几个包。
    if (!stop.stop_requested()) {
        auto flushed = video_encoder_->Flush();
        if (flushed) {
            for (auto& packet : flushed.value()) {
                metrics_.Increment(MetricCounter::kVideoPackets);
                router_->Submit(std::make_shared<const EncodedPacket>(std::move(packet)));
            }
        } else {
            LogWorkerError("video_encode_flush", flushed.error());
            ReportFatal(flushed.error());
        }
    }
    video_encode_health_.SetState(WorkerState::kStopped);
}

/// 功能：消费连续 PCM 块、编码并投递音频包。
void Application::AudioEncodeLoop(std::stop_token stop) {
    audio_encode_health_.MarkStarted(clock_->NowUs());
    while (!stop.stop_requested()) {
        auto popped =
            audio_encode_queue_->Pop(stop, std::chrono::milliseconds(250));  // 取 PCM 块结果。
        if (popped.status == QueueStatus::kTimeout) {
            continue;
        }
        if (popped.status != QueueStatus::kOk || !popped.item.has_value()) {
            break;
        }
        const TimestampUs started = clock_->NowUs();          // 音频编码开始时间。
        auto encoded = audio_encoder_->Encode(*popped.item);  // 零到多个编码包。
        metrics_.ObserveLatency("audio_encode", clock_->NowUs() - started);
        if (!encoded) {
            audio_encode_health_.MarkError(clock_->NowUs());
            LogWorkerError("audio_encode", encoded.error());
            ReportFatal(encoded.error());
            break;
        }
        for (auto& packet : encoded.value()) {
            metrics_.Increment(MetricCounter::kAudioPackets);
            router_->Submit(std::make_shared<const EncodedPacket>(std::move(packet)));
        }
        audio_encode_health_.MarkProgress(clock_->NowUs());
    }
    // 致命错误的 Abort 路径不再 Flush，避免传播可能不完整的尾包。
    if (!stop.stop_requested()) {
        auto flushed = audio_encoder_->Flush();
        if (flushed) {
            for (auto& packet : flushed.value()) {
                metrics_.Increment(MetricCounter::kAudioPackets);
                router_->Submit(std::make_shared<const EncodedPacket>(std::move(packet)));
            }
        } else {
            LogWorkerError("audio_encode_flush", flushed.error());
            ReportFatal(flushed.error());
        }
    }
    audio_encode_health_.SetState(WorkerState::kStopped);
}

/// 功能：协调指标输出、Router 错误转发和 worker 卡死检测三个监控任务。
void Application::MonitorLoop(std::stop_token stop) {
    const TimestampUs metrics_interval_us =
        static_cast<TimestampUs>(config_.monitoring.metrics_interval_ms) * 1000;
    const TimestampUs health_interval_us =
        static_cast<TimestampUs>(config_.monitoring.health_interval_ms) * 1000;
    const TimestampUs started_us = clock_->NowUs();              // 监控线程时间轴起点。
    TimestampUs next_report = started_us + metrics_interval_us;  // 下一次指标 deadline。
    TimestampUs next_health_check = started_us + health_interval_us;  // 下一次健康检查。
    while (!stop.stop_requested()) {
        // 指标和健康检查周期彼此独立，每次只睡到二者中更早的 deadline。
        const TimestampUs next_wakeup = std::min(next_report, next_health_check);
        if (!clock_->WaitUntil(next_wakeup, stop)) {
            break;
        }
        const TimestampUs now_us = clock_->NowUs();  // 本轮醒来后的统一当前时间。
        if (now_us >= next_report) {
            CollectQueueMetrics();
            Logger::Instance().Log(LogLevel::kInfo, "monitor", "metrics_snapshot",
                                   "periodic pipeline metrics",
                                   {{"metrics", metrics_.Snapshot().dump()}});
            // 若调度延迟跨过多个周期，直接推进到未来，避免连续补打过期日志。
            do {
                next_report += metrics_interval_us;
            } while (next_report <= now_us);
        }
        if (router_ && router_->has_fatal_error()) {
            auto error = router_->fatal_error();
            if (error.has_value()) {
                ReportFatal(*error);
                break;
            }
        }
        if (now_us >= next_health_check) {
            if (auto stalled = DetectStalledWorker(now_us); stalled.has_value()) {
                ReportFatal(*stalled);
                break;
            }
            do {
                next_health_check += health_interval_us;
            } while (next_health_check <= now_us);
        }
    }
}

/// 功能：把 ApplicationState 转换成稳定字符串。
const char* ToString(ApplicationState state) noexcept {
    switch (state) {
        case ApplicationState::kCreated:
            return "created";
        case ApplicationState::kInitializing:
            return "initializing";
        case ApplicationState::kRunning:
            return "running";
        case ApplicationState::kStopping:
            return "stopping";
        case ApplicationState::kStopped:
            return "stopped";
        case ApplicationState::kFailed:
            return "failed";
    }
    return "unknown";
}

}  // namespace rkav
