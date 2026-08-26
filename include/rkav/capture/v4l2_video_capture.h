// 文件作用：声明 Linux V4L2 MMAP 视频采集后端。
// 主要知识点：设备能力协商、MMAP 缓冲、poll 超时、帧复制和幂等资源回收。
#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "rkav/capture/video_capture.h"
#include "rkav/common/clock.h"

namespace rkav {

class V4L2VideoCapture final : public IVideoCapture {
   public:
    explicit V4L2VideoCapture(std::shared_ptr<IClock> clock);
    ~V4L2VideoCapture() override;

    Result<VideoCapabilities> Open(const VideoConfig& config) override;
    Result<VideoFrame> Read(std::stop_token stop) override;
    void Close() noexcept override;

   private:
    struct MappedBuffer {
        void* address{nullptr};
        std::size_t length{0};
    };

    void CloseUnlocked() noexcept;

    std::shared_ptr<IClock> clock_;
    std::mutex mutex_;
    VideoConfig config_;
    VideoCapabilities capabilities_;
    std::vector<MappedBuffer> buffers_;
    int file_descriptor_{-1};
    int stride_{0};
    std::uint64_t sequence_{0};
    bool streaming_{false};
    bool open_{false};
};

}  // namespace rkav
