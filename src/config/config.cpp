// 文件作用：把 JSON 文本解析成强类型配置，并在启动线程前完成严格语义校验。
// 主要知识点：nlohmann/json、默认值、未知字段拒绝、异常转 Result、跨字段约束。
#include "rkav/config/config.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <utility>

namespace rkav {
namespace {

using Json = nlohmann::json;  // 缩短本文件内 JSON 类型名称。

/// 功能：构造 config 模块错误；path 放在 operation 字段中以精确定位配置项。
Error ConfigError(std::string path, std::string message) {
    return Error{
        ErrorCategory::kInvalidConfig, 0, "config", std::move(path), std::move(message), false};
}

/// 功能：确认 json 是对象，并拒绝 known 集合之外的任何字段。
Result<void> RejectUnknownKeys(const Json& object, std::string_view path,
                               const std::set<std::string>& known) {
    if (!object.is_object()) {
        return Result<void>::Failure(ConfigError(std::string(path), "expected a JSON object"));
    }
    // 未知字段通常是拼写错误，不能静默回退到默认值。
    for (const auto& [key, unused] : object.items()) {
        static_cast<void>(unused);
        if (!known.contains(key)) {
            const std::string field_path = std::string(path) + '.' + key;
            return Result<void>::Failure(
                ConfigError(field_path, "unknown configuration field: " + field_path));
        }
    }
    return Result<void>::Success();
}

/// 功能：把配置文本转换为有界队列溢出策略枚举。
Result<OverflowPolicy> ParseOverflow(const std::string& value, std::string_view path) {
    if (value == "block_producer") {
        return Result<OverflowPolicy>::Success(OverflowPolicy::kBlockProducer);
    }
    if (value == "drop_newest") {
        return Result<OverflowPolicy>::Success(OverflowPolicy::kDropNewest);
    }
    if (value == "drop_oldest") {
        return Result<OverflowPolicy>::Success(OverflowPolicy::kDropOldest);
    }
    if (value == "keep_latest") {
        return Result<OverflowPolicy>::Success(OverflowPolicy::kKeepLatest);
    }
    return Result<OverflowPolicy>::Failure(
        ConfigError(std::string(path), "unsupported overflow policy: " + value));
}

/// 功能：把视频像素格式文本转换为枚举。
Result<PixelFormat> ParsePixelFormat(const std::string& value) {
    if (value == "RGB888") {
        return Result<PixelFormat>::Success(PixelFormat::kRgb888);
    }
    if (value == "BGR888") {
        return Result<PixelFormat>::Success(PixelFormat::kBgr888);
    }
    if (value == "NV12") {
        return Result<PixelFormat>::Success(PixelFormat::kNv12);
    }
    if (value == "YUYV" || value == "YUYV422") {
        return Result<PixelFormat>::Success(PixelFormat::kYuyv422);
    }
    if (value == "MJPEG") {
        return Result<PixelFormat>::Success(PixelFormat::kMjpeg);
    }
    return Result<PixelFormat>::Failure(
        ConfigError("video.format", "unsupported pixel format: " + value));
}

/// 功能：把音频样本格式文本转换为枚举。
Result<SampleFormat> ParseSampleFormat(const std::string& value) {
    if (value == "S16_LE") {
        return Result<SampleFormat>::Success(SampleFormat::kS16LE);
    }
    if (value == "F32_LE") {
        return Result<SampleFormat>::Success(SampleFormat::kF32LE);
    }
    return Result<SampleFormat>::Failure(
        ConfigError("audio.format", "unsupported sample format: " + value));
}

template <typename T>
/// 功能：读取可选数值；字段缺失或显式 null 都表示未设置。
std::optional<T> OptionalNumber(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);  // 目标字段在 JSON 对象中的位置。
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    return iterator->get<T>();
}

/// 功能：解析 runtime 子对象；未出现字段继续保留结构体默认值。
Result<void> ParseRuntime(const Json& json, RuntimeConfig& config) {
    auto keys =
        RejectUnknownKeys(json, "runtime",  // 当前子对象的字段检查结果。
                          {"mode", "log_level", "shutdown_timeout_ms", "run_duration_seconds"});
    if (!keys) {
        return keys;
    }
    config.mode = json.value("mode", config.mode);
    config.log_level = json.value("log_level", config.log_level);
    config.shutdown_timeout_ms = json.value("shutdown_timeout_ms", config.shutdown_timeout_ms);
    config.run_duration_seconds = json.value("run_duration_seconds", config.run_duration_seconds);
    return Result<void>::Success();
}

/// 功能：解析视频参数、溢出策略和视频故障注入配置。
Result<void> ParseVideo(const Json& json, VideoConfig& config) {
    auto keys = RejectUnknownKeys(json, "video",
                                  {"backend", "device", "width", "height", "fps", "format",
                                   "capture_timeout_ms", "mmap_buffer_count", "queue_capacity",
                                   "overflow_policy", "pattern", "realtime", "failure"});
    if (!keys) {
        return keys;
    }
    config.backend = json.value("backend", config.backend);
    config.device = json.value("device", config.device);
    config.width = json.value("width", config.width);
    config.height = json.value("height", config.height);
    config.fps = json.value("fps", config.fps);
    config.capture_timeout_ms = json.value("capture_timeout_ms", config.capture_timeout_ms);
    config.mmap_buffer_count = json.value("mmap_buffer_count", config.mmap_buffer_count);
    config.queue_capacity = json.value("queue_capacity", config.queue_capacity);
    config.pattern = json.value("pattern", config.pattern);
    config.realtime = json.value("realtime", config.realtime);
    if (json.contains("format")) {
        auto format = ParsePixelFormat(json.at("format").get<std::string>());
        if (!format) {
            return Result<void>::Failure(format.error());
        }
        config.format = format.value();
    }
    if (json.contains("overflow_policy")) {
        auto policy =
            ParseOverflow(json.at("overflow_policy").get<std::string>(), "video.overflow_policy");
        if (!policy) {
            return Result<void>::Failure(policy.error());
        }
        config.overflow_policy = policy.value();
    }
    if (json.contains("failure")) {
        const auto& failure = json.at("failure");
        keys = RejectUnknownKeys(failure, "video.failure",
                                 {"fail_after_frames", "fail_for_frames", "read_delay_ms"});
        if (!keys) {
            return keys;
        }
        config.failure.fail_after_frames =
            OptionalNumber<std::uint64_t>(failure, "fail_after_frames");
        config.failure.fail_for_frames =
            failure.value("fail_for_frames", config.failure.fail_for_frames);
        config.failure.read_delay_ms = failure.value("read_delay_ms", config.failure.read_delay_ms);
    }
    return Result<void>::Success();
}

/// 功能：解析 PCM 参数、信号发生器和音频故障注入配置。
Result<void> ParseAudio(const Json& json, AudioConfig& config) {
    auto keys =
        RejectUnknownKeys(json, "audio",
                          {"backend", "sample_rate", "channels", "format", "frame_duration_ms",
                           "queue_capacity_ms", "device", "capture_timeout_ms", "signal",
                           "frequency_hz", "amplitude", "realtime", "failure"});
    if (!keys) {
        return keys;
    }
    config.backend = json.value("backend", config.backend);
    config.device = json.value("device", config.device);
    config.sample_rate = json.value("sample_rate", config.sample_rate);
    config.channels = json.value("channels", config.channels);
    config.frame_duration_ms = json.value("frame_duration_ms", config.frame_duration_ms);
    config.capture_timeout_ms = json.value("capture_timeout_ms", config.capture_timeout_ms);
    config.queue_capacity_ms = json.value("queue_capacity_ms", config.queue_capacity_ms);
    config.signal = json.value("signal", config.signal);
    config.frequency_hz = json.value("frequency_hz", config.frequency_hz);
    config.amplitude = json.value("amplitude", config.amplitude);
    config.realtime = json.value("realtime", config.realtime);
    if (json.contains("format")) {
        auto format = ParseSampleFormat(json.at("format").get<std::string>());
        if (!format) {
            return Result<void>::Failure(format.error());
        }
        config.format = format.value();
    }
    if (json.contains("failure")) {
        const auto& failure = json.at("failure");
        keys = RejectUnknownKeys(failure, "audio.failure",
                                 {"xrun_every_blocks", "disconnect_after_blocks"});
        if (!keys) {
            return keys;
        }
        config.failure.xrun_every_blocks =
            OptionalNumber<std::uint64_t>(failure, "xrun_every_blocks");
        config.failure.disconnect_after_blocks =
            OptionalNumber<std::uint64_t>(failure, "disconnect_after_blocks");
    }
    return Result<void>::Success();
}

/// 功能：解析 Mock 推理尺寸、耗时、队列和结果有效期。
Result<void> ParseInference(const Json& json, InferenceConfig& config) {
    auto keys = RejectUnknownKeys(
        json, "inference",
        {"backend", "mode", "model_path", "input_width", "input_height", "latency_ms", "max_fps",
         "object_threshold", "nms_threshold", "max_detections", "queue_capacity", "overflow_policy",
         "max_result_age_ms"});
    if (!keys) {
        return keys;
    }
    config.backend = json.value("backend", config.backend);
    config.mode = json.value("mode", config.mode);
    config.model_path = json.value("model_path", config.model_path);
    config.input_width = json.value("input_width", config.input_width);
    config.input_height = json.value("input_height", config.input_height);
    config.latency_ms = json.value("latency_ms", config.latency_ms);
    config.max_fps = json.value("max_fps", config.max_fps);
    config.object_threshold = json.value("object_threshold", config.object_threshold);
    config.nms_threshold = json.value("nms_threshold", config.nms_threshold);
    config.max_detections = json.value("max_detections", config.max_detections);
    config.queue_capacity = json.value("queue_capacity", config.queue_capacity);
    config.max_result_age_ms = json.value("max_result_age_ms", config.max_result_age_ms);
    if (json.contains("overflow_policy")) {
        auto policy = ParseOverflow(json.at("overflow_policy").get<std::string>(),
                                    "inference.overflow_policy");
        if (!policy) {
            return Result<void>::Failure(policy.error());
        }
        config.overflow_policy = policy.value();
    }
    return Result<void>::Success();
}

/// 功能：解析 OSD 开关、实现后端和有界等待参数。
Result<void> ParseOverlay(const Json& json, OverlayConfig& config) {
    auto keys = RejectUnknownKeys(json, "overlay",
                                  {"enabled", "backend", "line_width", "draw_labels",
                                   "wait_for_result_ms"});
    if (!keys) {
        return keys;
    }
    config.enabled = json.value("enabled", config.enabled);
    config.backend = json.value("backend", config.backend);
    config.line_width = json.value("line_width", config.line_width);
    config.draw_labels = json.value("draw_labels", config.draw_labels);
    config.wait_for_result_ms = json.value("wait_for_result_ms", config.wait_for_result_ms);
    return Result<void>::Success();
}

/// 功能：解析输出数组；每个数组元素独立校验并追加到 outputs。
Result<void> ParseOutputs(const Json& json, std::vector<OutputConfig>& outputs) {
    if (!json.is_array()) {
        return Result<void>::Failure(ConfigError("outputs", "expected a JSON array"));
    }
    outputs.clear();
    for (std::size_t index = 0; index < json.size(); ++index) {
        const auto& item = json.at(index);  // 当前输出的 JSON 对象。
        const std::string path =
            "outputs[" + std::to_string(index) + ']';  // 用于错误定位的数组路径。
        auto keys = RejectUnknownKeys(
            item, path,
            {"type", "enabled", "required", "validate_timestamps", "path", "queue_capacity",
             "overflow_policy", "push_timeout_ms", "write_delay_ms", "fail_after_packets"});
        if (!keys) {
            return keys;
        }
        OutputConfig output;  // 从默认值开始覆盖当前输出字段。
        output.type = item.value("type", output.type);
        output.enabled = item.value("enabled", output.enabled);
        output.required = item.value("required", output.required);
        output.validate_timestamps = item.value("validate_timestamps", output.validate_timestamps);
        output.path = item.value("path", output.path);
        output.queue_capacity = item.value("queue_capacity", output.queue_capacity);
        output.push_timeout_ms = item.value("push_timeout_ms", output.push_timeout_ms);
        output.write_delay_ms = item.value("write_delay_ms", output.write_delay_ms);
        output.fail_after_packets = OptionalNumber<std::uint64_t>(item, "fail_after_packets");
        if (item.contains("overflow_policy")) {
            auto policy = ParseOverflow(item.at("overflow_policy").get<std::string>(),
                                        path + ".overflow_policy");
            if (!policy) {
                return Result<void>::Failure(policy.error());
            }
            output.overflow_policy = policy.value();
        }
        outputs.push_back(std::move(output));
    }
    return Result<void>::Success();
}

/// 功能：解析指标和 worker 健康检查周期。
Result<void> ParseMonitoring(const Json& json, MonitoringConfig& config) {
    auto keys =
        RejectUnknownKeys(json, "monitoring",
                          {"metrics_interval_ms", "health_interval_ms", "worker_stall_timeout_ms"});
    if (!keys) {
        return keys;
    }
    config.metrics_interval_ms = json.value("metrics_interval_ms", config.metrics_interval_ms);
    config.health_interval_ms = json.value("health_interval_ms", config.health_interval_ms);
    config.worker_stall_timeout_ms =
        json.value("worker_stall_timeout_ms", config.worker_stall_timeout_ms);
    return Result<void>::Success();
}

}  // namespace

