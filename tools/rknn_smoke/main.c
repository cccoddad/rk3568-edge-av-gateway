// File purpose: validate the RK3568 RKNN runtime, model metadata, inference, and output access.
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <rknn_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolov5_postprocess.h"

enum {
    RKAV_DEFAULT_ITERATIONS = 1,
    RKAV_MAXIMUM_ITERATIONS = 100000,
    RKAV_MAXIMUM_TENSOR_COUNT = 256,
    RKAV_TOP_RESULT_COUNT = 5,
    RKAV_MAXIMUM_DETECTION_COUNT = 256,
};

typedef struct {
    void* data;
    uint32_t size;
} RkavModel;

static int PrintRknnError(const char* operation, int code) {
    fprintf(stderr, "error operation=%s rknn_code=%d\n", operation, code);
    return 1;
}

static bool ParseIterations(const char* text, uint32_t* value) {
    char* end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0UL ||
        parsed > RKAV_MAXIMUM_ITERATIONS) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool ReadModel(const char* path, RkavModel* model) {
    FILE* stream = fopen(path, "rb");
    if (stream == NULL) {
        fprintf(stderr, "error operation=open_model path=%s errno=%d\n", path, errno);
        return false;
    }
    if (fseek(stream, 0L, SEEK_END) != 0) {
        fprintf(stderr, "error operation=seek_model_end path=%s errno=%d\n", path, errno);
        fclose(stream);
        return false;
    }
    const long end_position = ftell(stream);
    if (end_position <= 0L || (unsigned long)end_position > UINT32_MAX) {
        fprintf(stderr, "error operation=validate_model_size path=%s\n", path);
        fclose(stream);
        return false;
    }
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "error operation=seek_model_start path=%s errno=%d\n", path, errno);
        fclose(stream);
        return false;
    }

    model->size = (uint32_t)end_position;
    model->data = malloc(model->size);
    if (model->data == NULL) {
        fprintf(stderr, "error operation=allocate_model bytes=%" PRIu32 "\n", model->size);
        fclose(stream);
        return false;
    }
    const size_t bytes_read = fread(model->data, 1U, model->size, stream);
    const bool read_succeeded = bytes_read == model->size && ferror(stream) == 0;
    fclose(stream);
    if (!read_succeeded) {
        fprintf(stderr, "error operation=read_model path=%s bytes=%zu\n", path, bytes_read);
        free(model->data);
        model->data = NULL;
        model->size = 0U;
        return false;
    }
    return true;
}

static bool ReadExactInput(const char* path, void* buffer, uint32_t expected_size) {
    FILE* stream = fopen(path, "rb");
    if (stream == NULL) {
        fprintf(stderr, "error operation=open_input path=%s errno=%d\n", path, errno);
        return false;
    }
    if (fseek(stream, 0L, SEEK_END) != 0) {
        fprintf(stderr, "error operation=seek_input_end path=%s errno=%d\n", path, errno);
        fclose(stream);
        return false;
    }
    const long end_position = ftell(stream);
    if (end_position < 0L || (unsigned long)end_position != (unsigned long)expected_size) {
        fprintf(stderr,
                "error operation=validate_input_size path=%s expected=%" PRIu32 " actual=%ld\n",
                path, expected_size, end_position);
        fclose(stream);
        return false;
    }
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "error operation=seek_input_start path=%s errno=%d\n", path, errno);
        fclose(stream);
        return false;
    }
    const size_t bytes_read = fread(buffer, 1U, expected_size, stream);
    const bool read_succeeded = bytes_read == expected_size && ferror(stream) == 0;
    fclose(stream);
    if (!read_succeeded) {
        fprintf(stderr, "error operation=read_input path=%s bytes=%zu\n", path, bytes_read);
        return false;
    }
    return true;
}

static void PrintDimensions(const rknn_tensor_attr* attribute) {
    for (uint32_t index = 0U; index < attribute->n_dims; ++index) {
        if (index != 0U) {
            putchar('x');
        }
        printf("%" PRIu32, attribute->dims[index]);
    }
}

