// 文件作用：定义音频采集后端统一接口，Mock 和未来 ALSA 实现都遵守该契约。
// 主要知识点：PCM 参数、接口多态、可取消阻塞读取、XRUN 恢复和幂等关闭。
#pragma once

#include <stop_token>

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/config/config.h"

namespace rkav {

struct AudioCapabilities {
    int sample_rate{0};       // 设备最终采用的每秒采样点数。
    int channels{0};          // 声道数，1 为单声道、2 为双声道。
    int samples_per_frame{0}; // 每次 Read 返回的每声道样本数。
    SampleFormat format{SampleFormat::kS16LE};  // 单个样本的存储格式。
};

class IAudioCapture {
public:
    /// 功能：通过基类指针销毁具体音频后端，确保派生类资源被完整释放。
    virtual ~IAudioCapture() = default;
    /// 功能：使用 config 打开音频设备，并返回驱动最终协商出的 PCM 参数。
    /// 失败：重复 Open、设备不可用或参数不受支持时返回结构化 Error。
    virtual Result<AudioCapabilities> Open(const AudioConfig& config) = 0;
    /// 功能：阻塞到取得一个完整 PCM 块，或者 stop 收到取消请求。
    /// 返回：成功块的 PTS 必须单调递增；XRUN、取消或断连通过 Error 返回。
    virtual Result<AudioFrame> Read(std::stop_token stop) = 0;
    /// 功能：在 ALSA XRUN 等可恢复流错误后恢复采集状态。
    /// 返回：成功表示可以继续 Read；设备无法恢复时返回结构化 Error。
    virtual Result<void> Recover() = 0;
    /// 功能：释放音频设备资源；必须可重复调用且不得抛出异常。
    /// 注意：调用方应先请求传给 Read 的 stop，再调用 Close，避免关闭仍在使用的设备。
    virtual void Close() noexcept = 0;
};

}  // namespace rkav
