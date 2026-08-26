// 文件作用：声明直接使用 Linux ALSA PCM 内核接口的音频采集后端。
// 主要知识点：USB 声卡设备映射、PCM 参数协商、非阻塞 poll、XRUN 恢复和静态链接。
#pragma once

#include <memory>
#include <mutex>

#include "rkav/capture/audio_capture.h"
#include "rkav/common/clock.h"

namespace rkav {

class AlsaAudioCapture final : public IAudioCapture {
   public:
    explicit AlsaAudioCapture(std::shared_ptr<IClock> clock);
    ~AlsaAudioCapture() override;

    Result<AudioCapabilities> Open(const AudioConfig& config) override;
    Result<AudioFrame> Read(std::stop_token stop) override;
    Result<void> Recover() override;
    void Close() noexcept override;

   private:
    void CloseUnlocked() noexcept;

    std::shared_ptr<IClock> clock_;
    std::mutex mutex_;
    AudioConfig config_;
    AudioCapabilities capabilities_;
    int file_descriptor_{-1};
    std::uint64_t sequence_{0};
    TimestampUs next_pts_us_{0};
    bool open_{false};
};

}  // namespace rkav