static void PrintTensor(const char* direction, const rknn_tensor_attr* attribute) {
    printf("tensor direction=%s index=%" PRIu32 " name=%s dims=", direction, attribute->index,
           attribute->name);
    PrintDimensions(attribute);
    printf(" elements=%" PRIu32 " bytes=%" PRIu32 " stride_bytes=%" PRIu32
           " format=%s type=%s quantization=%s zero_point=%d scale=%g\n",
           attribute->n_elems, attribute->size, attribute->size_with_stride,
           get_format_string(attribute->fmt), get_type_string(attribute->type),
           get_qnt_type_string(attribute->qnt_type), attribute->zp, (double)attribute->scale);
}

static bool QueryTensors(rknn_context context, rknn_query_cmd command, uint32_t count,
                         const char* direction, rknn_tensor_attr* attributes) {
    for (uint32_t index = 0U; index < count; ++index) {
        memset(&attributes[index], 0, sizeof(attributes[index]));
        attributes[index].index = index;
        const int result =
            rknn_query(context, command, &attributes[index], sizeof(attributes[index]));
        if (result != RKNN_SUCC) {
            PrintRknnError("query_tensor", result);
            return false;
        }
        if (attributes[index].n_dims > RKNN_MAX_DIMS) {
            fprintf(stderr,
                    "error operation=validate_tensor_dimensions direction=%s index=%" PRIu32
                    " dimensions=%" PRIu32 "\n",
                    direction, index, attributes[index].n_dims);
            return false;
        }
        PrintTensor(direction, &attributes[index]);
    }
    return true;
}

static uint64_t HashBytes(const void* data, size_t size) {
    const uint64_t offset_basis = UINT64_C(14695981039346656037);
    const uint64_t prime = UINT64_C(1099511628211);
    const uint8_t* bytes = data;
    uint64_t hash = offset_basis;
    for (size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= prime;
    }
    return hash;
}

static void PrintTopResults(uint32_t iteration, uint32_t output_index, const float* values,
                            size_t count) {
    size_t top_indices[RKAV_TOP_RESULT_COUNT];
    float top_values[RKAV_TOP_RESULT_COUNT];
    for (size_t rank = 0U; rank < RKAV_TOP_RESULT_COUNT; ++rank) {
        top_indices[rank] = SIZE_MAX;
        top_values[rank] = -INFINITY;
    }

    for (size_t value_index = 0U; value_index < count; ++value_index) {
        const float value = values[value_index];
        if (!isfinite(value)) {
            continue;
        }
        for (size_t rank = 0U; rank < RKAV_TOP_RESULT_COUNT; ++rank) {
            const bool is_better = top_indices[rank] == SIZE_MAX || value > top_values[rank] ||
                                   (value == top_values[rank] && value_index < top_indices[rank]);
            if (!is_better) {
                continue;
            }
            for (size_t shift = RKAV_TOP_RESULT_COUNT - 1U; shift > rank; --shift) {
                top_indices[shift] = top_indices[shift - 1U];
                top_values[shift] = top_values[shift - 1U];
            }
            top_indices[rank] = value_index;
            top_values[rank] = value;
            break;
        }
    }

    for (size_t rank = 0U; rank < RKAV_TOP_RESULT_COUNT; ++rank) {
        if (top_indices[rank] == SIZE_MAX) {
            break;
        }
        printf("top_result iteration=%" PRIu32 " output=%" PRIu32
               " rank=%zu class_index=%zu score=%.8g\n",
               iteration, output_index, rank + 1U, top_indices[rank], (double)top_values[rank]);
    }
}

static bool PrintOutputSummary(uint32_t iteration, const rknn_output* outputs,
                               uint32_t output_count) {
    for (uint32_t index = 0U; index < output_count; ++index) {
        const rknn_output* output = &outputs[index];
        if (output->buf == NULL || output->size == 0U || output->size % sizeof(float) != 0U) {
            fprintf(stderr, "error operation=validate_output index=%" PRIu32 " bytes=%" PRIu32 "\n",
                    index, output->size);
            return false;
        }
        const size_t count = output->size / sizeof(float);
        const float* values = output->buf;
        double sum = 0.0;
        float minimum = INFINITY;
        float maximum = -INFINITY;
        size_t finite_count = 0U;
        for (size_t value_index = 0U; value_index < count; ++value_index) {
            const float value = values[value_index];
            if (isfinite(value)) {
                minimum = value < minimum ? value : minimum;
                maximum = value > maximum ? value : maximum;
                sum += value;
                ++finite_count;
            }
        }
        const double mean = finite_count == 0U ? 0.0 : sum / (double)finite_count;
        printf("output iteration=%" PRIu32 " index=%" PRIu32 " bytes=%" PRIu32
               " floats=%zu finite=%zu min=%.8g max=%.8g mean=%.8g"
               " fnv1a64=0x%016" PRIx64 "\n",
               iteration, index, output->size, count, finite_count, (double)minimum,
               (double)maximum, mean, HashBytes(output->buf, output->size));
        PrintTopResults(iteration, index, values, count);
    }
    return true;
}

