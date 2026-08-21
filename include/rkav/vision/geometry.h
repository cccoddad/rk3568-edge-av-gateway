// 文件作用：定义保持宽高比缩放（letterbox）的元数据和检测框反向映射函数。
// 主要知识点：坐标系转换、等比例缩放、补边、浮点比例和边界裁剪。
#pragma once

#include "rkav/common/result.h"
#include "rkav/common/types.h"

namespace rkav {

struct TransformMetadata {
    int source_width{0};        // 原始图像宽度。
    int source_height{0};       // 原始图像高度。
    int destination_width{0};   // 模型输入宽度。
    int destination_height{0};  // 模型输入高度。
    int resized_width{0};       // 保持比例缩放后的有效图像宽度。
    int resized_height{0};      // 保持比例缩放后的有效图像高度。
    int pad_left{0};            // 左侧补边像素数。
    int pad_top{0};             // 顶部补边像素数。
    float scale_x{0.0F};        // 原图到缩放图的水平方向比例。
    float scale_y{0.0F};        // 原图到缩放图的垂直方向比例。
};

/// 计算 source 放入 destination 时的等比例缩放尺寸、补边和实际比例。
Result<TransformMetadata> ComputeLetterboxTransform(int source_width, int source_height,
                                                    int destination_width, int destination_height);
/// 把模型输入坐标系中的框映射回原图坐标，并裁剪到原图边界。
RectF MapBoxToSource(const RectF& model_box, const TransformMetadata& transform);

}  // namespace rkav
