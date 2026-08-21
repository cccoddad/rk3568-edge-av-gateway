// 文件作用：实现“成功值或错误”二选一的返回类型，统一普通值和 void 函数的错误处理。
// 主要知识点：模板特化、std::variant、std::optional、移动语义、显式状态检查。
#pragma once

#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "rkav/common/error.h"

namespace rkav {

template <typename T>
class Result {
   public:
    /// 功能：构造持有成功值的 Result。
    static Result Success(T value) { return Result(std::move(value)); }
    /// 功能：构造持有结构化错误的 Result。
    static Result Failure(Error error) { return Result(std::move(error)); }

    /// 判断结果是否成功；nodiscard 提醒调用者不要忽略错误。
    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(value_); }
    /// 功能：允许写 `if (result)` 判断成功，但禁止隐式转换成整数等类型。
    explicit operator bool() const noexcept { return ok(); }

    /// 读取左值中的成功值；失败状态调用说明调用方逻辑有误。
    T& value() & {
        if (!ok()) {
            throw std::logic_error("attempted to read value from a failed Result");
        }
        return std::get<T>(value_);
    }

    /// 功能：从 const 左值 Result 中只读访问成功值。
    const T& value() const& {
        if (!ok()) {
            throw std::logic_error("attempted to read value from a failed Result");
        }
        return std::get<T>(value_);
    }

    /// 从临时 Result 中移动成功值，传递大 Frame 时可避免复制。
    T&& value() && {
        if (!ok()) {
            throw std::logic_error("attempted to move value from a failed Result");
        }
        return std::get<T>(std::move(value_));
    }

    /// 读取失败信息；成功状态调用同样属于程序逻辑错误。
    Error& error() & {
        if (ok()) {
            throw std::logic_error("attempted to read error from a successful Result");
        }
        return std::get<Error>(value_);
    }

    /// 功能：从 const Result 中只读访问错误。
    const Error& error() const& {
        if (ok()) {
            throw std::logic_error("attempted to read error from a successful Result");
        }
        return std::get<Error>(value_);
    }

   private:
    /// 功能：仅供 Success 工厂使用的成功值构造函数。
    explicit Result(T value) : value_(std::move(value)) {}
    /// 功能：仅供 Failure 工厂使用的错误构造函数。
    explicit Result(Error error) : value_(std::move(error)) {}

    std::variant<T, Error> value_;  // 任意时刻只持有 T 或 Error 中的一种。
};

template <>
class Result<void> {
   public:
    /// 功能：构造不含错误的 void 成功结果。
    static Result Success() { return Result(); }
    /// 功能：构造包含 Error 的 void 失败结果。
    static Result Failure(Error error) { return Result(std::move(error)); }

    /// 功能：通过 optional 是否为空判断成功。
    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    /// 功能：允许使用 `if (result)` 进行显式成功判断。
    explicit operator bool() const noexcept { return ok(); }

    /// 功能：访问可修改错误；成功状态调用属于逻辑错误。
    Error& error() & {
        if (ok()) {
            throw std::logic_error("attempted to read error from a successful Result");
        }
        return *error_;
    }

    /// 功能：只读访问错误；成功状态调用属于逻辑错误。
    const Error& error() const& {
        if (ok()) {
            throw std::logic_error("attempted to read error from a successful Result");
        }
        return *error_;
    }

   private:
    /// 功能：仅供 Success 使用的默认成功构造函数。
    Result() = default;
    /// 功能：仅供 Failure 使用的错误构造函数。
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<Error> error_;
};

}  // namespace rkav