/// 功能：以二进制方式完整读取配置文件，再交给统一 Parse 流程。
Result<AppConfig> ConfigLoader::LoadFromFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);  // 配置文件输入流。
    if (!input) {
        return Result<AppConfig>::Failure(
            ConfigError("file", "cannot open configuration file: " + path));
    }
    std::ostringstream content;  // 保存完整 JSON 文本。
    content << input.rdbuf();
    return Parse(content.str());
}

/// 功能：解析 JSON 结构、填充默认值，并调用 Validate 完成最终语义检查。
Result<AppConfig> ConfigLoader::Parse(std::string_view json_text) {
    try {
        const Json root = Json::parse(json_text);  // 顶层 JSON 对象。
        auto keys = RejectUnknownKeys(root, "config",
                                      {"schema_version", "runtime", "video", "audio", "inference",
                                       "overlay", "video_encoder", "audio_encoder", "outputs",
                                       "monitoring"});
        if (!keys) {
            return Result<AppConfig>::Failure(keys.error());
        }

        AppConfig config;  // 先采用默认值，再覆盖 JSON 明确给出的字段。
        config.schema_version = root.value("schema_version", config.schema_version);
        if (root.contains("runtime")) {
            auto result = ParseRuntime(root.at("runtime"), config.runtime);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        }
        if (root.contains("video")) {
            auto result = ParseVideo(root.at("video"), config.video);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        }
        if (root.contains("audio")) {
            auto result = ParseAudio(root.at("audio"), config.audio);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        }
        if (root.contains("inference")) {
            auto result = ParseInference(root.at("inference"), config.inference);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        }
        if (root.contains("overlay")) {
            auto result = ParseOverlay(root.at("overlay"), config.overlay);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        }
        if (root.contains("video_encoder")) {
            const auto& encoder = root.at("video_encoder");
            keys = RejectUnknownKeys(
                encoder, "video_encoder",
                {"backend", "mock_keyframe_interval", "codec_name", "bitrate_bps", "gop_size",
                 "preset"});
            if (!keys) {
                return Result<AppConfig>::Failure(keys.error());
            }
            config.video_encoder.backend = encoder.value("backend", config.video_encoder.backend);
            config.video_encoder.mock_keyframe_interval = encoder.value(
                "mock_keyframe_interval", config.video_encoder.mock_keyframe_interval);
            config.video_encoder.codec_name =
                encoder.value("codec_name", config.video_encoder.codec_name);
            config.video_encoder.bitrate_bps =
                encoder.value("bitrate_bps", config.video_encoder.bitrate_bps);
            config.video_encoder.gop_size =
                encoder.value("gop_size", config.video_encoder.gop_size);
            config.video_encoder.preset = encoder.value("preset", config.video_encoder.preset);
        }
        if (root.contains("audio_encoder")) {
            const auto& encoder = root.at("audio_encoder");
            keys = RejectUnknownKeys(encoder, "audio_encoder",
                                     {"backend", "codec_name", "bitrate_bps"});
            if (!keys) {
                return Result<AppConfig>::Failure(keys.error());
            }
            config.audio_encoder.backend = encoder.value("backend", config.audio_encoder.backend);
            config.audio_encoder.codec_name =
                encoder.value("codec_name", config.audio_encoder.codec_name);
            config.audio_encoder.bitrate_bps =
                encoder.value("bitrate_bps", config.audio_encoder.bitrate_bps);
        }
        if (root.contains("outputs")) {
            auto result = ParseOutputs(root.at("outputs"), config.outputs);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        } else {
            config.outputs.push_back(OutputConfig{});
        }
        if (root.contains("monitoring")) {
            auto result = ParseMonitoring(root.at("monitoring"), config.monitoring);
            if (!result) {
                return Result<AppConfig>::Failure(result.error());
            }
        }

        auto validation = Validate(config);  // 第二层：范围和跨字段语义校验。
        if (!validation) {
            return Result<AppConfig>::Failure(validation.error());
        }
        return Result<AppConfig>::Success(std::move(config));
    } catch (const Json::exception& exception) {
        return Result<AppConfig>::Failure(ConfigError("json", exception.what()));
    } catch (const std::exception& exception) {
        return Result<AppConfig>::Failure(ConfigError("parse", exception.what()));
    }
}

