// 文件作用：声明 RK3568 RKNN YOLOv5 推理后端，并隔离厂商 SDK 类型。
// 主要知识点：Pimpl、模型生命周期、输入预处理、可取消推理和来源坐标映射。
#pragma once

#include <memory>
#include <mutex>

#include "rkav/common/clock.h"
#include "rkav/vision/inference_engine.h"

namespace rkav {

class RknnInferenceEngine final : public IInferenceEngine {
   public:
    explicit RknnInferenceEngine(std::shared_ptr<IClock> clock);
    ~RknnInferenceEngine() override;

    Result<ModelInfo> Open(const InferenceConfig& config) override;
    Result<DetectionBatch> Infer(const VideoFrame& frame, std::stop_token stop) override;
    void Close() noexcept override;

   private:
    struct Impl;

    std::shared_ptr<IClock> clock_;
    std::mutex mutex_;
    InferenceConfig config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rkav
