// 文件作用：实现真实/手动单调时钟、绝对帧节拍和微秒到其他时间基的换算。
// 主要知识点：steady_clock、可取消等待、条件变量、整数拆分计算和溢出检查。
#include "rkav/common/clock.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace rkav {
namespace {

/// 功能：读取操作系统 steady_clock 的原始微秒计数。
TimestampUs RawSteadyNowUs() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// 功能：构造 clock 模块的统一计算错误。
Error ClockError(std::string message) {
    return Error{ErrorCategory::kInvalidConfig, 0, "clock", "calculate", std::move(message), false};
}

}  // namespace

/// 功能：记录进程相对时间零点；对外时间不会受系统日期调整影响。
SteadyClock::SteadyClock() : epoch_us_(RawSteadyNowUs()) {}

/// 功能：返回相对本时钟构造时刻经过的微秒数。
TimestampUs SteadyClock::NowUs() const noexcept { return RawSteadyNowUs() - epoch_us_; }

/// 功能：等待到绝对微秒 deadline；到时返回 true，被 stop 取消返回 false。
bool SteadyClock::WaitUntil(TimestampUs deadline_us, std::stop_token stop) {
    if (stop.stop_requested()) {
        return false;
    }
    const TimestampUs remaining_us = deadline_us - NowUs();  // 距 deadline 剩余微秒数。
    if (remaining_us <= 0) {
        return true;
    }

    // condition_variable_any 的 stop_token 重载可在 deadline 前被 request_stop 唤醒。
    std::mutex mutex;
    std::condition_variable_any condition;
    std::unique_lock lock(mutex);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::microseconds(remaining_us);
    condition.wait_until(lock, stop, deadline, [] { return false; });
    return !stop.stop_requested();
}

/// 功能：线程安全地读取测试虚拟时间。
TimestampUs ManualClock::NowUs() const noexcept {
    std::scoped_lock lock(mutex_);
    return now_us_;
}

/// 功能：等待测试代码把虚拟时间推进到 deadline，或被 stop 取消。
bool ManualClock::WaitUntil(TimestampUs deadline_us, std::stop_token stop) {
    // 推进虚拟时间或请求停止都会唤醒等待者，测试不需要真实等待。
    std::stop_callback callback(stop, [this] { condition_.notify_all(); });
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this, deadline_us, &stop] {
        return now_us_ >= deadline_us || stop.stop_requested();
    });
    return !stop.stop_requested();
}

/// 功能：把虚拟时钟推进到更晚的绝对时间，并唤醒所有等待线程。
void ManualClock::AdvanceTo(TimestampUs now_us) {
    {
        std::scoped_lock lock(mutex_);
        if (now_us > now_us_) {
            now_us_ = now_us;
        }
    }
    condition_.notify_all();
}

/// 功能：按相对微秒数推进虚拟时钟。
void ManualClock::AdvanceBy(TimestampUs delta_us) {
    if (delta_us <= 0) {
        return;
    }
    {
        std::scoped_lock lock(mutex_);
        now_us_ += delta_us;
    }
    condition_.notify_all();
}

FramePacer::FramePacer(TimestampUs start_us, std::uint32_t frames_per_second)
    : start_us_(start_us), fps_(frames_per_second) {}

/// 功能：计算指定 sequence 帧相对于起点的绝对 deadline。
Result<TimestampUs> FramePacer::DeadlineFor(std::uint64_t sequence) const {
    if (fps_ == 0U) {
        return Result<TimestampUs>::Failure(ClockError("frame rate must be greater than zero"));
    }
    // 拆成整秒和余帧再计算，可避免 sequence * 1,000,000 过早溢出。
    const std::uint64_t whole_seconds = sequence / fps_;
    const std::uint64_t remainder = sequence % fps_;        // 当前整秒内的余帧数。
    constexpr std::uint64_t kMicrosPerSecond = 1'000'000U;  // 一秒的微秒数。
    const std::uint64_t max_timestamp =
        static_cast<std::uint64_t>(std::numeric_limits<TimestampUs>::max());
    if (whole_seconds > max_timestamp / kMicrosPerSecond) {
        return Result<TimestampUs>::Failure(ClockError("frame deadline overflow"));
    }
    const std::uint64_t offset =
        whole_seconds * kMicrosPerSecond + remainder * kMicrosPerSecond / fps_;
    if (start_us_ < 0 || offset > max_timestamp - static_cast<std::uint64_t>(start_us_)) {
        return Result<TimestampUs>::Failure(ClockError("frame deadline overflow"));
    }
    return Result<TimestampUs>::Success(start_us_ + static_cast<TimestampUs>(offset));
}

/// 功能：把微秒 PTS 换算为目标 time_base 的整数刻度，并进行范围检查。
Result<std::int64_t> RescaleTimestamp(TimestampUs timestamp_us, Rational destination) {
    if (timestamp_us < 0 || destination.numerator <= 0 || destination.denominator <= 0) {
        return Result<std::int64_t>::Failure(ClockError("invalid timestamp or time base"));
    }
    // 使用较高精度中间值，最后统一四舍五入到目标时间基整数刻度。
    const long double scaled = static_cast<long double>(timestamp_us) *
                               static_cast<long double>(destination.denominator) /
                               (1'000'000.0L * static_cast<long double>(destination.numerator));
    if (scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return Result<std::int64_t>::Failure(ClockError("timestamp rescale overflow"));
    }
    return Result<std::int64_t>::Success(static_cast<std::int64_t>(std::llround(scaled)));
}

}  // namespace rkav
