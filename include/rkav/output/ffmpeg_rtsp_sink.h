// 文件作用：声明基于 libavformat 的 RTSP 网络视频流输出端。
// 主要知识点：RTSP 协议、网络 I/O、FFmpeg muxer 生命周期和连接管理。
#pragma once

#include <memory>

#include "rkav/output/packet_sink.h"

namespace rkav {

/// 基于 FFmpeg RTSP muxer 的网络流输出端。
/// 将 H.264/AAC 编码包通过 RTSP 协议推送到网络，支持 TCP 和 UDP 传输。
/// 实际网络 I/O 由 FFmpeg libavformat 的 rtsp muxer 完成。
class FfmpegRtspSink final : public IPacketSink {
   public:
    FfmpegRtspSink();
    ~FfmpegRtspSink() override;

    Result<void> Open(const OutputConfig& config,
                      std::span<const EncodedStreamInfo> streams) override;
    Result<void> Write(const EncodedPacket& packet) override;
    Result<void> Flush() override;
    void Close() noexcept override;
    [[nodiscard]] std::string name() const override { return "rtsp"; }

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rkav
