// 文件作用：实现带可取消延迟、输出确定性检测框的 Mock 推理后端。
// 主要知识点：推理接口状态机、配置快照、stop_token、来源帧绑定和共享时钟。
#include "rkav/vision/mock_inference_engine.h"

#include <utility>

#include "rkav/capture/mock_video_capture.h"

namespace rkav {
namespace {

/// 功能：构造 mock_inference 模块统一错误。
Error InferenceError(ErrorCategory category, std::string message, bool retryable) {
    return Error{category, 0, "mock_inference", "infer", std::move(message), retryable};
}

}  // namespace

/// 功能：保存外部统一时钟；构造阶段不自动打开引擎。
MockInferenceEngine::MockInferenceEngine(std::shared_ptr<IClock> clock)
    : clock_(std::move(clock)) {}

/// 功能：保存 Mock 推理配置并返回模型输入尺寸。
Result<ModelInfo> MockInferenceEngine::Open(const InferenceConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<ModelInfo>::Failure(
            InferenceError(ErrorCategory::kInvalidState, "engine is already open", false));
    }
    config_ = config;
    open_ = true;
    return Result<ModelInfo>::Success(
        ModelInfo{config.input_width, config.input_height, "mock"});
}

/// 功能：校验输入帧、模拟推理耗时，并生成与来源帧严格绑定的检测结果。
Result<DetectionBatch> MockInferenceEngine::Infer(const VideoFrame& frame,
                                                   std::stop_token stop) {
    InferenceConfig config;  // 本次推理使用的配置快照，离开锁后保持不变。
    {
        // 只在读取状态时持锁，模拟耗时阶段不阻塞 Close 获取 mutex_。
        std::scoped_lock lock(mutex_);
        if (!open_) {
            return Result<DetectionBatch>::Failure(
                InferenceError(ErrorCategory::kInvalidState, "engine is not open", false));
        }
        config = config_;
    }

    auto validation = ValidateVideoFrame(frame);
    if (!validation) {
        return Result<DetectionBatch>::Failure(validation.error());
    }
    if (config.latency_ms > 0) {
        // 使用可取消时钟等待模拟推理耗时，停止时无需等满整个 latency。
        const TimestampUs deadline =
            clock_->NowUs() + static_cast<TimestampUs>(config.latency_ms) * 1000;
        if (!clock_->WaitUntil(deadline, stop)) {
            return Result<DetectionBatch>::Failure(
                InferenceError(ErrorCategory::kCancelled, "inference was cancelled", false));
        }
    }

    // 检测结果完全由帧序号决定，便于验证结果和来源帧是否正确绑定。
    DetectionBatch batch;  // 本次推理的完整结果容器。
    batch.frame_sequence = frame.sequence;
    batch.source_pts_us = frame.pts_us;
    batch.completed_at_us = clock_->NowUs();
    batch.items.push_back(Detection{0, 0.95F,
                                    SyntheticBoxForFrame(frame.sequence, frame.width, frame.height)});
    return Result<DetectionBatch>::Success(std::move(batch));
}

/// 功能：线程安全地关闭 Mock 引擎；允许重复调用。
void MockInferenceEngine::Close() noexcept {
    std::scoped_lock lock(mutex_);
    open_ = false;
}

}  // namespace rkav
