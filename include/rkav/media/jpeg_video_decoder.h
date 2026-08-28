// 文件作用：声明基于 libjpeg-turbo 的 MJPEG/JPEG 到 RGB888 解码后端。
// 主要知识点：Pimpl、TurboJPEG 生命周期、尺寸校验和 CPU RGB 布局。
#pragma once

#include <memory>
#include <mutex>

#include "rkav/media/video_decoder.h"

namespace rkav {

class JpegVideoDecoder final : public IVideoDecoder {
   public:
    JpegVideoDecoder();
    ~JpegVideoDecoder() override;

    Result<void> Open() override;
    Result<VideoFrame> Decode(const VideoFrame& frame) override;
    void Close() noexcept override;

   private:
    struct Impl;

    std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rkav
