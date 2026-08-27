#ifndef RKAV_YOLOV5_POSTPROCESS_H
#define RKAV_YOLOV5_POSTPROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RKAV_YOLOV5_OUTPUT_COUNT = 3,
    RKAV_YOLOV5_CLASS_COUNT = 80,
};

typedef struct {
    const float* data;
    uint32_t channels;
    uint32_t height;
    uint32_t width;
} RkavYolov5Output;

typedef struct {
    uint32_t image_width;
    uint32_t image_height;
    float object_threshold;
    float nms_threshold;
} RkavYolov5Config;

typedef struct {
    float left;
    float top;
    float right;
    float bottom;
    float confidence;
    uint32_t class_id;
} RkavYolov5Detection;

typedef struct {
    size_t candidate_count;
    size_t selected_count;
    size_t detection_count;
    bool truncated;
} RkavYolov5Result;

typedef enum {
    RKAV_YOLOV5_OK = 0,
    RKAV_YOLOV5_INVALID_ARGUMENT = 1,
    RKAV_YOLOV5_UNSUPPORTED_SHAPE = 2,
    RKAV_YOLOV5_ALLOCATION_FAILED = 3,
} RkavYolov5Status;

RkavYolov5Status RkavYolov5Postprocess(const RkavYolov5Config* config,
                                       const RkavYolov5Output outputs[RKAV_YOLOV5_OUTPUT_COUNT],
                                       RkavYolov5Detection* detections, size_t detection_capacity,
                                       RkavYolov5Result* result);

const char* RkavYolov5ClassName(uint32_t class_id);

#endif
