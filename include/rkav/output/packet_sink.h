// 文件作用：定义编码包最终消费端的统一接口，例如 Null、JSONL、未来 RTSP/MP4。
// 主要知识点：接口多态、输出生命周期、写入错误传播和 Flush/Close 区别。
#pragma once

#include "rkav/common/result.h"
#include "rkav/common/types.h"
#include "rkav/config/config.h"

namespace rkav {

class IPacketSink {
   public:
    /// 功能：通过接口指针销毁具体输出端，确保派生类资源被正确释放。
    virtual ~IPacketSink() = default;
    /// 打开输出资源并应用队列外的 Sink 配置。
    virtual Result<void> Open(const OutputConfig& config) = 0;
    /// 同步消费一个不可变编码包；调用发生在该 Sink 自己的 worker 线程。
    virtual Result<void> Write(const EncodedPacket& packet) = 0;
    /// 把用户态/文件缓冲刷新到底层；成功后不再接受 Write。
    virtual Result<void> Flush() = 0;
    /// 释放输出资源；必须可重复调用且不抛异常。
    virtual void Close() noexcept = 0;
    /// 返回用于日志和指标键的稳定短名称。
    [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace rkav
