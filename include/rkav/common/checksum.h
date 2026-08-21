// 文件作用：提供 FNV-1a 64 位摘要，用于验证 Mock 音视频内容是否发生变化。
// 主要知识点：哈希迭代、std::span、无符号整数自然溢出；它不是密码学哈希。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rkav {

/// 功能：计算一段字节的确定性摘要；相同输入必定得到相同结果。
/// 参数 bytes：只读字节视图，函数不持有也不修改原缓冲区。
inline std::uint64_t Fnv1a64(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV-1a 标准 64 位偏移基数。
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(value));
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace rkav