/// 功能：检查字段范围、后端支持情况，以及多个配置项之间是否相互矛盾。
Result<void> ConfigLoader::Validate(const AppConfig& config) {
    const auto fail = [](std::string path, std::string message) {
        return Result<void>::Failure(ConfigError(std::move(path), std::move(message)));
    };
    if (config.schema_version != 1) {
        return fail("schema_version", "only schema version 1 is supported");
    }
    if (config.runtime.mode != "mock" && config.runtime.mode != "hardware") {
        return fail("runtime.mode", "expected 'mock' or 'hardware'");
    }
    const std::set<std::string> supported_log_levels{"trace", "debug", "info",
                                                     "warn",  "error", "fatal"};
    if (!supported_log_levels.contains(config.runtime.log_level)) {
        return fail("runtime.log_level", "expected one of: trace, debug, info, warn, error, fatal");
    }
    if (config.runtime.shutdown_timeout_ms < 100 || config.runtime.shutdown_timeout_ms > 60000) {
        return fail("runtime.shutdown_timeout_ms", "expected value in range [100, 60000]");
    }
    if (config.runtime.run_duration_seconds < 0) {
        return fail("runtime.run_duration_seconds", "must not be negative");
    }
    if (config.video.width < 16 || config.video.width > 7680 || config.video.height < 16 ||
        config.video.height > 4320) {
        return fail("video", "width/height are outside the supported range");
    }
    if (config.video.fps < 1 || config.video.fps > 120) {
        return fail("video.fps", "expected integer in range [1, 120]");
    }
    if (config.video.backend == "mock") {
#if RKAV_ENABLE_MOCK
        if (config.video.format != PixelFormat::kRgb888) {
            return fail("video.format", "mock video currently produces RGB888 only");
        }
#else
        return fail("video.backend", "mock video backend is not compiled in");
#endif
    } else if (config.video.backend == "v4l2") {
#if RKAV_WITH_V4L2
        if (config.video.device.empty()) {
            return fail("video.device", "V4L2 device path must not be empty");
        }
        if (config.video.format != PixelFormat::kMjpeg &&
            config.video.format != PixelFormat::kYuyv422) {
            return fail("video.format", "V4L2 phase 1 supports MJPEG or YUYV only");
        }
#else
        return fail("video.backend", "V4L2 backend is not compiled in");
#endif
    } else {
        return fail("video.backend", "expected 'mock' or 'v4l2'");
    }
    if (config.video.capture_timeout_ms < 100 || config.video.capture_timeout_ms > 60'000) {
        return fail("video.capture_timeout_ms", "expected value in range [100, 60000]");
    }
    if (config.video.mmap_buffer_count < 2U || config.video.mmap_buffer_count > 32U) {
        return fail("video.mmap_buffer_count", "expected value in range [2, 32]");
    }
    if (config.video.queue_capacity == 0U || config.video.queue_capacity > 4096U) {
        return fail("video.queue_capacity", "expected value in range [1, 4096]");
    }
    if (config.video.failure.read_delay_ms < 0) {
        return fail("video.failure.read_delay_ms", "must not be negative");
    }
    if (config.audio.backend == "mock") {
#if !RKAV_ENABLE_MOCK
        return fail("audio.backend", "mock audio backend is not compiled in");
#endif
    } else if (config.audio.backend == "alsa") {
#if RKAV_WITH_ALSA
        if (config.audio.device.empty()) {
            return fail("audio.device", "ALSA device must not be empty");
        }
#else
        return fail("audio.backend", "ALSA backend is not compiled in");
#endif
    } else {
        return fail("audio.backend", "expected 'mock' or 'alsa'");
    }
    if (config.audio.sample_rate < 8000 || config.audio.sample_rate > 192000) {
        return fail("audio.sample_rate", "expected value in range [8000, 192000]");
    }
    if (config.audio.channels < 1 || config.audio.channels > 8) {
        return fail("audio.channels", "expected value in range [1, 8]");
    }
    if (config.audio.format != SampleFormat::kS16LE) {
        return fail("audio.format", "audio backends currently support S16_LE only");
    }
    if (config.audio.frame_duration_ms <= 0 || config.audio.frame_duration_ms > 1000 ||
        (static_cast<std::int64_t>(config.audio.sample_rate) * config.audio.frame_duration_ms) %
                1000 !=
            0) {
        return fail("audio.frame_duration_ms",
                    "expected at most 1000 ms and an integer number of samples");
    }
    if (config.audio.queue_capacity_ms < config.audio.frame_duration_ms ||
        config.audio.queue_capacity_ms > 60'000) {
        return fail("audio.queue_capacity_ms",
                    "must hold at least one frame and not exceed 60000 ms");
    }
    if (config.audio.capture_timeout_ms < 100 || config.audio.capture_timeout_ms > 60'000) {
        return fail("audio.capture_timeout_ms", "expected value in range [100, 60000]");
    }
    if (config.audio.signal != "silence" && config.audio.signal != "sine") {
        return fail("audio.signal", "expected 'silence' or 'sine'");
    }
    if (config.audio.amplitude < 0.0 || config.audio.amplitude > 1.0) {
        return fail("audio.amplitude", "expected value in range [0.0, 1.0]");
    }
    if (config.audio.frequency_hz <= 0.0 ||
        config.audio.frequency_hz >= static_cast<double>(config.audio.sample_rate) / 2.0) {
        return fail("audio.frequency_hz", "must be positive and below the Nyquist frequency");
    }
    if (config.audio.failure.xrun_every_blocks.has_value() &&
        *config.audio.failure.xrun_every_blocks == 0U) {
        return fail("audio.failure.xrun_every_blocks", "must be greater than zero when set");
    }
    if (config.inference.backend == "mock") {
#if !RKAV_ENABLE_MOCK
        return fail("inference.backend", "mock inference backend is not compiled in");
#endif
    } else if (config.inference.backend == "rknn") {
#if RKAV_WITH_RKNN
        if (config.inference.model_path.empty()) {
            return fail("inference.model_path", "RKNN backend requires a model path");
        }
        if (config.inference.mode != "yolov5") {
            return fail("inference.mode", "RKNN backend currently supports yolov5 only");
        }
        if (config.video.format == PixelFormat::kMjpeg) {
#if !RKAV_WITH_JPEG
            return fail("video.format", "MJPEG with RKNN requires the JPEG decoder feature");
#endif
        } else if (config.video.format != PixelFormat::kRgb888 &&
                   config.video.format != PixelFormat::kBgr888) {
            return fail("video.format", "RKNN currently requires RGB/BGR or decodable MJPEG");
        }
#else
        return fail("inference.backend", "RKNN inference backend is not compiled in");
#endif
    } else {
        return fail("inference.backend", "expected 'mock' or 'rknn'");
    }
    if (config.inference.input_width <= 0 || config.inference.input_height <= 0) {
        return fail("inference", "input dimensions must be positive");
    }
    if (config.inference.latency_ms < 0 || config.inference.max_fps < 0 ||
        config.inference.max_fps > 120 || config.inference.queue_capacity == 0U ||
        config.inference.queue_capacity > 4096U || config.inference.max_result_age_ms < 0) {
        return fail("inference",
                    "latency/age/fps must be in range and queue capacity must be in [1, 4096]");
    }
    if (config.inference.object_threshold <= 0.0F || config.inference.object_threshold >= 1.0F ||
        config.inference.nms_threshold <= 0.0F || config.inference.nms_threshold >= 1.0F) {
        return fail("inference", "object and NMS thresholds must be in range (0, 1)");
    }
    if (config.inference.max_detections == 0U || config.inference.max_detections > 4096U) {
        return fail("inference.max_detections", "expected value in range [1, 4096]");
    }
    if (config.overlay.backend != "cpu") {
        return fail("overlay.backend", "expected 'cpu'");
    }
    if (config.overlay.line_width < 1 || config.overlay.line_width > 8) {
        return fail("overlay.line_width", "expected value in range [1, 8]");
    }
    if (config.overlay.wait_for_result_ms < 0 ||
        config.overlay.wait_for_result_ms > config.inference.max_result_age_ms) {
        return fail("overlay.wait_for_result_ms",
                    "must be non-negative and not exceed inference.max_result_age_ms");
    }
    if (config.overlay.enabled && config.video.format != PixelFormat::kRgb888 &&
        config.video.format != PixelFormat::kBgr888 && config.video.format != PixelFormat::kMjpeg) {
        return fail("overlay", "CPU OSD requires RGB/BGR or decodable MJPEG video");
    }
    if (config.overlay.enabled && config.video.format == PixelFormat::kMjpeg) {
#if !RKAV_WITH_JPEG
        return fail("overlay", "MJPEG OSD requires the JPEG decoder feature");
#endif
    }
    if (config.video_encoder.backend == "checksum") {
        if (config.video_encoder.mock_keyframe_interval <= 0) {
            return fail("video_encoder.mock_keyframe_interval", "must be positive");
        }
    } else if (config.video_encoder.backend == "ffmpeg") {
#if RKAV_WITH_FFMPEG
        if (config.video_encoder.codec_name.empty() || config.video_encoder.bitrate_bps <= 0 ||
            config.video_encoder.gop_size <= 0 || config.video_encoder.preset.empty()) {
            return fail("video_encoder",
                        "FFmpeg codec, bitrate, GOP and preset must be configured");
        }
        if (config.video.format == PixelFormat::kMjpeg) {
#if !RKAV_WITH_JPEG
            return fail("video.format", "FFmpeg H.264 from MJPEG requires the JPEG decoder feature");
#endif
        } else if (config.video.format != PixelFormat::kRgb888 &&
                   config.video.format != PixelFormat::kBgr888) {
            return fail("video.format", "FFmpeg software baseline requires RGB/BGR or decodable MJPEG");
        }
#else
        return fail("video_encoder.backend", "FFmpeg video encoder is not compiled in");
#endif
    } else if (config.video_encoder.backend == "mpp") {
#if RKAV_WITH_MPP && RKAV_WITH_RGA
        if (config.video_encoder.bitrate_bps <= 0 || config.video_encoder.gop_size <= 0) {
            return fail("video_encoder", "MPP H.264 bitrate and GOP must be positive");
        }
        if (config.video_encoder.codec_name != "h264") {
            return fail("video_encoder.codec_name", "MPP backend requires codec_name 'h264'");
        }
        if (config.video.format == PixelFormat::kMjpeg) {
#if !RKAV_WITH_JPEG
            return fail("video.format", "MPP H.264 from MJPEG requires the JPEG decoder feature");
#endif
        } else if (config.video.format != PixelFormat::kRgb888 &&
                   config.video.format != PixelFormat::kBgr888) {
            return fail("video.format", "MPP H.264 requires RGB/BGR or decodable MJPEG video");
        }
#else
        return fail("video_encoder.backend", "MPP/RGA video encoder is not compiled in");
#endif
    } else {
        return fail("video_encoder.backend", "expected 'checksum', 'ffmpeg' or 'mpp'");
    }
    if (config.audio_encoder.backend == "ffmpeg") {
#if RKAV_WITH_FFMPEG
        if (config.audio_encoder.codec_name.empty() || config.audio_encoder.bitrate_bps <= 0) {
            return fail("audio_encoder", "FFmpeg codec and positive bitrate are required");
        }
#else
        return fail("audio_encoder.backend", "FFmpeg audio encoder is not compiled in");
#endif
    } else if (config.audio_encoder.backend != "checksum") {
        return fail("audio_encoder.backend", "expected 'checksum' or 'ffmpeg'");
    }
    bool has_enabled_output = false;  // 最终必须至少存在一个启用的输出。
    for (std::size_t index = 0; index < config.outputs.size(); ++index) {
        const auto& output = config.outputs[index];  // 当前待校验输出配置。
        if (!output.enabled) {
            continue;
        }
        has_enabled_output = true;
        if (output.type != "null" && output.type != "jsonl" && output.type != "mp4" &&
            output.type != "h264") {
            return fail("outputs[" + std::to_string(index) + "].type",
                        "expected 'null', 'jsonl', 'h264' or 'mp4'");
        }
        if (output.type == "h264") {
            if (output.path.empty() || !std::string_view(output.path).ends_with(".h264")) {
                return fail("outputs[" + std::to_string(index) + "].path",
                            "H.264 elementary output path must end with .h264");
            }
            if (config.video_encoder.backend != "mpp") {
                return fail("outputs[" + std::to_string(index) + "].type",
                            "H.264 elementary output requires the MPP video encoder");
            }
        }
        if (output.type == "mp4") {
#if RKAV_WITH_FFMPEG
            if (output.path.empty()) {
                return fail("outputs[" + std::to_string(index) + "].path",
                            "MP4 output path must not be empty");
            }
            if (config.video_encoder.backend != "ffmpeg" ||
                config.audio_encoder.backend != "ffmpeg") {
                return fail("outputs[" + std::to_string(index) + "].type",
                            "MP4 requires real FFmpeg H.264 and AAC encoders");
            }
            if (output.overflow_policy != OverflowPolicy::kBlockProducer ||
                output.push_timeout_ms <= 0) {
                return fail("outputs[" + std::to_string(index) + "]",
                            "MP4 requires block_producer and a positive push timeout");
            }
#else
            return fail("outputs[" + std::to_string(index) + "].type",
                        "MP4 output is not compiled in");
#endif
        }
        if (output.queue_capacity == 0U || output.queue_capacity > 65'536U) {
            return fail("outputs[" + std::to_string(index) + "].queue_capacity",
                        "expected value in range [1, 65536]");
        }
        if (output.write_delay_ms < 0) {
            return fail("outputs[" + std::to_string(index) + "].write_delay_ms",
                        "must not be negative");
        }
        if (output.push_timeout_ms < 0 ||
            output.push_timeout_ms >= config.runtime.shutdown_timeout_ms) {
            return fail("outputs[" + std::to_string(index) + "].push_timeout_ms",
                        "must be non-negative and shorter than shutdown timeout");
        }
        if (output.write_delay_ms >= config.runtime.shutdown_timeout_ms) {
            return fail("outputs[" + std::to_string(index) + "].write_delay_ms",
                        "must be shorter than runtime.shutdown_timeout_ms");
        }
        if (output.type == "jsonl" && output.path.empty()) {
            return fail("outputs[" + std::to_string(index) + "].path",
                        "JSONL output requires a path");
        }
    }
    if (!has_enabled_output) {
        return fail("outputs", "at least one output must be enabled");
    }
    if (config.monitoring.metrics_interval_ms <= 0 || config.monitoring.health_interval_ms <= 0 ||
        config.monitoring.worker_stall_timeout_ms <= config.monitoring.health_interval_ms) {
        return fail("monitoring",
                    "intervals must be positive and stall timeout must exceed health interval");
    }
    // 健康超时要覆盖最慢阶段和一次检查周期，否则正常慢处理会被误判为卡死。
    const int video_progress_interval_ms =
        (1000 + config.video.fps - 1) / config.video.fps +
        config.video.failure.read_delay_ms;  // 最慢情况下两次视频进展的间隔。
    const int slowest_stage_ms = std::max(
        {video_progress_interval_ms, config.audio.frame_duration_ms, config.inference.latency_ms});
    if (config.monitoring.worker_stall_timeout_ms <=
        slowest_stage_ms + config.monitoring.health_interval_ms) {
        return fail("monitoring.worker_stall_timeout_ms",
                    "must exceed the slowest configured stage plus one health interval");
    }
    return Result<void>::Success();
}

