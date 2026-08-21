// 文件作用：实现 letterbox 参数计算以及模型检测框到原图坐标的反向映射。
// 主要知识点：保持宽高比缩放、居中补边、坐标逆变换、浮点舍入和边界裁剪。
#include "rkav/vision/geometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rkav {

/// 功能：计算原图完整放入模型输入尺寸时的实际缩放大小和四周补边。
/// 返回：任一尺寸非正时返回配置错误，否则返回可供预处理/后处理共享的元数据。
Result<TransformMetadata> ComputeLetterboxTransform(int source_width, int source_height,
                                                     int destination_width,
                                                     int destination_height) {
    if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
        destination_height <= 0) {
        return Result<TransformMetadata>::Failure(
            Error{ErrorCategory::kInvalidConfig, 0, "geometry", "letterbox",
                  "source and destination dimensions must be positive", false});
    }

    // 取宽高缩放比中较小者，保证完整图像落入目标尺寸且不发生拉伸变形。
    const double requested_scale =
        std::min(static_cast<double>(destination_width) / source_width,
                 static_cast<double>(destination_height) / source_height); // 理论等比缩放率。
    const int resized_width =
        std::clamp(static_cast<int>(std::lround(source_width * requested_scale)), 1,
                   destination_width);
    const int resized_height =
        std::clamp(static_cast<int>(std::lround(source_height * requested_scale)), 1,
                   destination_height);
    const int remaining_width = destination_width - resized_width;   // 水平剩余补边总宽。
    const int remaining_height = destination_height - resized_height; // 垂直剩余补边总高。

    return Result<TransformMetadata>::Success(
        TransformMetadata{source_width,
                          source_height,
                          destination_width,
                          destination_height,
                          resized_width,
                          resized_height,
                          remaining_width / 2,
                          remaining_height / 2,
                          static_cast<float>(resized_width) /
                              static_cast<float>(source_width),
                          static_cast<float>(resized_height) /
                              static_cast<float>(source_height)});
}

/// 功能：把模型输入坐标中的矩形框还原到原图坐标，并裁剪掉补边和越界部分。
RectF MapBoxToSource(const RectF& model_box, const TransformMetadata& transform) {
    // 先移除 letterbox 填充，再除以实际缩放比例，得到原图坐标。
    const float left =
        (model_box.x - static_cast<float>(transform.pad_left)) / transform.scale_x;
    const float top =
        (model_box.y - static_cast<float>(transform.pad_top)) / transform.scale_y;
    const float right = (model_box.x + model_box.width -
                         static_cast<float>(transform.pad_left)) /
                        transform.scale_x;
    const float bottom = (model_box.y + model_box.height -
                          static_cast<float>(transform.pad_top)) /
                         transform.scale_y;

    const float source_width = static_cast<float>(transform.source_width);   // 原图右边界。
    const float source_height = static_cast<float>(transform.source_height); // 原图下边界。
    // 模型框可能落在填充区或越界，映射后必须裁剪到原图有效范围。
    const float clipped_left = std::clamp(left, 0.0F, source_width);
    const float clipped_top = std::clamp(top, 0.0F, source_height);
    const float clipped_right = std::clamp(right, 0.0F, source_width);
    const float clipped_bottom = std::clamp(bottom, 0.0F, source_height);

    return RectF{clipped_left, clipped_top, std::max(0.0F, clipped_right - clipped_left),
                 std::max(0.0F, clipped_bottom - clipped_top)};
}

}  // namespace rkav
