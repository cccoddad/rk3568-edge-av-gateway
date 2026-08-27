// 文件作用：实现 RKNN 1.4.0 YOLOv5 模型加载、RGB 预处理、NPU 推理和检测框映射。
// 主要知识点：RKNN tensor 契约、RAII、最近邻缩放、NCHW 输出和错误边界。
#include "rkav/vision/rknn_inference_engine.h"

#include <rknn_api.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

#include "yolov5_postprocess.h"

namespace rkav {
namespace {

constexpr std::uint32_t kInputCount = 1U;
constexpr std::uint32_t kOutputCount = RKAV_YOLOV5_OUTPUT_COUNT;

Error RknnError(ErrorCategory category, std::string operation, std::string message,
                int native_code = 0) {
    return Error{category,           native_code, "rknn_inference", std::move(operation),
                 std::move(message), false};
}

Result<std::vector<std::uint8_t>> ReadModel(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return Result<std::vector<std::uint8_t>>::Failure(
            RknnError(ErrorCategory::kDeviceNotFound, "read_model", "cannot open model: " + path));
    }
    const std::streampos end = stream.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) >
                        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return Result<std::vector<std::uint8_t>>::Failure(
            RknnError(ErrorCategory::kInvalidConfig, "read_model", "invalid RKNN model size"));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        return Result<std::vector<std::uint8_t>>::Failure(
            RknnError(ErrorCategory::kIo, "read_model", "failed to read complete RKNN model"));
    }
    return Result<std::vector<std::uint8_t>>::Success(std::move(bytes));
}

bool IsYolov5Input(const rknn_tensor_attr& attribute) {
    return attribute.n_dims == 4U && attribute.dims[0] == 1U && attribute.dims[1] == 640U &&
           attribute.dims[2] == 640U && attribute.dims[3] == 3U &&
           attribute.fmt == RKNN_TENSOR_NHWC && attribute.type == RKNN_TENSOR_INT8;
}

bool IsYolov5Output(const rknn_tensor_attr& attribute) {
    if (attribute.n_dims != 4U || attribute.dims[0] != 1U || attribute.dims[1] != 255U ||
        attribute.dims[2] != attribute.dims[3] || attribute.fmt != RKNN_TENSOR_NCHW) {
        return false;
    }
    return attribute.dims[2] == 80U || attribute.dims[2] == 40U || attribute.dims[2] == 20U;
}

std::vector<std::uint8_t> ResizeToModelInput(const VideoFrame& frame, std::uint32_t target_width,
                                             std::uint32_t target_height) {
    const std::size_t target_bytes = static_cast<std::size_t>(target_width) * target_height * 3U;
    std::vector<std::uint8_t> result(target_bytes);
    const auto* source = reinterpret_cast<const std::uint8_t*>(frame.buffer->data());
    const bool bgr = frame.format == PixelFormat::kBgr888;

    for (std::uint32_t target_y = 0; target_y < target_height; ++target_y) {
        const std::uint32_t source_y = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(target_y) * static_cast<std::uint32_t>(frame.height)) /
            target_height);
        const auto* source_row =
            source + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(frame.stride);
        auto* target_row = result.data() + static_cast<std::size_t>(target_y) * target_width * 3U;
        for (std::uint32_t target_x = 0; target_x < target_width; ++target_x) {
            const std::uint32_t source_x = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(target_x) * static_cast<std::uint32_t>(frame.width)) /
                target_width);
            const auto* source_pixel = source_row + static_cast<std::size_t>(source_x) * 3U;
            auto* target_pixel = target_row + static_cast<std::size_t>(target_x) * 3U;
            target_pixel[0] = source_pixel[bgr ? 2U : 0U];
            target_pixel[1] = source_pixel[1];
            target_pixel[2] = source_pixel[bgr ? 0U : 2U];
        }
    }
    return result;
}

class OutputLease {
   public:
    OutputLease(rknn_context context, std::array<rknn_output, kOutputCount>& outputs)
        : context_(context), outputs_(outputs) {}

    ~OutputLease() {
        if (acquired_) {
            static_cast<void>(rknn_outputs_release(context_, kOutputCount, outputs_.data()));
        }
    }

    void MarkAcquired() noexcept { acquired_ = true; }

    int Release() noexcept {
        if (!acquired_) {
            return RKNN_SUCC;
        }
        acquired_ = false;
        return rknn_outputs_release(context_, kOutputCount, outputs_.data());
    }

