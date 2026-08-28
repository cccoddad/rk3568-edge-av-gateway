// 文件作用：声明基于 libavformat 的 H.264/AAC MP4 文件输出端。
#pragma once

#include <memory>

#include "rkav/output/packet_sink.h"

namespace rkav {

class FfmpegMp4Sink final : public IPacketSink {
   public:
    FfmpegMp4Sink();
    ~FfmpegMp4Sink() override;

    Result<void> Open(const OutputConfig& config,
                      std::span<const EncodedStreamInfo> streams) override;
    Result<void> Write(const EncodedPacket& packet) override;
    Result<void> Flush() override;
    void Close() noexcept override;
    [[nodiscard]] std::string name() const override { return "mp4"; }

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rkav
