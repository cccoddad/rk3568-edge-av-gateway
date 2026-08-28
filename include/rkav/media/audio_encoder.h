// 文件作用：定义 PCM 音频帧到编码包的统一编码接口。
// 主要知识点：接口多态、编码器状态机、一个输入产生零到多个输出包、Flush 语义。
#pragma once

#include <vector>

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/config/config.h"
#include "rkav/capture/audio_capture.h"

namespace rkav {

class IAudioEncoder {
   public:
    /// 功能：通过接口指针销毁具体编码器，确保派生类资源被正确释放。
    virtual ~IAudioEncoder() = default;
    /// 用配置初始化编码器；成功后才能调用 Encode。
    virtual Result<EncodedStreamInfo> Open(const AudioEncoderConfig& config,
                                           const AudioCapabilities& input) = 0;
    /// 编码一个完整 PCM 块；返回数组是因为真实编码器可能暂不出包或一次出多个包。
    virtual Result<std::vector<EncodedPacket>> Encode(const AudioFrame& frame) = 0;
    // Flush 只输出一次内部延迟包；Flush 之后再次 Encode 必须返回错误。
    virtual Result<std::vector<EncodedPacket>> Flush() = 0;
    /// 释放编码器资源；必须可重复调用。
    virtual void Close() noexcept = 0;
};

}  // namespace rkav
