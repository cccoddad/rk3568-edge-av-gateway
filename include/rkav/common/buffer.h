// 文件作用：定义音视频数据共用的连续字节缓冲区及其访问方式。
// 主要知识点：RAII、std::vector 连续内存、std::span 非拥有视图、shared_ptr 共享所有权。
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace rkav {

class Buffer {
   public:
    /// 功能：按指定字节数创建并用 0 初始化缓冲区。
    /// 参数 size：需要分配的字节数，不是像素数或样本数。
    explicit Buffer(std::size_t size) : bytes_(size) {}
    /// 功能：接管已有字节数组，避免再次复制其中的数据。
    explicit Buffer(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}

    /// 功能：分配 Buffer 并返回 shared_ptr；同一帧可扇出而不复制底层字节。
    static std::shared_ptr<Buffer> Allocate(std::size_t size) {
        return std::make_shared<Buffer>(size);
    }

    /// 返回可写首地址；缓冲区为空时可能为 nullptr。
    [[nodiscard]] std::byte* data() noexcept { return bytes_.data(); }
    /// 返回只读首地址，供校验、编码等只读步骤使用。
    [[nodiscard]] const std::byte* data() const noexcept { return bytes_.data(); }
    /// 返回当前有效字节数。
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    /// 返回底层已分配容量；它可能大于 size()。
    [[nodiscard]] std::size_t capacity() const noexcept { return bytes_.capacity(); }
    /// 返回可写视图；span 不拥有内存，生命周期不能超过本 Buffer。
    [[nodiscard]] std::span<std::byte> writable_span() noexcept { return bytes_; }
    /// 返回只读视图；用于在不暴露 vector 的情况下遍历字节。
    [[nodiscard]] std::span<const std::byte> span() const noexcept { return bytes_; }

   private:
    std::vector<std::byte> bytes_;  // 真正拥有内存的连续字节数组。
};

}  // namespace rkav
