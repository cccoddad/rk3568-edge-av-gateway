// 文件作用：声明用摘要包代替真实 H.264/AAC 的测试编码器。
// 主要知识点：接口实现、状态机、互斥锁、PTS 连续性校验和确定性摘要。
#pragma once

#include <mutex>
#include <optional>

#include "rkav/media/audio_encoder.h"
#include "rkav/media/video_encoder.h"

namespace rkav {

class ChecksumVideoEncoder final : public IVideoEncoder {
   public:
    /// 保存关键帧周期并进入可编码状态。
    Result<EncodedStreamInfo> Open(const VideoEncoderConfig& config,
                                   const VideoCapabilities& input) override;
    /// 校验视频帧并生成一个只含摘要信息的 Mock 视频包。
    Result<std::vector<EncodedPacket>> Encode(const VideoFrame& frame) override;
    /// 标记已经 Flush；Mock 无内部延迟，因此返回空数组。
    Result<std::vector<EncodedPacket>> Flush() override;
    /// 关闭编码器。
    void Close() noexcept override;

   private:
    std::mutex mutex_;  // 保护编码器状态，接口可从生命周期线程安全关闭。
    VideoEncoderConfig config_;  // 关键帧周期等配置。
    bool open_{false};           // 是否已 Open。
    bool flushed_{false};        // 是否已结束输入。
};

class ChecksumAudioEncoder final : public IAudioEncoder {
   public:
    /// 重置连续性检查状态并进入可编码状态。
    Result<EncodedStreamInfo> Open(const AudioEncoderConfig& config,
                                   const AudioCapabilities& input) override;
    /// 校验 PCM 块和 PTS 连续性，然后生成一个摘要包。
    Result<std::vector<EncodedPacket>> Encode(const AudioFrame& frame) override;
    /// 标记已经 Flush；Mock 无内部延迟，因此返回空数组。
    Result<std::vector<EncodedPacket>> Flush() override;
    /// 关闭编码器。
    void Close() noexcept override;

   private:
    std::mutex mutex_;                                 // 保护以下编码状态。
    bool open_{false};                                 // 是否已 Open。
    bool flushed_{false};                              // 是否已结束输入。
    std::optional<TimestampUs> expected_next_pts_us_;  // 下一块必须使用的微秒 PTS。
};

}  // namespace rkav
