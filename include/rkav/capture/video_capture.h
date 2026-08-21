// 文件作用：定义视频采集后端统一接口，隔离 Mock、V4L2 和具体摄像头实现。
// 主要知识点：视频协商参数、接口多态、可取消阻塞读取和资源生命周期。
#pragma once

#include <stop_token>

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/config/config.h"

namespace rkav {

struct VideoCapabilities {
    int width{0};                               // 实际输出宽度，单位像素。
    int height{0};                              // 实际输出高度，单位像素。
    int fps{0};                                 // 每秒帧数。
    PixelFormat format{PixelFormat::kUnknown};  // 实际像素格式。
};

class IVideoCapture {
   public:
    /// 功能：通过基类指针销毁具体采集后端，确保派生类资源被完整释放。
    virtual ~IVideoCapture() = default;
    /// 功能：使用 config 打开视频源，并返回驱动最终协商出的实际参数。
    /// 失败：重复 Open、设备不可用或参数不受支持时返回结构化 Error。
    virtual Result<VideoCapabilities> Open(const VideoConfig& config) = 0;
    /// 功能：阻塞到取得一帧，或者 stop 收到取消请求。
    /// 返回：成功时包含一帧及其 PTS；取消、断连等情况返回结构化 Error。
    virtual Result<VideoFrame> Read(std::stop_token stop) = 0;
    /// 功能：释放视频设备资源；必须可重复调用且不得抛出异常。
    /// 注意：调用方应先请求传给 Read 的 stop，再调用 Close，避免关闭仍在使用的设备。
    virtual void Close() noexcept = 0;
};

}  // namespace rkav
