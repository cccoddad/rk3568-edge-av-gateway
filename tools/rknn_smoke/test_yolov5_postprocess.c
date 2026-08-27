#include "yolov5_postprocess.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    kChannels = 255,
};

static void Fail(const char* message) {
    fprintf(stderr, "test failure: %s\n", message);
    exit(1);
}

static void Require(bool condition, const char* message) {
    if (!condition) {
        Fail(message);
    }
}

static bool NearlyEqual(float left, float right, float tolerance) {
    return fabsf(left - right) <= tolerance;
}

static size_t Index(const RkavYolov5Output* output, uint32_t anchor, uint32_t value_index,
                    uint32_t row, uint32_t column) {
    const size_t channel = (size_t)anchor * 85U + value_index;
    return (channel * output->height + row) * output->width + column;
}

static void SetCandidate(RkavYolov5Output* output, uint32_t anchor, uint32_t row,
                         uint32_t column, float x, float y, float width, float height,
                         float object, uint32_t class_id, float class_score) {
    float* data = (float*)output->data;
    data[Index(output, anchor, 0U, row, column)] = x;
    data[Index(output, anchor, 1U, row, column)] = y;
    data[Index(output, anchor, 2U, row, column)] = width;
    data[Index(output, anchor, 3U, row, column)] = height;
    data[Index(output, anchor, 4U, row, column)] = object;
    data[Index(output, anchor, 5U + class_id, row, column)] = class_score;
}

int main(void) {
    const uint32_t sizes[RKAV_YOLOV5_OUTPUT_COUNT] = {80U, 40U, 20U};
    RkavYolov5Output outputs[RKAV_YOLOV5_OUTPUT_COUNT] = {0};
    for (uint32_t index = 0U; index < RKAV_YOLOV5_OUTPUT_COUNT; ++index) {
        outputs[index].channels = kChannels;
        outputs[index].height = sizes[index];
        outputs[index].width = sizes[index];
        const size_t count = (size_t)kChannels * sizes[index] * sizes[index];
        float* data = malloc(count * sizeof(*data));
        Require(data != NULL, "allocate output");
        for (size_t value = 0U; value < count; ++value) {
            data[value] = -100.0F;
        }
        outputs[index].data = data;
    }

    SetCandidate(&outputs[0], 0U, 10U, 20U, 0.0F, 0.0F, 0.0F, 0.0F, 4.0F, 0U, 3.0F);
    SetCandidate(&outputs[0], 0U, 10U, 21U, -2.7080502F, 0.0F, 0.0F, 0.0F, 2.0F,
                 0U, 2.0F);
    SetCandidate(&outputs[1], 0U, 12U, 10U, 0.0F, 0.0F, 0.0F, 0.0F, 3.0F, 5U, 2.5F);

    const RkavYolov5Config config = {
        .image_width = 640U,
        .image_height = 640U,
        .object_threshold = 0.25F,
        .nms_threshold = 0.45F,
    };
    RkavYolov5Detection detections[8] = {0};
    RkavYolov5Result result = {0};
    Require(RkavYolov5Postprocess(&config, outputs, detections, 8U, &result) ==
                RKAV_YOLOV5_OK,
            "postprocess succeeds");
    Require(result.candidate_count == 3U, "three candidates");
    Require(result.selected_count == 2U, "nms selects two");
    Require(result.detection_count == 2U, "two returned detections");
    Require(!result.truncated, "result is not truncated");
    Require(detections[0].class_id == 0U, "person is first");
    Require(NearlyEqual(detections[0].left, 159.0F, 0.001F), "person left");
    Require(NearlyEqual(detections[0].top, 77.5F, 0.001F), "person top");
    Require(NearlyEqual(detections[0].right, 169.0F, 0.001F), "person right");
    Require(NearlyEqual(detections[0].bottom, 90.5F, 0.001F), "person bottom");
    Require(detections[1].class_id == 5U, "bus is second");
    Require(RkavYolov5ClassName(0U)[0] == 'p', "person class name");
    Require(RkavYolov5ClassName(5U)[0] == 'b', "bus class name");

    RkavYolov5Result truncated = {0};
    Require(RkavYolov5Postprocess(&config, outputs, detections, 1U, &truncated) ==
                RKAV_YOLOV5_OK,
            "truncated postprocess succeeds");
    Require(truncated.selected_count == 2U && truncated.detection_count == 1U &&
                truncated.truncated,
            "truncation is reported");

    const uint32_t original_width = outputs[0].width;
    outputs[0].width = 79U;
    Require(RkavYolov5Postprocess(&config, outputs, detections, 8U, &result) ==
                RKAV_YOLOV5_UNSUPPORTED_SHAPE,
            "invalid grid is rejected");
    outputs[0].width = original_width;

    for (uint32_t index = 0U; index < RKAV_YOLOV5_OUTPUT_COUNT; ++index) {
        free((void*)outputs[index].data);
    }
    puts("yolov5_postprocess_test=passed");
    return 0;
}
