// 文件作用：声明无需 RKNN 模型即可输出确定性检测框的 Mock 推理引擎。
// 主要知识点：接口实现、共享时钟、可取消延迟、线程安全状态和确定性测试数据。
#pragma once

#include <memory>
#include <mutex>

#include "rkav/common/clock.h"
#include "rkav/vision/inference_engine.h"

namespace rkav {

class MockInferenceEngine final : public IInferenceEngine {
   public:
    /// 注入统一时钟，以便测试控制模拟推理耗时。
    explicit MockInferenceEngine(std::shared_ptr<IClock> clock);

    /// 保存推理配置并返回 Mock 模型信息。
    Result<ModelInfo> Open(const InferenceConfig& config) override;
    /// 等待配置的延迟后，生成与输入帧序号绑定的确定性检测框。
    Result<DetectionBatch> Infer(const VideoFrame& frame, std::stop_token stop) override;
    /// 关闭引擎，之后 Infer 会返回状态错误。
    void Close() noexcept override;

   private:
    std::shared_ptr<IClock> clock_;  // 模拟延迟和完成时间使用的统一时钟。
    std::mutex mutex_;               // 保护配置和打开状态。
    InferenceConfig config_;         // Open 后生效的配置快照。
    bool open_{false};               // 当前是否允许 Infer。
};

}  // namespace rkav
