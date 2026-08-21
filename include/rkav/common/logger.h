// 文件作用：声明线程安全的结构化 JSON 日志器及日志级别转换函数。
// 主要知识点：单例、互斥锁、输出流注入、结构化字段和日志级别过滤。
#pragma once

#include <initializer_list>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rkav {

enum class LogLevel { kTrace, kDebug, kInfo, kWarn, kError, kFatal };
using LogField = std::pair<std::string, std::string>;

class Logger {
public:
    /// 返回进程内唯一日志器实例。
    static Logger& Instance();

    /// 设置最低输出级别，低于该级别的日志会被忽略。
    void SetMinimumLevel(LogLevel level) noexcept;
    /// 替换日志输出流，主要供测试使用；调用者负责保证流的生命周期。
    void SetOutput(std::ostream& output) noexcept;
    /// 输出一条 JSON 日志；fields 用于携带可搜索的额外键值。
    void Log(LogLevel level, std::string_view module, std::string_view event,
             std::string_view message, std::initializer_list<LogField> fields = {});

private:
    Logger() = default;

    std::mutex mutex_;  // 保护配置和整行写入，防止多线程日志相互穿插。
    LogLevel minimum_level_{LogLevel::kInfo};  // 当前最低输出级别。
    std::ostream* output_{nullptr};  // nullptr 表示使用默认标准输出。
};

/// 将日志级别转换成稳定的小写字符串。
const char* ToString(LogLevel level) noexcept;
/// 解析配置中的日志级别字符串；成功时写入 level 并返回 true。
bool ParseLogLevel(std::string_view text, LogLevel& level) noexcept;

}  // namespace rkav
