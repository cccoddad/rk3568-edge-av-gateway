// 文件作用：实现线程安全 JSON 日志输出和日志级别解析。
// 主要知识点：UTC 时间格式化、跨平台时间函数、单例局部静态变量、互斥锁。
#include "rkav/common/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace rkav {
namespace {

/// 功能：生成 ISO 8601 UTC 时间字符串，仅用于人类阅读日志时间。
std::string UtcTimestamp() {
    const auto now = std::chrono::system_clock::now();            // 当前墙上时钟时间。
    const auto time = std::chrono::system_clock::to_time_t(now);  // 转为日历时间。
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm utc{};  // 保存拆分后的 UTC 年月日时分秒。
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << milliseconds.count() << 'Z';
    return stream.str();
}

}  // namespace

/// 功能：取得唯一 Logger；C++11 保证局部静态对象初始化是线程安全的。
Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

/// 功能：线程安全地更新最低日志级别。
void Logger::SetMinimumLevel(LogLevel level) noexcept {
    std::scoped_lock lock(mutex_);
    minimum_level_ = level;
}

/// 功能：线程安全地切换输出流，主要便于测试捕获日志。
void Logger::SetOutput(std::ostream& output) noexcept {
    std::scoped_lock lock(mutex_);
    output_ = &output;
}

/// 功能：构造一整条 JSON 记录并在同一临界区内写出，防止多线程串行交叉。
void Logger::Log(LogLevel level, std::string_view module, std::string_view event,
                 std::string_view message, std::initializer_list<LogField> fields) {
    std::scoped_lock lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(minimum_level_)) {
        return;
    }

    nlohmann::json record{{"timestamp", UtcTimestamp()},  // 本条结构化日志对象。
                          {"level", ToString(level)},
                          {"module", module},
                          {"event", event},
                          {"message", message}};
    for (const auto& [key, value] : fields) {
        record[key] = value;
    }
    std::ostream& output = output_ != nullptr ? *output_ : std::cerr;  // 实际输出目标。
    output << record.dump() << '\n';
    output.flush();
}

/// 功能：把日志级别枚举转换成配置和 JSON 中使用的小写文本。
const char* ToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::kTrace:
            return "trace";
        case LogLevel::kDebug:
            return "debug";
        case LogLevel::kInfo:
            return "info";
        case LogLevel::kWarn:
            return "warn";
        case LogLevel::kError:
            return "error";
        case LogLevel::kFatal:
            return "fatal";
    }
    return "unknown";
}

/// 功能：解析用户配置的日志级别；无法识别时不修改为任意默认值并返回 false。
bool ParseLogLevel(std::string_view text, LogLevel& level) noexcept {
    if (text == "trace") {
        level = LogLevel::kTrace;
    } else if (text == "debug") {
        level = LogLevel::kDebug;
    } else if (text == "info") {
        level = LogLevel::kInfo;
    } else if (text == "warn") {
        level = LogLevel::kWarn;
    } else if (text == "error") {
        level = LogLevel::kError;
    } else if (text == "fatal") {
        level = LogLevel::kFatal;
    } else {
        return false;
    }
    return true;
}

}  // namespace rkav
