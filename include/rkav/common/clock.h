// 文件作用：定义统一单调时钟、测试用手动时钟、视频节拍器和时间基换算。
// 主要知识点：steady_clock、条件变量、stop_token、绝对 deadline、整数溢出保护。
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>

#include "rkav/common/result.h"
#include "rkav/common/types.h"

namespace rkav {

class IClock {
public:
    virtual ~IClock() = default;
    /// 返回相对时钟起点的微秒数，必须单调不倒退。
    [[nodiscard]] virtual TimestampUs NowUs() const noexcept = 0;
    // 等待绝对时间点而非固定时长，避免循环 sleep 产生累计漂移，并且必须响应 stop。
    virtual bool WaitUntil(TimestampUs deadline_us, std::stop_token stop) = 0;
};

class SteadyClock final : public IClock {
public:
    /// 记录构造时刻作为相对时间零点。
    SteadyClock();
    [[nodiscard]] TimestampUs NowUs() const noexcept override;
    bool WaitUntil(TimestampUs deadline_us, std::stop_token stop) override;

private:
    std::int64_t epoch_us_{0};  // 构造时 steady_clock 的原始微秒值。
};

class ManualClock final : public IClock {
public:
    // 测试可手动推进时间，不依赖真实 sleep，因此结果可重复且执行更快。
    /// 功能：用指定微秒值初始化虚拟时间。
    explicit ManualClock(TimestampUs initial_us = 0) : now_us_(initial_us) {}

    [[nodiscard]] TimestampUs NowUs() const noexcept override;
    bool WaitUntil(TimestampUs deadline_us, std::stop_token stop) override;
    /// 把虚拟时间推进到指定绝对值；传入更早时间不会倒退。
    void AdvanceTo(TimestampUs now_us);
    /// 把虚拟时间向前推进指定微秒数；非正数被忽略。
    void AdvanceBy(TimestampUs delta_us);

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    TimestampUs now_us_{0};  // 当前虚拟时间，单位微秒。
};

class FramePacer {
public:
    FramePacer(TimestampUs start_us, std::uint32_t frames_per_second);

    // 每一帧都从统一起点计算 deadline，不在上一帧时间上累加截断误差。
    [[nodiscard]] Result<TimestampUs> DeadlineFor(std::uint64_t sequence) const;

private:
    TimestampUs start_us_;
    std::uint32_t fps_;
};

/// 把内部微秒时间戳换算成 destination 时间基中的整数刻度。
Result<std::int64_t> RescaleTimestamp(TimestampUs timestamp_us, Rational destination);

}  // namespace rkav
