// 文件作用：实现错误枚举转文本、完整错误描述和上层上下文补充。
// 主要知识点：枚举分派、ostringstream 文本组装、按值传参配合移动语义。
#include "rkav/common/error.h"

#include <sstream>

namespace rkav {

/// 功能：把 ErrorCategory 转为日志中稳定可检索的字符串。
const char* ToString(ErrorCategory category) noexcept {
    switch (category) {
        case ErrorCategory::kInvalidConfig:
            return "invalid_config";
        case ErrorCategory::kInvalidState:
            return "invalid_state";
        case ErrorCategory::kNotSupported:
            return "not_supported";
        case ErrorCategory::kDeviceNotFound:
            return "device_not_found";
        case ErrorCategory::kDeviceDisconnected:
            return "device_disconnected";
        case ErrorCategory::kTimeout:
            return "timeout";
        case ErrorCategory::kWouldBlock:
            return "would_block";
        case ErrorCategory::kIo:
            return "io";
        case ErrorCategory::kCodec:
            return "codec";
        case ErrorCategory::kInference:
            return "inference";
        case ErrorCategory::kNetwork:
            return "network";
        case ErrorCategory::kResourceExhausted:
            return "resource_exhausted";
        case ErrorCategory::kCancelled:
            return "cancelled";
        case ErrorCategory::kXrun:
            return "xrun";
        case ErrorCategory::kInternal:
            return "internal";
    }
    return "unknown";
}

/// 功能：把结构化错误展开为一行诊断文本；不会修改原 Error。
std::string DescribeError(const Error& error) {
    std::ostringstream stream;  // 临时文本构造器，用于避免多次字符串拼接。
    stream << error.module << '.' << error.operation << ": " << error.message
           << " [category=" << ToString(error.category) << ", native_code="
           << error.native_code << ", retryable=" << (error.retryable ? "true" : "false")
           << ']';
    return stream.str();
}

/// 功能：为下层错误替换当前调用层的模块与操作，保留类别、原始码和消息。
Error WithContext(Error error, std::string_view module, std::string_view operation) {
    error.module = std::string(module);
    error.operation = std::string(operation);
    return error;
}

}  // namespace rkav
