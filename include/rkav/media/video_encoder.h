// 文件作用：定义原始视频帧到编码包的统一编码接口。
// 主要知识点：接口多态、编码器状态机、关键帧、延迟出包和 Flush 语义。
#pragma once

#include <vector>

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/config/config.h"

namespace rkav {

class IVideoEncoder {
   public:
    /// 功能：通过接口指针销毁具体编码器，确保派生类资源被正确释放。
    virtual ~IVideoEncoder() = default;
    /// 用配置初始化编码器；成功后才能调用 Encode。
    virtual Result<void> Open(const VideoEncoderConfig& config) = 0;
    /// 编码一个视频帧；真实硬件编码器可能返回零个或多个包。
    virtual Result<std::vector<EncodedPacket>> Encode(const VideoFrame& frame) = 0;
    // Flush 只输出一次内部延迟包；Flush 之后再次 Encode 必须返回错误。
    virtual Result<std::vector<EncodedPacket>> Flush() = 0;
    /// 释放编码器资源；必须可重复调用。
    virtual void Close() noexcept = 0;
};

}  // namespace rkav