static bool IsYolov5Contract(const rknn_tensor_attr* input_attributes, uint32_t input_count,
                             const rknn_tensor_attr* output_attributes, uint32_t output_count,
                             const rknn_output* outputs) {
    if (input_count != 1U || output_count != RKAV_YOLOV5_OUTPUT_COUNT) {
        return false;
    }
    const rknn_tensor_attr* input = &input_attributes[0];
    if (input->n_dims != 4U || input->dims[0] != 1U || input->dims[1] != 640U ||
        input->dims[2] != 640U || input->dims[3] != 3U || input->fmt != RKNN_TENSOR_NHWC) {
        return false;
    }

    bool grids_seen[RKAV_YOLOV5_OUTPUT_COUNT] = {false};
    const uint32_t grid_sizes[RKAV_YOLOV5_OUTPUT_COUNT] = {80U, 40U, 20U};
    for (uint32_t output_index = 0U; output_index < output_count; ++output_index) {
        const rknn_tensor_attr* attribute = &output_attributes[output_index];
        if (attribute->n_dims != 4U || attribute->dims[0] != 1U || attribute->dims[1] != 255U ||
            attribute->dims[2] != attribute->dims[3] || attribute->fmt != RKNN_TENSOR_NCHW ||
            outputs[output_index].buf == NULL ||
            outputs[output_index].size != attribute->n_elems * sizeof(float)) {
            return false;
        }
        bool grid_matched = false;
        for (uint32_t grid_index = 0U; grid_index < RKAV_YOLOV5_OUTPUT_COUNT; ++grid_index) {
            if (attribute->dims[2] == grid_sizes[grid_index] && !grids_seen[grid_index]) {
                grids_seen[grid_index] = true;
                grid_matched = true;
                break;
            }
        }
        if (!grid_matched) {
            return false;
        }
    }
    return true;
}

static bool PrintYolov5Detections(uint32_t iteration, const rknn_tensor_attr* input_attributes,
                                  uint32_t input_count, const rknn_tensor_attr* output_attributes,
                                  uint32_t output_count, const rknn_output* outputs) {
    if (!IsYolov5Contract(input_attributes, input_count, output_attributes, output_count,
                          outputs)) {
        return true;
    }

    RkavYolov5Output yolov5_outputs[RKAV_YOLOV5_OUTPUT_COUNT] = {0};
    for (uint32_t index = 0U; index < RKAV_YOLOV5_OUTPUT_COUNT; ++index) {
        yolov5_outputs[index].data = outputs[index].buf;
        yolov5_outputs[index].channels = output_attributes[index].dims[1];
        yolov5_outputs[index].height = output_attributes[index].dims[2];
        yolov5_outputs[index].width = output_attributes[index].dims[3];
    }
    const RkavYolov5Config config = {
        .image_width = input_attributes[0].dims[2],
        .image_height = input_attributes[0].dims[1],
        .object_threshold = 0.25F,
        .nms_threshold = 0.45F,
    };
    RkavYolov5Detection detections[RKAV_MAXIMUM_DETECTION_COUNT] = {0};
    RkavYolov5Result result = {0};
    const RkavYolov5Status postprocess_status = RkavYolov5Postprocess(
        &config, yolov5_outputs, detections, RKAV_MAXIMUM_DETECTION_COUNT, &result);
    if (postprocess_status != RKAV_YOLOV5_OK) {
        fprintf(stderr, "error operation=yolov5_postprocess status=%d\n", (int)postprocess_status);
        return false;
    }

    printf("yolov5_postprocess iteration=%" PRIu32
           " status=passed candidates=%zu selected=%zu returned=%zu truncated=%s"
           " object_threshold=%.2f nms_threshold=%.2f\n",
           iteration, result.candidate_count, result.selected_count, result.detection_count,
           result.truncated ? "true" : "false", (double)config.object_threshold,
           (double)config.nms_threshold);
    for (size_t index = 0U; index < result.detection_count; ++index) {
        const RkavYolov5Detection* detection = &detections[index];
        printf("detection iteration=%" PRIu32 " rank=%zu class_id=%" PRIu32
               " class=%s confidence=%.8g"
               " left=%.3f top=%.3f right=%.3f bottom=%.3f\n",
               iteration, index + 1U, detection->class_id, RkavYolov5ClassName(detection->class_id),
               (double)detection->confidence, (double)detection->left, (double)detection->top,
               (double)detection->right, (double)detection->bottom);
    }
    return true;
}

