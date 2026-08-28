// 文件作用：声明 FFmpeg 软件 H.264/AAC 编码后端并隔离第三方 ABI。
#pragma once

#include <memory>

#include "rkav/media/audio_encoder.h"
#include "rkav/media/video_encoder.h"

namespace rkav {

class FfmpegVideoEncoder final : public IVideoEncoder {
   public:
    FfmpegVideoEncoder();
    ~FfmpegVideoEncoder() override;

    Result<EncodedStreamInfo> Open(const VideoEncoderConfig& config,
                                   const VideoCapabilities& input) override;
    Result<std::vector<EncodedPacket>> Encode(const VideoFrame& frame) override;
    Result<std::vector<EncodedPacket>> Flush() override;
    void Close() noexcept override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class FfmpegAudioEncoder final : public IAudioEncoder {
   public:
    FfmpegAudioEncoder();
    ~FfmpegAudioEncoder() override;

    Result<EncodedStreamInfo> Open(const AudioEncoderConfig& config,
                                   const AudioCapabilities& input) override;
    Result<std::vector<EncodedPacket>> Encode(const AudioFrame& frame) override;
    Result<std::vector<EncodedPacket>> Flush() override;
    void Close() noexcept override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rkav
