// File purpose: validate the RK3568 RKNN runtime, model metadata, inference, and output access.
#include <rknn_api.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultIterations = 1U;
constexpr std::uint32_t kMaximumIterations = 100'000U;
constexpr std::uint32_t kMaximumTensorCount = 256U;

class RknnContext final {
   public:
    RknnContext() = default;
    ~RknnContext() {
        if (value_ != 0U) {
            static_cast<void>(rknn_destroy(value_));
        }
    }

    RknnContext(const RknnContext&) = delete;
    RknnContext& operator=(const RknnContext&) = delete;

    [[nodiscard]] rknn_context value() const noexcept { return value_; }
    [[nodiscard]] rknn_context* address() noexcept { return &value_; }

   private:
    rknn_context value_{0U};
};

int PrintRknnError(std::string_view operation, int code) {
    std::cerr << "error operation=" << operation << " rknn_code=" << code << '\n';
    return 1;
}

bool ParseIterations(std::string_view text, std::uint32_t& value) {
    std::uint32_t parsed = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed == 0U ||
        parsed > kMaximumIterations) {
        return false;
    }
    value = parsed;
    return true;
}

bool ReadModel(const std::string& path, std::vector<std::uint8_t>& model) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        std::cerr << "error operation=open_model path=" << path << '\n';
        return false;
    }
    const std::streampos end = stream.tellg();
    if (end <= 0 || end > static_cast<std::streampos>(std::numeric_limits<std::uint32_t>::max())) {
        std::cerr << "error operation=validate_model_size path=" << path << '\n';
        return false;
    }
    const auto size = static_cast<std::size_t>(end);
    model.resize(size);
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(model.data()), static_cast<std::streamsize>(model.size()));
    if (!stream) {
        std::cerr << "error operation=read_model path=" << path << '\n';
        return false;
    }
    return true;
}

std::string Dimensions(const rknn_tensor_attr& attribute) {
    std::string result;
    for (std::uint32_t index = 0U; index < attribute.n_dims; ++index) {
        if (!result.empty()) {
            result += 'x';
        }
        result += std::to_string(attribute.dims[index]);
    }
    return result;
}

void PrintTensor(std::string_view direction, const rknn_tensor_attr& attribute) {
    std::cout << "tensor direction=" << direction << " index=" << attribute.index
              << " name=" << attribute.name << " dims=" << Dimensions(attribute)
              << " elements=" << attribute.n_elems << " bytes=" << attribute.size
              << " stride_bytes=" << attribute.size_with_stride
              << " format=" << get_format_string(attribute.fmt)
              << " type=" << get_type_string(attribute.type)
              << " quantization=" << get_qnt_type_string(attribute.qnt_type)
              << " zero_point=" << attribute.zp << " scale=" << attribute.scale << '\n';
}

bool QueryTensors(rknn_context context, rknn_query_cmd command, std::uint32_t count,
                  std::string_view direction, std::vector<rknn_tensor_attr>& attributes) {
    attributes.resize(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        attributes[index] = {};
        attributes[index].index = index;
        const int result =
            rknn_query(context, command, &attributes[index], sizeof(attributes[index]));
        if (result != RKNN_SUCC) {
            static_cast<void>(PrintRknnError("query_tensor", result));
            return false;
        }
        if (attributes[index].n_dims > RKNN_MAX_DIMS) {
            std::cerr << "error operation=validate_tensor_dimensions direction=" << direction
                      << " index=" << index << " dimensions=" << attributes[index].n_dims << '\n';
            return false;
        }
        PrintTensor(direction, attributes[index]);
    }
    return true;
}

std::uint64_t HashBytes(const void* data, std::size_t size) {
    constexpr std::uint64_t kOffsetBasis = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t kPrime = 1'099'511'628'211ULL;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = kOffsetBasis;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kPrime;
    }
    return hash;
}

bool PrintOutputSummary(std::uint32_t iteration, const std::vector<rknn_output>& outputs) {
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        const auto& output = outputs[index];
        if (output.buf == nullptr || output.size == 0U || output.size % sizeof(float) != 0U) {
            std::cerr << "error operation=validate_output index=" << index
                      << " bytes=" << output.size << '\n';
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(output.size) / sizeof(float);
        const auto* values = static_cast<const float*>(output.buf);
        double sum = 0.0;
        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        std::size_t finite_count = 0U;
        for (std::size_t value_index = 0U; value_index < count; ++value_index) {
            const float value = values[value_index];
            if (std::isfinite(value)) {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
                sum += static_cast<double>(value);
                ++finite_count;
            }
        }
        const double mean = finite_count == 0U ? 0.0 : sum / static_cast<double>(finite_count);
        std::cout << "output iteration=" << iteration << " index=" << index
                  << " bytes=" << output.size << " floats=" << count << " finite=" << finite_count
                  << " min=" << minimum << " max=" << maximum << " mean=" << mean << " fnv1a64=0x"
                  << std::hex << HashBytes(output.buf, output.size) << std::dec << '\n';
    }
    return true;
}

