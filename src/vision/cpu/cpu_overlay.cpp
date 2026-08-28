// 文件作用：以 CPU 在 RGB/BGR 图像上绘制裁剪检测框和紧凑文字标签。
#include "rkav/vision/overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace rkav {
namespace {

Error OverlayError(std::string operation, std::string message) {
    return Error{ErrorCategory::kNotSupported, 0, "cpu_overlay", std::move(operation),
                 std::move(message), false};
}

struct RgbColor {
    std::byte red;
    std::byte green;
    std::byte blue;
};

constexpr RgbColor kBoxColor{std::byte{255}, std::byte{220}, std::byte{0}};
constexpr RgbColor kTextColor{std::byte{255}, std::byte{255}, std::byte{255}};
constexpr RgbColor kLabelBackground{std::byte{20}, std::byte{20}, std::byte{20}};

void SetPixel(VideoFrame& frame, int x, int y, RgbColor color) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        return;
    }
    std::byte* pixel = frame.buffer->data() + static_cast<std::size_t>(y) *
                                                 static_cast<std::size_t>(frame.stride) +
                       static_cast<std::size_t>(x) * 3U;
    if (frame.format == PixelFormat::kRgb888) {
        pixel[0] = color.red;
        pixel[1] = color.green;
        pixel[2] = color.blue;
    } else {
        pixel[0] = color.blue;
        pixel[1] = color.green;
        pixel[2] = color.red;
    }
}

void FillRect(VideoFrame& frame, int left, int top, int right, int bottom, RgbColor color) {
    const int clipped_left = std::clamp(left, 0, frame.width);
    const int clipped_top = std::clamp(top, 0, frame.height);
    const int clipped_right = std::clamp(right, 0, frame.width);
    const int clipped_bottom = std::clamp(bottom, 0, frame.height);
    for (int y = clipped_top; y < clipped_bottom; ++y) {
        for (int x = clipped_left; x < clipped_right; ++x) {
            SetPixel(frame, x, y, color);
        }
    }
}

// 5x7 字形按行保存为低 5 位；当前标签只需 C、数字、空格和百分号。
std::array<std::uint8_t, 7> Glyph(char character) {
    switch (character) {
        case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case '5': return {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
        case '6': return {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
        case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        case '%': return {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13};
        case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        default: return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    }
}

void DrawText(VideoFrame& frame, int x, int y, const std::string& text) {
    constexpr int kGlyphWidth = 5;
    constexpr int kGlyphHeight = 7;
    constexpr int kGlyphSpacing = 1;
    for (std::size_t character_index = 0; character_index < text.size(); ++character_index) {
        const auto glyph = Glyph(text[character_index]);
        const int glyph_left = x + static_cast<int>(character_index) * (kGlyphWidth + kGlyphSpacing);
        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int column = 0; column < kGlyphWidth; ++column) {
                if ((glyph[static_cast<std::size_t>(row)] &
                     (1U << static_cast<unsigned>(kGlyphWidth - 1 - column))) != 0U) {
                    SetPixel(frame, glyph_left + column, y + row, kTextColor);
                }
            }
        }
    }
}

std::string LabelFor(const Detection& detection) {
    const int confidence_percent = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(detection.confidence) * 100.0)), 0, 100);
    return "C" + std::to_string(detection.class_id) + " " +
           std::to_string(confidence_percent) + "%";
}

}  // namespace

Result<VideoFrame> CpuOverlay::Apply(const VideoFrame& frame, const DetectionBatch& detections) {
    auto validation = ValidateVideoFrame(frame);
    if (!validation) {
        return Result<VideoFrame>::Failure(validation.error());
    }
    if (frame.format != PixelFormat::kRgb888 && frame.format != PixelFormat::kBgr888) {
        return Result<VideoFrame>::Failure(
            OverlayError("apply", "CPU OSD requires RGB888 or BGR888 input"));
    }

    VideoFrame output = frame;
    output.buffer = Buffer::Allocate(frame.buffer->size());
    std::memcpy(output.buffer->data(), frame.buffer->data(), frame.buffer->size());

    for (const auto& detection : detections.items) {
        const RectF& box = detection.box_in_source;
        if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) ||
            !std::isfinite(box.height) || box.width <= 0.0F || box.height <= 0.0F) {
            continue;
        }
        const int left = std::clamp(static_cast<int>(std::floor(box.x)), 0, output.width - 1);
        const int top = std::clamp(static_cast<int>(std::floor(box.y)), 0, output.height - 1);
        const int right = std::clamp(static_cast<int>(std::ceil(box.x + box.width)), 0,
                                     output.width - 1);
        const int bottom = std::clamp(static_cast<int>(std::ceil(box.y + box.height)), 0,
                                      output.height - 1);
        if (right <= left || bottom <= top) {
            continue;
        }

        for (int thickness = 0; thickness < config_.line_width; ++thickness) {
            const int inset_left = left - thickness;
            const int inset_top = top - thickness;
            const int inset_right = right + thickness;
            const int inset_bottom = bottom + thickness;
            for (int x = inset_left; x <= inset_right; ++x) {
                SetPixel(output, x, inset_top, kBoxColor);
                SetPixel(output, x, inset_bottom, kBoxColor);
            }
            for (int y = inset_top; y <= inset_bottom; ++y) {
                SetPixel(output, inset_left, y, kBoxColor);
                SetPixel(output, inset_right, y, kBoxColor);
            }
        }

        if (config_.draw_labels) {
            constexpr int kGlyphHeight = 7;
            constexpr int kGlyphAdvance = 6;
            constexpr int kPadding = 1;
            const std::string label = LabelFor(detection);
            const int label_width = static_cast<int>(label.size()) * kGlyphAdvance + 2 * kPadding;
            const int label_height = kGlyphHeight + 2 * kPadding;
            const int label_left = std::min(left, std::max(0, output.width - label_width));
            const int label_top = top >= label_height ? top - label_height : std::min(
                bottom + 1, std::max(0, output.height - label_height));
            FillRect(output, label_left, label_top, label_left + label_width,
                     label_top + label_height, kLabelBackground);
            DrawText(output, label_left + kPadding, label_top + kPadding, label);
        }
    }
    return Result<VideoFrame>::Success(std::move(output));
}

}  // namespace rkav