   private:
    rknn_context context_;
    std::array<rknn_output, kOutputCount>& outputs_;
    bool acquired_{false};
};

}  // namespace

struct RknnInferenceEngine::Impl {
    rknn_context context{0U};
    std::vector<std::uint8_t> model;
    rknn_tensor_attr input{};
    std::array<rknn_tensor_attr, kOutputCount> outputs{};

    ~Impl() {
        if (context != 0U) {
            static_cast<void>(rknn_destroy(context));
        }
    }
};

RknnInferenceEngine::RknnInferenceEngine(std::shared_ptr<IClock> clock)
    : clock_(std::move(clock)) {}

RknnInferenceEngine::~RknnInferenceEngine() = default;

Result<ModelInfo> RknnInferenceEngine::Open(const InferenceConfig& config) {
    std::scoped_lock lock(mutex_);
    if (impl_) {
        return Result<ModelInfo>::Failure(
            RknnError(ErrorCategory::kInvalidState, "open", "engine is already open"));
    }

    auto model = ReadModel(config.model_path);
    if (!model) {
        return Result<ModelInfo>::Failure(model.error());
    }

    auto candidate = std::make_unique<Impl>();
    candidate->model = std::move(model).value();
    const int init_status =
        rknn_init(&candidate->context, candidate->model.data(),
                  static_cast<std::uint32_t>(candidate->model.size()), 0U, nullptr);
    if (init_status != RKNN_SUCC) {
        return Result<ModelInfo>::Failure(
            RknnError(ErrorCategory::kInference, "init", "rknn_init failed", init_status));
    }

    rknn_input_output_num counts{};
    int status = rknn_query(candidate->context, RKNN_QUERY_IN_OUT_NUM, &counts, sizeof(counts));
    if (status != RKNN_SUCC || counts.n_input != kInputCount || counts.n_output != kOutputCount) {
        return Result<ModelInfo>::Failure(
            RknnError(ErrorCategory::kNotSupported, "query_tensor_count",
                      "model must expose one input and three YOLOv5 outputs", status));
    }

    candidate->input.index = 0U;
    status = rknn_query(candidate->context, RKNN_QUERY_INPUT_ATTR, &candidate->input,
                        sizeof(candidate->input));
    if (status != RKNN_SUCC || !IsYolov5Input(candidate->input)) {
        return Result<ModelInfo>::Failure(RknnError(ErrorCategory::kNotSupported, "query_input",
                                                    "model input must be NHWC INT8 1x640x640x3",
                                                    status));
    }

    bool grids[3]{false, false, false};
    for (std::uint32_t index = 0U; index < kOutputCount; ++index) {
        candidate->outputs[index].index = index;
        status = rknn_query(candidate->context, RKNN_QUERY_OUTPUT_ATTR, &candidate->outputs[index],
                            sizeof(candidate->outputs[index]));
        if (status != RKNN_SUCC || !IsYolov5Output(candidate->outputs[index])) {
            return Result<ModelInfo>::Failure(
                RknnError(ErrorCategory::kNotSupported, "query_output",
                          "model output does not match YOLOv5 NCHW contract", status));
        }
        const std::uint32_t grid = candidate->outputs[index].dims[2];
        const std::size_t grid_index = grid == 80U ? 0U : (grid == 40U ? 1U : 2U);
        if (grids[grid_index]) {
            return Result<ModelInfo>::Failure(
                RknnError(ErrorCategory::kNotSupported, "query_output",
                          "model contains duplicate YOLOv5 output grids"));
        }
        grids[grid_index] = true;
    }

    if (config.input_width != static_cast<int>(candidate->input.dims[2]) ||
        config.input_height != static_cast<int>(candidate->input.dims[1])) {
        return Result<ModelInfo>::Failure(
            RknnError(ErrorCategory::kInvalidConfig, "open",
                      "configured input dimensions do not match the RKNN model"));
    }

    config_ = config;
    impl_ = std::move(candidate);
    return Result<ModelInfo>::Success(ModelInfo{static_cast<int>(impl_->input.dims[2]),
                                                static_cast<int>(impl_->input.dims[1]), "rknn"});
}