static void FreeInputBuffers(void** buffers, uint32_t count) {
    if (buffers == NULL) {
        return;
    }
    for (uint32_t index = 0U; index < count; ++index) {
        free(buffers[index]);
    }
    free(buffers);
}

static int Run(const char* model_path, uint32_t iterations, const char* input_path) {
    int status = 1;
    RkavModel model = {0};
    rknn_context context = 0U;
    rknn_tensor_attr* input_attributes = NULL;
    rknn_tensor_attr* output_attributes = NULL;
    rknn_input* inputs = NULL;
    rknn_output* outputs = NULL;
    void** input_buffers = NULL;
    uint32_t input_count = 0U;

    if (!ReadModel(model_path, &model)) {
        return 3;
    }
    printf("model path=%s bytes=%" PRIu32 "\n", model_path, model.size);

    const int init_result = rknn_init(&context, model.data, model.size, 0U, NULL);
    if (init_result != RKNN_SUCC) {
        status = PrintRknnError("init", init_result);
        goto cleanup;
    }

    rknn_sdk_version version = {0};
    const int version_result =
        rknn_query(context, RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    if (version_result != RKNN_SUCC) {
        status = PrintRknnError("query_sdk_version", version_result);
        goto cleanup;
    }
    printf("runtime api_version=%s driver_version=%s\n", version.api_version, version.drv_version);

    rknn_input_output_num counts = {0};
    const int count_result = rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &counts, sizeof(counts));
    if (count_result != RKNN_SUCC) {
        status = PrintRknnError("query_input_output_count", count_result);
        goto cleanup;
    }
    if (counts.n_input == 0U || counts.n_input > RKAV_MAXIMUM_TENSOR_COUNT ||
        counts.n_output == 0U || counts.n_output > RKAV_MAXIMUM_TENSOR_COUNT) {
        fprintf(stderr,
                "error operation=validate_input_output_count inputs=%" PRIu32 " outputs=%" PRIu32
                "\n",
                counts.n_input, counts.n_output);
        goto cleanup;
    }
    input_count = counts.n_input;
    printf("model inputs=%" PRIu32 " outputs=%" PRIu32 "\n", counts.n_input, counts.n_output);

    input_attributes = calloc(counts.n_input, sizeof(*input_attributes));
    output_attributes = calloc(counts.n_output, sizeof(*output_attributes));
    inputs = calloc(counts.n_input, sizeof(*inputs));
    outputs = calloc(counts.n_output, sizeof(*outputs));
    input_buffers = calloc(counts.n_input, sizeof(*input_buffers));
    if (input_attributes == NULL || output_attributes == NULL || inputs == NULL ||
        outputs == NULL || input_buffers == NULL) {
        fprintf(stderr, "error operation=allocate_tensor_metadata\n");
        goto cleanup;
    }
    if (!QueryTensors(context, RKNN_QUERY_INPUT_ATTR, counts.n_input, "input", input_attributes) ||
        !QueryTensors(context, RKNN_QUERY_OUTPUT_ATTR, counts.n_output, "output",
                      output_attributes)) {
        goto cleanup;
    }

    if (input_path != NULL && counts.n_input != 1U) {
        fprintf(stderr, "error operation=validate_raw_input_count expected=1 actual=%" PRIu32 "\n",
                counts.n_input);
        goto cleanup;
    }

    for (uint32_t index = 0U; index < counts.n_input; ++index) {
        const rknn_tensor_attr* attribute = &input_attributes[index];
        if (attribute->size == 0U) {
            fprintf(stderr, "error operation=validate_input_size index=%" PRIu32 "\n", index);
            goto cleanup;
        }
        if (input_path != NULL &&
            (attribute->fmt != RKNN_TENSOR_NHWC || attribute->type != RKNN_TENSOR_INT8 ||
             attribute->n_elems != attribute->size)) {
            fprintf(stderr,
                    "error operation=validate_raw_rgb_contract index=%" PRIu32
                    " format=%s type=%s elements=%" PRIu32 " bytes=%" PRIu32 "\n",
                    index, get_format_string(attribute->fmt), get_type_string(attribute->type),
                    attribute->n_elems, attribute->size);
            goto cleanup;
        }
        input_buffers[index] = calloc(attribute->size, 1U);
        if (input_buffers[index] == NULL) {
            fprintf(stderr, "error operation=allocate_input index=%" PRIu32 " bytes=%" PRIu32 "\n",
                    index, attribute->size);
            goto cleanup;
        }
        if (input_path != NULL &&
            !ReadExactInput(input_path, input_buffers[index], attribute->size)) {
            goto cleanup;
        }
        inputs[index].index = index;
        inputs[index].buf = input_buffers[index];
        inputs[index].size = attribute->size;
        inputs[index].pass_through = 0U;
        inputs[index].type = input_path == NULL ? attribute->type : RKNN_TENSOR_UINT8;
        inputs[index].fmt = attribute->fmt;
        printf("input source=%s index=%" PRIu32 " bytes=%" PRIu32
               " format=%s type=%s fnv1a64=0x%016" PRIx64,
               input_path == NULL ? "zero" : "raw_rgb", index, inputs[index].size,
               get_format_string(inputs[index].fmt), get_type_string(inputs[index].type),
               HashBytes(inputs[index].buf, inputs[index].size));
        if (input_path != NULL) {
            printf(" path=%s", input_path);
        }
        putchar('\n');
    }

    for (uint32_t iteration = 1U; iteration <= iterations; ++iteration) {
        const int inputs_result = rknn_inputs_set(context, counts.n_input, inputs);
        if (inputs_result != RKNN_SUCC) {
            status = PrintRknnError("set_inputs", inputs_result);
            goto cleanup;
        }
        const int run_result = rknn_run(context, NULL);
        if (run_result != RKNN_SUCC) {
            status = PrintRknnError("run", run_result);
            goto cleanup;
        }

        memset(outputs, 0, counts.n_output * sizeof(*outputs));
        for (uint32_t index = 0U; index < counts.n_output; ++index) {
            outputs[index].index = index;
            outputs[index].want_float = 1U;
            outputs[index].is_prealloc = 0U;
        }
        const int outputs_result = rknn_outputs_get(context, counts.n_output, outputs, NULL);
        if (outputs_result != RKNN_SUCC) {
            status = PrintRknnError("get_outputs", outputs_result);
            goto cleanup;
        }

        const bool should_print = iteration == 1U || iteration == iterations;
        const bool summary_valid =
            !should_print || (PrintOutputSummary(iteration, outputs, counts.n_output) &&
                              PrintYolov5Detections(iteration, input_attributes, counts.n_input,
                                                    output_attributes, counts.n_output, outputs));
        const int release_result = rknn_outputs_release(context, counts.n_output, outputs);
        memset(outputs, 0, counts.n_output * sizeof(*outputs));
        if (release_result != RKNN_SUCC) {
            status = PrintRknnError("release_outputs", release_result);
            goto cleanup;
        }
        if (!summary_valid) {
            goto cleanup;
        }
    }

    rknn_perf_run performance = {0};
    const int performance_result =
        rknn_query(context, RKNN_QUERY_PERF_RUN, &performance, sizeof(performance));
    if (performance_result == RKNN_SUCC) {
        printf("performance last_inference_us=%" PRIu64 "\n", performance.run_duration);
    }
    printf("smoke_test status=passed iterations=%" PRIu32 "\n", iterations);
    status = 0;

cleanup:
    FreeInputBuffers(input_buffers, input_count);
    free(inputs);
    free(outputs);
    free(input_attributes);
    free(output_attributes);
    if (context != 0U) {
        rknn_destroy(context);
    }
    free(model.data);
    return status;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: rknn-smoke <model.rknn> [iterations] [input.rgb]\n");
        return 2;
    }
    uint32_t iterations = RKAV_DEFAULT_ITERATIONS;
    if (argc == 3 && !ParseIterations(argv[2], &iterations)) {
        fprintf(stderr, "iterations must be an integer in range [1, %d]\n",
                RKAV_MAXIMUM_ITERATIONS);
        return 2;
    }
    const char* input_path = argc == 4 ? argv[3] : NULL;
    return Run(argv[1], iterations, input_path);
}