int Run(const std::string& model_path, std::uint32_t iterations) {
    std::vector<std::uint8_t> model;
    if (!ReadModel(model_path, model)) {
        return 3;
    }
    std::cout << "model path=" << model_path << " bytes=" << model.size() << '\n';

    RknnContext context;
    const int init_result = rknn_init(context.address(), model.data(),
                                      static_cast<std::uint32_t>(model.size()), 0U, nullptr);
    if (init_result != RKNN_SUCC) {
        return PrintRknnError("init", init_result);
    }

    rknn_sdk_version version{};
    const int version_result =
        rknn_query(context.value(), RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    if (version_result != RKNN_SUCC) {
        return PrintRknnError("query_sdk_version", version_result);
    }
    std::cout << "runtime api_version=" << version.api_version
              << " driver_version=" << version.drv_version << '\n';

    rknn_input_output_num counts{};
    const int count_result =
        rknn_query(context.value(), RKNN_QUERY_IN_OUT_NUM, &counts, sizeof(counts));
    if (count_result != RKNN_SUCC) {
        return PrintRknnError("query_input_output_count", count_result);
    }
    if (counts.n_input == 0U || counts.n_input > kMaximumTensorCount || counts.n_output == 0U ||
        counts.n_output > kMaximumTensorCount) {
        std::cerr << "error operation=validate_input_output_count inputs=" << counts.n_input
                  << " outputs=" << counts.n_output << '\n';
        return 1;
    }
    std::cout << "model inputs=" << counts.n_input << " outputs=" << counts.n_output << '\n';

    std::vector<rknn_tensor_attr> input_attributes;
    std::vector<rknn_tensor_attr> output_attributes;
    if (!QueryTensors(context.value(), RKNN_QUERY_INPUT_ATTR, counts.n_input, "input",
                      input_attributes) ||
        !QueryTensors(context.value(), RKNN_QUERY_OUTPUT_ATTR, counts.n_output, "output",
                      output_attributes)) {
        return 1;
    }

    std::vector<std::vector<std::uint8_t>> input_buffers(counts.n_input);
    std::vector<rknn_input> inputs(counts.n_input);
    for (std::uint32_t index = 0U; index < counts.n_input; ++index) {
        const auto& attribute = input_attributes[index];
        if (attribute.size == 0U) {
            std::cerr << "error operation=validate_input_size index=" << index << '\n';
            return 1;
        }
        input_buffers[index].resize(attribute.size, 0U);
        inputs[index] = {};
        inputs[index].index = index;
        inputs[index].buf = input_buffers[index].data();
        inputs[index].size = attribute.size;
        inputs[index].pass_through = 0U;
        inputs[index].type = attribute.type;
        inputs[index].fmt = attribute.fmt;
    }

    std::cout << std::setprecision(8);
    for (std::uint32_t iteration = 1U; iteration <= iterations; ++iteration) {
        const int inputs_result = rknn_inputs_set(context.value(), counts.n_input, inputs.data());
        if (inputs_result != RKNN_SUCC) {
            return PrintRknnError("set_inputs", inputs_result);
        }
        const int run_result = rknn_run(context.value(), nullptr);
        if (run_result != RKNN_SUCC) {
            return PrintRknnError("run", run_result);
        }

        std::vector<rknn_output> outputs(counts.n_output);
        for (std::uint32_t index = 0U; index < counts.n_output; ++index) {
            outputs[index] = {};
            outputs[index].index = index;
            outputs[index].want_float = 1U;
            outputs[index].is_prealloc = 0U;
        }
        const int outputs_result =
            rknn_outputs_get(context.value(), counts.n_output, outputs.data(), nullptr);
        if (outputs_result != RKNN_SUCC) {
            return PrintRknnError("get_outputs", outputs_result);
        }

        const bool summary_valid =
            (iteration != 1U && iteration != iterations) || PrintOutputSummary(iteration, outputs);
        const int release_result =
            rknn_outputs_release(context.value(), counts.n_output, outputs.data());
        if (release_result != RKNN_SUCC) {
            return PrintRknnError("release_outputs", release_result);
        }
        if (!summary_valid) {
            return 1;
        }
    }

    rknn_perf_run performance{};
    const int performance_result =
        rknn_query(context.value(), RKNN_QUERY_PERF_RUN, &performance, sizeof(performance));
    if (performance_result == RKNN_SUCC) {
        std::cout << "performance last_inference_us=" << performance.run_duration << '\n';
    }
    std::cout << "smoke_test status=passed iterations=" << iterations << '\n';
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: rknn-smoke <model.rknn> [iterations]\n";
        return 2;
    }
    std::uint32_t iterations = kDefaultIterations;
    if (argc == 3 && !ParseIterations(argv[2], iterations)) {
        std::cerr << "iterations must be an integer in range [1, " << kMaximumIterations << "]\n";
        return 2;
    }
    return Run(argv[1], iterations);
}
