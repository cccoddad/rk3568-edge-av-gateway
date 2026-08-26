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

enum {
    RKAV_DEFAULT_ITERATIONS = 1,
    RKAV_MAXIMUM_ITERATIONS = 100000,
    RKAV_MAXIMUM_TENSOR_COUNT = 256,
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

static int Run(const char* model_path, uint32_t iterations) {
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

    for (uint32_t index = 0U; index < counts.n_input; ++index) {
        const rknn_tensor_attr* attribute = &input_attributes[index];
        if (attribute->size == 0U) {
            fprintf(stderr, "error operation=validate_input_size index=%" PRIu32 "\n", index);
            goto cleanup;
        }
        input_buffers[index] = calloc(attribute->size, 1U);
        if (input_buffers[index] == NULL) {
            fprintf(stderr, "error operation=allocate_input index=%" PRIu32 " bytes=%" PRIu32 "\n",
                    index, attribute->size);
            goto cleanup;
        }
        inputs[index].index = index;
        inputs[index].buf = input_buffers[index];
        inputs[index].size = attribute->size;
        inputs[index].pass_through = 0U;
        inputs[index].type = attribute->type;
        inputs[index].fmt = attribute->fmt;
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

        const bool summary_valid = (iteration != 1U && iteration != iterations) ||
                                   PrintOutputSummary(iteration, outputs, counts.n_output);
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
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: rknn-smoke <model.rknn> [iterations]\n");
        return 2;
    }
    uint32_t iterations = RKAV_DEFAULT_ITERATIONS;
    if (argc == 3 && !ParseIterations(argv[2], &iterations)) {
        fprintf(stderr, "iterations must be an integer in range [1, %d]\n",
                RKAV_MAXIMUM_ITERATIONS);
        return 2;
    }
    return Run(argv[1], iterations);
}