/// 功能：生成精简、无敏感路径的配置摘要，用于启动日志和 --validate-config。
std::string ConfigSummary(const AppConfig& config) {
    Json summary{
        // 只列出判断本次运行形态所需的核心字段。
        {"schema_version", config.schema_version},
        {"runtime", {{"mode", config.runtime.mode}, {"log_level", config.runtime.log_level}}},
        {"video",
         {{"backend", config.video.backend},
          {"device", config.video.device},
          {"width", config.video.width},
          {"height", config.video.height},
          {"fps", config.video.fps},
          {"format", ToString(config.video.format)},
          {"queue_capacity", config.video.queue_capacity},
          {"overflow_policy", ToString(config.video.overflow_policy)}}},
        {"audio",
         {{"backend", config.audio.backend},
          {"device", config.audio.device},
          {"sample_rate", config.audio.sample_rate},
          {"channels", config.audio.channels},
          {"format", ToString(config.audio.format)},
          {"frame_duration_ms", config.audio.frame_duration_ms}}},
        {"inference",
         {{"backend", config.inference.backend},
          {"latency_ms", config.inference.latency_ms},
          {"max_fps", config.inference.max_fps},
          {"queue_capacity", config.inference.queue_capacity},
          {"overflow_policy", ToString(config.inference.overflow_policy)}}},
        {"overlay",
         {{"enabled", config.overlay.enabled},
          {"backend", config.overlay.backend},
          {"line_width", config.overlay.line_width},
          {"draw_labels", config.overlay.draw_labels},
          {"wait_for_result_ms", config.overlay.wait_for_result_ms}}},
        {"video_encoder",
         {{"backend", config.video_encoder.backend},
          {"codec_name", config.video_encoder.codec_name},
          {"bitrate_bps", config.video_encoder.bitrate_bps},
          {"gop_size", config.video_encoder.gop_size}}},
        {"audio_encoder",
         {{"backend", config.audio_encoder.backend},
          {"codec_name", config.audio_encoder.codec_name},
          {"bitrate_bps", config.audio_encoder.bitrate_bps}}}};
    return summary.dump();
}

}  // namespace rkav
