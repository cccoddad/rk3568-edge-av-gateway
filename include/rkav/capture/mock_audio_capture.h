// 文件作用：声明无需麦克风即可生成连续 PCM 数据的 Mock 音频采集器。
// 主要知识点：接口继承、正弦波生成状态、线程安全状态机、确定性 XRUN 故障注入。
#pragma once

#include <memory>
#include <mutex>
#include <optional>

#include "rkav/capture/audio_capture.h"
#include "rkav/common/clock.h"

namespace rkav {

class MockAudioCapture final : public IAudioCapture {
   public:
    /// 注入统一时钟；测试可传 ManualClock，运行时通常传 SteadyClock。
    explicit MockAudioCapture(std::shared_ptr<IClock> clock);

    /// 保存配置、重置序号和相位，并进入已打开状态。
    Result<AudioCapabilities> Open(const AudioConfig& config) override;
    /// 按配置生成一块静音或正弦波 PCM，也可能在指定块注入故障。
    Result<AudioFrame> Read(std::stop_token stop) override;
    /// 确认采集器仍已打开；Mock 中无需真实驱动恢复命令。
    Result<void> Recover() override;
    /// 退出已打开状态；允许重复调用。
    void Close() noexcept override;

   private:
    std::shared_ptr<IClock> clock_;  // 与视频、推理共享的单调时钟。
    std::mutex mutex_;         // 保护以下可变状态，避免 Read 与 Close 数据竞争。
    AudioConfig config_;       // Open 成功后生效的配置快照。
    TimestampUs start_us_{0};  // 第 0 个音频块的 PTS，单位微秒。
    std::uint64_t block_sequence_{0};  // 下一次成功块的序号。
    std::uint64_t sample_index_{0};    // 下一块首样本在整条波形中的绝对索引。
    std::optional<std::uint64_t> last_injected_xrun_sequence_;  // 防止同一块重复 XRUN。
    bool open_{false};  // 简单状态机：false=关闭，true=允许 Read/Recover。
};

}  // namespace rkav
