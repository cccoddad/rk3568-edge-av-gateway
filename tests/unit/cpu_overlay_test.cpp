// 文件作用：验证 CPU OSD 的像素复制、边框裁剪、文字标签和格式边界。
#include "rkav/vision/overlay.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>

namespace rkav {
namespace {

VideoFrame SolidFrame(PixelFormat format) {
    VideoFrame frame;
    frame.sequence = 7;
    frame.pts_us = 7000;
    frame.width = 24;
    frame.height = 18;
    frame.stride = frame.width * 3;
    frame.format = format;
    frame.buffer = Buffer::Allocate(static_cast<std::size_t>(frame.stride * frame.height));
    std::fill(frame.buffer->writable_span().begin(), frame.buffer->writable_span().end(),
              std::byte{0});
    return frame;
}

DetectionBatch SingleDetection() {
    DetectionBatch batch;
    batch.frame_sequence = 7;
    batch.source_pts_us = 7000;
    batch.completed_at_us = 8000;
    batch.items.push_back(Detection{3, 0.95F, RectF{3.0F, 10.0F, 8.0F, 5.0F}});
    return batch;
}

unsigned ByteAt(const VideoFrame& frame, int x, int y, int channel) {
    return std::to_integer<unsigned>(
        frame.buffer->data()[static_cast<std::size_t>(y * frame.stride + x * 3 + channel)]);
}

TEST(CpuOverlayTest, DrawsBoxAndLabelWithoutMutatingSharedSource) {
    OverlayConfig config;
    config.line_width = 1;
    config.draw_labels = true;
    CpuOverlay overlay(config);
    const VideoFrame source = SolidFrame(PixelFormat::kRgb888);

    auto result = overlay.Apply(source, SingleDetection());

    ASSERT_TRUE(result);
    const VideoFrame& output = result.value();
    EXPECT_NE(output.buffer.get(), source.buffer.get());
    EXPECT_EQ(ByteAt(source, 3, 10, 0), 0U);
    EXPECT_EQ(ByteAt(output, 3, 10, 0), 255U);
    EXPECT_EQ(ByteAt(output, 3, 10, 1), 220U);
    EXPECT_EQ(ByteAt(output, 3, 10, 2), 0U);

    bool label_changed = false;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < output.width; ++x) {
            label_changed = label_changed || ByteAt(output, x, y, 0) != 0U ||
                            ByteAt(output, x, y, 1) != 0U || ByteAt(output, x, y, 2) != 0U;
        }
    }
    EXPECT_TRUE(label_changed);
}

TEST(CpuOverlayTest, WritesLogicalColorInBgrAndClipsOffscreenBox) {
    OverlayConfig config;
    config.draw_labels = false;
    CpuOverlay overlay(config);
    VideoFrame source = SolidFrame(PixelFormat::kBgr888);
    DetectionBatch batch = SingleDetection();
    batch.items.front().box_in_source = RectF{-5.0F, -5.0F, 10.0F, 10.0F};

    auto result = overlay.Apply(source, batch);

    ASSERT_TRUE(result);
    EXPECT_EQ(ByteAt(result.value(), 0, 0, 0), 0U);
    EXPECT_EQ(ByteAt(result.value(), 0, 0, 1), 220U);
    EXPECT_EQ(ByteAt(result.value(), 0, 0, 2), 255U);
}

TEST(CpuOverlayTest, RejectsCompressedInput) {
    CpuOverlay overlay(OverlayConfig{});
    VideoFrame source = SolidFrame(PixelFormat::kMjpeg);
    source.stride = 0;
    source.buffer = Buffer::Allocate(1U);

    auto result = overlay.Apply(source, SingleDetection());

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::kNotSupported);
}

}  // namespace
}  // namespace rkav
