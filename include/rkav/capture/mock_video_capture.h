// 文件作用：声明无需摄像头即可生成确定性 RGB 测试图的视频采集器。
// 主要知识点：接口继承、帧节拍、共享时钟、像素缓冲区和确定性故障注入。
#pragma once

#include <memory>
#include <mutex>

#include "rkav/capture/video_capture.h"
#include "rkav/common/clock.h"

namespace rkav {

/// 根据帧序号计算移动目标框；相同输入总是得到相同坐标。
RectF SyntheticBoxForFrame(std::uint64_t sequence, int width, int height);
/// 在调用者提供的 RGB888 Frame 中写入色条、移动框和二进制帧号。
Result<void> GenerateRgbTestPattern(std::uint64_t sequence, VideoFrame& frame);

class MockVideoCapture final : public IVideoCapture {
public:
    /// 注入统一时钟；测试时可替换为 ManualClock。
    explicit MockVideoCapture(std::shared_ptr<IClock> clock);

    /// 保存配置并把当前时刻设为视频时间轴起点。
    Result<VideoCapabilities> Open(const VideoConfig& config) override;
    /// 按绝对帧 deadline 生成一帧 RGB888 图像，或按配置注入读取失败。
    Result<VideoFrame> Read(std::stop_token stop) override;
    /// 标记采集器关闭；之后 Read 会返回状态错误。
    void Close() noexcept override;

private:
    std::shared_ptr<IClock> clock_;  // 与音频和推理共用的时间源。
    std::mutex mutex_;              // 保护 Open/Read/Close 共享状态。
    VideoConfig config_;            // 已生效的视频配置快照。
    TimestampUs start_us_{0};        // 第 0 帧绝对 PTS，单位微秒。
    std::uint64_t attempt_{0};       // Read 尝试次数，也用于确定故障注入位置。
    bool open_{false};               // 当前是否允许读取。
};

}  // namespace rkav
