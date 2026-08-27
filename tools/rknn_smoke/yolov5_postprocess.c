#include "yolov5_postprocess.h"

#include <math.h>
#include <stdlib.h>

enum {
    RKAV_YOLOV5_ANCHORS_PER_OUTPUT = 3,
    RKAV_YOLOV5_VALUES_PER_ANCHOR = 5 + RKAV_YOLOV5_CLASS_COUNT,
    RKAV_YOLOV5_CHANNEL_COUNT = RKAV_YOLOV5_ANCHORS_PER_OUTPUT * RKAV_YOLOV5_VALUES_PER_ANCHOR,
};

typedef struct {
    RkavYolov5Detection detection;
    bool suppressed;
} RkavYolov5Candidate;

static const float kAnchors[RKAV_YOLOV5_OUTPUT_COUNT][RKAV_YOLOV5_ANCHORS_PER_OUTPUT][2] = {
    {{10.0F, 13.0F}, {16.0F, 30.0F}, {33.0F, 23.0F}},
    {{30.0F, 61.0F}, {62.0F, 45.0F}, {59.0F, 119.0F}},
    {{116.0F, 90.0F}, {156.0F, 198.0F}, {373.0F, 326.0F}},
};

static const char* const kClassNames[RKAV_YOLOV5_CLASS_COUNT] = {
    "person",        "bicycle",       "car",           "motorbike",
    "aeroplane",     "bus",           "train",         "truck",
    "boat",          "traffic_light", "fire_hydrant",  "stop_sign",
    "parking_meter", "bench",         "bird",          "cat",
    "dog",           "horse",         "sheep",         "cow",
    "elephant",      "bear",          "zebra",         "giraffe",
    "backpack",      "umbrella",      "handbag",       "tie",
    "suitcase",      "frisbee",       "skis",          "snowboard",
    "sports_ball",   "kite",          "baseball_bat",  "baseball_glove",
    "skateboard",    "surfboard",     "tennis_racket", "bottle",
    "wine_glass",    "cup",           "fork",          "knife",
    "spoon",         "bowl",          "banana",        "apple",
    "sandwich",      "orange",        "broccoli",      "carrot",
    "hot_dog",       "pizza",         "donut",         "cake",
    "chair",         "sofa",          "pottedplant",   "bed",
    "diningtable",   "toilet",        "tvmonitor",     "laptop",
    "mouse",         "remote",        "keyboard",      "cell_phone",
    "microwave",     "oven",          "toaster",       "sink",
    "refrigerator",  "book",          "clock",         "vase",
    "scissors",      "teddy_bear",    "hair_drier",    "toothbrush",
};

static float Sigmoid(float value) {
    if (value >= 0.0F) {
        return 1.0F / (1.0F + expf(-value));
    }
    const float exponential = expf(value);
    return exponential / (1.0F + exponential);
}

