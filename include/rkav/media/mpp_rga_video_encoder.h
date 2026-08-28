// 文件作用：声明 RK3568 MPP H.264 与 RGA NV12 转换的可选视频编码后端。
#pragma once

#include <memory>

#include "rkav/media/video_encoder.h"

namespace rkav {

// 厂商 SDK 类型只保留在 .cpp 的 Impl 中，公共接口继续使用项目自身数据契约。
class MppRgaVideoEncoder final : public IVideoEncoder {
   public:
    MppRgaVideoEncoder();
    ~MppRgaVideoEncoder() override;

    Result<EncodedStreamInfo> Open(const VideoEncoderConfig& config,
                                   const VideoCapabilities& input) override;
    Result<std::vector<EncodedPacket>> Encode(const VideoFrame& frame) override;
    Result<std::vector<EncodedPacket>> Flush() override;
    void Close() noexcept override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rkav
