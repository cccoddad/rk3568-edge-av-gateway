// 文件作用：定义项目统一错误分类、错误上下文和格式化接口。
// 主要知识点：强类型枚举、结构化错误、错误上下文传播，避免只返回模糊的 bool。
#pragma once

#include <string>
#include <string_view>

namespace rkav {

enum class ErrorCategory {
    kInvalidConfig,
    kInvalidState,
    kNotSupported,
    kDeviceNotFound,
    kDeviceDisconnected,
    kTimeout,
    kWouldBlock,
    kIo,
    kCodec,
    kInference,
    kNetwork,
    kResourceExhausted,
    kCancelled,
    kXrun,
    kInternal,
};

struct Error {
    ErrorCategory category{ErrorCategory::kInternal};  // 跨平台错误大类。
    int native_code{0};     // errno、ALSA 或 SDK 原始错误码；Mock 通常为 0。
    std::string module;     // 产生错误的模块，例如 mock_audio。
    std::string operation;  // 失败操作，例如 open、read、encode。
    std::string message;    // 面向开发者的具体原因。
    bool retryable{false};  // true 表示可退避重试，不表示必定恢复。
};

/// 将错误分类转换成稳定字符串，供日志和测试使用。
const char* ToString(ErrorCategory category) noexcept;
/// 把 Error 的所有字段拼成可直接打印的诊断文本。
std::string DescribeError(const Error& error);
/// 保留原始错误信息，同时补充当前调用层的模块与操作。
Error WithContext(Error error, std::string_view module, std::string_view operation);

}  // namespace rkav
