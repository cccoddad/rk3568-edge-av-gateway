// 文件作用：定义与推理后端无关的检测框和文字 OSD 像素叠加接口。
#pragma once

#include <utility>

#include "rkav/common/types.h"
#include "rkav/config/config.h"

namespace rkav {

class IOverlay {
   public:
    virtual ~IOverlay() = default;

    // 返回独立缓冲区的图像，禁止改写可能仍被推理分支共享的源帧。
    virtual Result<VideoFrame> Apply(const VideoFrame& frame,
                                     const DetectionBatch& detections) = 0;
};

// CPU 基线只处理 RGB/BGR；后续 RGA 实现保持相同数据契约。
class CpuOverlay final : public IOverlay {
   public:
    explicit CpuOverlay(OverlayConfig config) : config_(std::move(config)) {}

    Result<VideoFrame> Apply(const VideoFrame& frame,
                             const DetectionBatch& detections) override;

   private:
    OverlayConfig config_;
};

}  // namespace rkav
