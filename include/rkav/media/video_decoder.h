// 文件作用：定义压缩视频帧到 CPU 像素帧的稳定解码接口。
// 主要知识点：接口隔离、帧元数据保持、可恢复帧错误和幂等生命周期。
#pragma once

#include "rkav/common/result.h"
#include "rkav/common/types.h"

namespace rkav {

class IVideoDecoder {
   public:
    virtual ~IVideoDecoder() = default;
    /// 初始化解码器资源；重复 Open 必须返回状态错误。
    virtual Result<void> Open() = 0;
    /// 把一帧压缩视频解码为 CPU 像素帧，并保持来源 sequence 和 PTS。
    virtual Result<VideoFrame> Decode(const VideoFrame& frame) = 0;
    /// 释放解码器资源；必须可重复调用且不得抛出异常。
    virtual void Close() noexcept = 0;
};

}  // namespace rkav
