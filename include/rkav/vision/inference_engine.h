// 文件作用：定义视频帧到检测结果的统一推理接口，隔离 Mock 和未来 RKNN 实现。
// 主要知识点：接口多态、模型元数据、可取消耗时任务和结果来源帧绑定。
#pragma once

#include <stop_token>

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/config/config.h"

namespace rkav {

struct ModelInfo {
    int input_width{0};   // 模型期望输入宽度。
    int input_height{0};  // 模型期望输入高度。
    std::string backend;  // 实际后端名称，例如 mock 或 rknn。
};

class IInferenceEngine {
   public:
    /// 功能：通过接口指针销毁具体推理后端，确保派生类资源被正确释放。
    virtual ~IInferenceEngine() = default;
    /// 加载模型或初始化后端，并返回实际模型输入信息。
    virtual Result<ModelInfo> Open(const InferenceConfig& config) = 0;
    // 结果必须保留输入帧序号和 PTS，以便下游识别过期或错配结果。
    virtual Result<DetectionBatch> Infer(const VideoFrame& frame, std::stop_token stop) = 0;
    /// 释放模型和后端资源；必须可重复调用。
    virtual void Close() noexcept = 0;
};

}  // namespace rkav