static float Clamp(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static size_t TensorIndex(const RkavYolov5Output* output, uint32_t anchor, uint32_t value_index,
                          uint32_t row, uint32_t column) {
    const size_t channel = (size_t)anchor * RKAV_YOLOV5_VALUES_PER_ANCHOR + value_index;
    return (channel * output->height + row) * output->width + column;
}

static int CandidateOrder(const void* left_pointer, const void* right_pointer) {
    const RkavYolov5Candidate* left = left_pointer;
    const RkavYolov5Candidate* right = right_pointer;
    if (left->detection.confidence > right->detection.confidence) {
        return -1;
    }
    if (left->detection.confidence < right->detection.confidence) {
        return 1;
    }
    if (left->detection.class_id < right->detection.class_id) {
        return -1;
    }
    if (left->detection.class_id > right->detection.class_id) {
        return 1;
    }
    if (left->detection.left < right->detection.left) {
        return -1;
    }
    if (left->detection.left > right->detection.left) {
        return 1;
    }
    if (left->detection.top < right->detection.top) {
        return -1;
    }
    if (left->detection.top > right->detection.top) {
        return 1;
    }
    return 0;
}

static float IntersectionOverUnion(const RkavYolov5Detection* left,
                                   const RkavYolov5Detection* right) {
    const float intersection_left = fmaxf(left->left, right->left);
    const float intersection_top = fmaxf(left->top, right->top);
    const float intersection_right = fminf(left->right, right->right);
    const float intersection_bottom = fminf(left->bottom, right->bottom);
    const float intersection_width = fmaxf(0.0F, intersection_right - intersection_left + 0.00001F);
    const float intersection_height =
        fmaxf(0.0F, intersection_bottom - intersection_top + 0.00001F);
    const float intersection_area = intersection_width * intersection_height;
    const float left_area = (left->right - left->left) * (left->bottom - left->top);
    const float right_area = (right->right - right->left) * (right->bottom - right->top);
    const float union_area = left_area + right_area - intersection_area;
    return union_area <= 0.0F ? 0.0F : intersection_area / union_area;
}

static bool ResolveScale(const RkavYolov5Config* config, const RkavYolov5Output* output,
                         uint32_t* scale_index, float* stride_x, float* stride_y) {
    if (output->data == NULL || output->channels != RKAV_YOLOV5_CHANNEL_COUNT ||
        output->height == 0U || output->width == 0U) {
        return false;
    }
    static const uint32_t kStrides[RKAV_YOLOV5_OUTPUT_COUNT] = {8U, 16U, 32U};
    for (uint32_t index = 0U; index < RKAV_YOLOV5_OUTPUT_COUNT; ++index) {
        if (output->width * kStrides[index] == config->image_width &&
            output->height * kStrides[index] == config->image_height) {
            *scale_index = index;
            *stride_x = (float)config->image_width / (float)output->width;
            *stride_y = (float)config->image_height / (float)output->height;
            return true;
        }
    }
    return false;
}

RkavYolov5Status RkavYolov5Postprocess(const RkavYolov5Config* config,
                                       const RkavYolov5Output outputs[RKAV_YOLOV5_OUTPUT_COUNT],
                                       RkavYolov5Detection* detections, size_t detection_capacity,
                                       RkavYolov5Result* result) {
    if (config == NULL || outputs == NULL || detections == NULL || result == NULL ||
        detection_capacity == 0U || config->image_width == 0U || config->image_height == 0U ||
        !isfinite(config->object_threshold) || config->object_threshold <= 0.0F ||
        config->object_threshold >= 1.0F || !isfinite(config->nms_threshold) ||
        config->nms_threshold <= 0.0F || config->nms_threshold >= 1.0F) {
        return RKAV_YOLOV5_INVALID_ARGUMENT;
    }
    *result = (RkavYolov5Result){0};

    uint32_t scale_indices[RKAV_YOLOV5_OUTPUT_COUNT] = {0};
    float stride_x[RKAV_YOLOV5_OUTPUT_COUNT] = {0};
    float stride_y[RKAV_YOLOV5_OUTPUT_COUNT] = {0};
    bool scale_seen[RKAV_YOLOV5_OUTPUT_COUNT] = {false};
    size_t maximum_candidates = 0U;
    for (uint32_t output_index = 0U; output_index < RKAV_YOLOV5_OUTPUT_COUNT; ++output_index) {
        if (!ResolveScale(config, &outputs[output_index], &scale_indices[output_index],
                          &stride_x[output_index], &stride_y[output_index]) ||
            scale_seen[scale_indices[output_index]]) {
            return RKAV_YOLOV5_UNSUPPORTED_SHAPE;
        }
        scale_seen[scale_indices[output_index]] = true;
        const size_t cells = (size_t)outputs[output_index].height * outputs[output_index].width;
        if (cells > (SIZE_MAX - maximum_candidates) / RKAV_YOLOV5_ANCHORS_PER_OUTPUT) {
            return RKAV_YOLOV5_UNSUPPORTED_SHAPE;
        }
        maximum_candidates += cells * RKAV_YOLOV5_ANCHORS_PER_OUTPUT;
    }

    RkavYolov5Candidate* candidates = calloc(maximum_candidates, sizeof(*candidates));
    if (candidates == NULL) {
        return RKAV_YOLOV5_ALLOCATION_FAILED;
    }

    size_t candidate_count = 0U;
    for (uint32_t output_index = 0U; output_index < RKAV_YOLOV5_OUTPUT_COUNT; ++output_index) {
        const RkavYolov5Output* output = &outputs[output_index];
        const uint32_t scale_index = scale_indices[output_index];
        for (uint32_t row = 0U; row < output->height; ++row) {
            for (uint32_t column = 0U; column < output->width; ++column) {
                for (uint32_t anchor = 0U; anchor < RKAV_YOLOV5_ANCHORS_PER_OUTPUT; ++anchor) {
                    const float object_logit =
                        output->data[TensorIndex(output, anchor, 4U, row, column)];
                    if (!isfinite(object_logit)) {
                        continue;
                    }
                    const float object_probability = Sigmoid(object_logit);
                    if (object_probability < config->object_threshold) {
                        continue;
                    }

                    uint32_t best_class = 0U;
                    float best_class_probability = -1.0F;
                    for (uint32_t class_id = 0U; class_id < RKAV_YOLOV5_CLASS_COUNT; ++class_id) {
                        const float class_logit =
                            output->data[TensorIndex(output, anchor, 5U + class_id, row, column)];
                        if (!isfinite(class_logit)) {
                            continue;
                        }
                        const float class_probability = Sigmoid(class_logit);
                        if (class_probability > best_class_probability) {
                            best_class_probability = class_probability;
                            best_class = class_id;
                        }
                    }
                    if (best_class_probability < config->object_threshold) {
                        continue;
                    }

                    const float x_logit =
                        output->data[TensorIndex(output, anchor, 0U, row, column)];
                    const float y_logit =
                        output->data[TensorIndex(output, anchor, 1U, row, column)];
                    const float width_logit =
                        output->data[TensorIndex(output, anchor, 2U, row, column)];
                    const float height_logit =
                        output->data[TensorIndex(output, anchor, 3U, row, column)];
                    if (!isfinite(x_logit) || !isfinite(y_logit) || !isfinite(width_logit) ||
                        !isfinite(height_logit)) {
                        continue;
                    }

                    const float center_x =
                        (Sigmoid(x_logit) * 2.0F - 0.5F + (float)column) * stride_x[output_index];
                    const float center_y =
                        (Sigmoid(y_logit) * 2.0F - 0.5F + (float)row) * stride_y[output_index];
                    const float scaled_width = Sigmoid(width_logit) * 2.0F;
                    const float scaled_height = Sigmoid(height_logit) * 2.0F;
                    const float width =
                        scaled_width * scaled_width * kAnchors[scale_index][anchor][0];
                    const float height =
                        scaled_height * scaled_height * kAnchors[scale_index][anchor][1];

                    RkavYolov5Detection detection = {
                        .left = Clamp(center_x - width * 0.5F, 0.0F, (float)config->image_width),
                        .top = Clamp(center_y - height * 0.5F, 0.0F, (float)config->image_height),
                        .right = Clamp(center_x + width * 0.5F, 0.0F, (float)config->image_width),
                        .bottom =
                            Clamp(center_y + height * 0.5F, 0.0F, (float)config->image_height),
                        .confidence = object_probability * best_class_probability,
                        .class_id = best_class,
                    };
                    if (detection.right <= detection.left || detection.bottom <= detection.top) {
                        continue;
                    }
                    candidates[candidate_count].detection = detection;
                    ++candidate_count;
                }
            }
        }
    }

    qsort(candidates, candidate_count, sizeof(*candidates), CandidateOrder);
    size_t selected_count = 0U;
    size_t detection_count = 0U;
    for (size_t index = 0U; index < candidate_count; ++index) {
        if (candidates[index].suppressed) {
            continue;
        }
        if (detection_count < detection_capacity) {
            detections[detection_count] = candidates[index].detection;
            ++detection_count;
        }
        ++selected_count;
        for (size_t other = index + 1U; other < candidate_count; ++other) {
            if (!candidates[other].suppressed &&
                candidates[other].detection.class_id == candidates[index].detection.class_id &&
                IntersectionOverUnion(&candidates[index].detection, &candidates[other].detection) >
                    config->nms_threshold) {
                candidates[other].suppressed = true;
            }
        }
    }

    result->candidate_count = candidate_count;
    result->selected_count = selected_count;
    result->detection_count = detection_count;
    result->truncated = selected_count > detection_capacity;
    free(candidates);
    return RKAV_YOLOV5_OK;
}

const char* RkavYolov5ClassName(uint32_t class_id) {
    return class_id < RKAV_YOLOV5_CLASS_COUNT ? kClassNames[class_id] : "unknown";
}