Result<DetectionBatch> RknnInferenceEngine::Infer(const VideoFrame& frame, std::stop_token stop) {
    std::scoped_lock lock(mutex_);
    if (!impl_) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kInvalidState, "infer", "engine is not open"));
    }
    if (stop.stop_requested()) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kCancelled, "infer", "inference was cancelled"));
    }

    auto valid = ValidateVideoFrame(frame);
    if (!valid) {
        return Result<DetectionBatch>::Failure(valid.error());
    }
    if (frame.memory.kind != MemoryKind::kCpu ||
        (frame.format != PixelFormat::kRgb888 && frame.format != PixelFormat::kBgr888)) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kNotSupported, "preprocess",
                      "RKNN phase 1 requires a CPU RGB888 or BGR888 frame"));
    }

    const std::uint32_t input_width = impl_->input.dims[2];
    const std::uint32_t input_height = impl_->input.dims[1];
    auto input_bytes = ResizeToModelInput(frame, input_width, input_height);
    rknn_input input{};
    input.index = 0U;
    input.buf = input_bytes.data();
    input.size = static_cast<std::uint32_t>(input_bytes.size());
    input.pass_through = 0U;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;

    int status = rknn_inputs_set(impl_->context, kInputCount, &input);
    if (status != RKNN_SUCC) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kInference, "set_inputs", "rknn_inputs_set failed", status));
    }
    status = rknn_run(impl_->context, nullptr);
    if (status != RKNN_SUCC) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kInference, "run", "rknn_run failed", status));
    }

    std::array<rknn_output, kOutputCount> outputs{};
    for (std::uint32_t index = 0U; index < kOutputCount; ++index) {
        outputs[index].index = index;
        outputs[index].want_float = 1U;
        outputs[index].is_prealloc = 0U;
    }
    OutputLease lease(impl_->context, outputs);
    status = rknn_outputs_get(impl_->context, kOutputCount, outputs.data(), nullptr);
    if (status != RKNN_SUCC) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kInference, "get_outputs", "rknn_outputs_get failed", status));
    }
    lease.MarkAcquired();

    std::array<RkavYolov5Output, kOutputCount> yolov5_outputs{};
    for (std::uint32_t index = 0U; index < kOutputCount; ++index) {
        const std::size_t expected =
            static_cast<std::size_t>(impl_->outputs[index].n_elems) * sizeof(float);
        if (outputs[index].buf == nullptr || outputs[index].size != expected) {
            return Result<DetectionBatch>::Failure(
                RknnError(ErrorCategory::kInference, "validate_outputs",
                          "RKNN float output size does not match model metadata"));
        }
        yolov5_outputs[index] = RkavYolov5Output{
            static_cast<const float*>(outputs[index].buf), impl_->outputs[index].dims[1],
            impl_->outputs[index].dims[2], impl_->outputs[index].dims[3]};
    }

    std::vector<RkavYolov5Detection> decoded(config_.max_detections);
    const RkavYolov5Config postprocess_config{input_width, input_height, config_.object_threshold,
                                              config_.nms_threshold};
    RkavYolov5Result postprocess_result{};
    const auto postprocess_status =
        RkavYolov5Postprocess(&postprocess_config, yolov5_outputs.data(), decoded.data(),
                              decoded.size(), &postprocess_result);
    if (postprocess_status != RKAV_YOLOV5_OK) {
        return Result<DetectionBatch>::Failure(RknnError(ErrorCategory::kInference, "postprocess",
                                                         "YOLOv5 postprocess failed",
                                                         static_cast<int>(postprocess_status)));
    }

    const int release_status = lease.Release();
    if (release_status != RKNN_SUCC) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kInference, "release_outputs", "rknn_outputs_release failed",
                      release_status));
    }
    if (stop.stop_requested()) {
        return Result<DetectionBatch>::Failure(
            RknnError(ErrorCategory::kCancelled, "infer", "inference was cancelled"));
    }

    const float scale_x = static_cast<float>(frame.width) / static_cast<float>(input_width);
    const float scale_y = static_cast<float>(frame.height) / static_cast<float>(input_height);
    DetectionBatch batch;
    batch.frame_sequence = frame.sequence;
    batch.source_pts_us = frame.pts_us;
    batch.completed_at_us = clock_->NowUs();
    batch.items.reserve(postprocess_result.detection_count);
    for (std::size_t index = 0; index < postprocess_result.detection_count; ++index) {
        const auto& detection = decoded[index];
        batch.items.push_back(Detection{static_cast<int>(detection.class_id), detection.confidence,
                                        RectF{detection.left * scale_x, detection.top * scale_y,
                                              (detection.right - detection.left) * scale_x,
                                              (detection.bottom - detection.top) * scale_y}});
    }
    return Result<DetectionBatch>::Success(std::move(batch));
}

void RknnInferenceEngine::Close() noexcept {
    std::scoped_lock lock(mutex_);
    impl_.reset();
}

}  // namespace rkav
