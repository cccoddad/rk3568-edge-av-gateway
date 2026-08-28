// 文件作用：验证 JSON 默认值、未知字段拒绝、范围限制和跨字段语义约束。
// 主要知识点：配置负向测试、错误路径定位和“解析成功不等于配置可运行”。
#include "rkav/config/config.h"

#include <gtest/gtest.h>

namespace rkav {
namespace {

// 空 JSON 对象应采用安全默认值，并自动提供一个启用的 Null Sink。
TEST(ConfigTest, ParsesDefaultsFromEmptyObject) {
    auto parsed = ConfigLoader::Parse("{}");

    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().schema_version, 1);
    EXPECT_EQ(parsed.value().runtime.mode, "mock");
    ASSERT_EQ(parsed.value().outputs.size(), 1U);
    EXPECT_TRUE(parsed.value().outputs.front().enabled);
}

// 故意拼错 fps，确认加载器指出 video.fpps 而不是静默采用默认 fps。
TEST(ConfigTest, RejectsUnknownFieldsToCatchMisspellings) {
    auto parsed = ConfigLoader::Parse(R"({"video":{"fpps":30}})");

    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().category, ErrorCategory::kInvalidConfig);
    EXPECT_NE(parsed.error().message.find("fpps"), std::string::npos);
}

// V4L2 专用字段必须进入强类型配置，不能因为当前主机未启用硬件后端而被当作未知字段。
TEST(ConfigTest, ParsesV4l2DeviceAndBufferSettings) {
    auto parsed = ConfigLoader::Parse(
        R"({"video":{"device":"/dev/video9","capture_timeout_ms":1500,"mmap_buffer_count":6}})");

    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().video.device, "/dev/video9");
    EXPECT_EQ(parsed.value().video.capture_timeout_ms, 1500);
    EXPECT_EQ(parsed.value().video.mmap_buffer_count, 6U);
}

TEST(ConfigTest, ParsesAlsaDeviceAndCaptureTimeout) {
    auto parsed = ConfigLoader::Parse(
        R"({"audio":{"backend":"alsa","device":"hw:2,0","capture_timeout_ms":1500}})");

#if RKAV_WITH_ALSA
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().audio.backend, "alsa");
    EXPECT_EQ(parsed.value().audio.device, "hw:2,0");
    EXPECT_EQ(parsed.value().audio.capture_timeout_ms, 1500);
#else
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().operation, "audio.backend");
#endif
}

TEST(ConfigTest, ParsesRknnInferenceContractFields) {
    auto parsed = ConfigLoader::Parse(
        R"({"inference":{"model_path":"/models/yolov5s.rknn","input_width":640,"input_height":640,"max_fps":5,"object_threshold":0.3,"nms_threshold":0.5,"max_detections":64}})");

    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().inference.model_path, "/models/yolov5s.rknn");
    EXPECT_EQ(parsed.value().inference.input_width, 640);
    EXPECT_EQ(parsed.value().inference.input_height, 640);
    EXPECT_EQ(parsed.value().inference.max_fps, 5);
    EXPECT_FLOAT_EQ(parsed.value().inference.object_threshold, 0.3F);
    EXPECT_FLOAT_EQ(parsed.value().inference.nms_threshold, 0.5F);
    EXPECT_EQ(parsed.value().inference.max_detections, 64U);
}

TEST(ConfigTest, RejectsInvalidInferenceThresholds) {
    AppConfig config;
    config.outputs.emplace_back();
    config.inference.object_threshold = 1.0F;

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "inference");
}

// 缓冲数量少于 2 无法形成可靠的 V4L2 流水，必须在打开设备前拒绝。
TEST(ConfigTest, RejectsTooFewV4l2MmapBuffers) {
    AppConfig config;
    config.outputs.emplace_back();
    config.video.mmap_buffer_count = 1;

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "video.mmap_buffer_count");
}

// XRUN 周期为 0 会导致取模错误，因此必须在运行前拒绝。
TEST(ConfigTest, RejectsZeroXrunPeriodBeforeModuloIsEvaluated) {
    AppConfig config;
    config.outputs.emplace_back();
    config.audio.failure.xrun_every_blocks = 0;

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().operation.find("xrun_every_blocks"), std::string::npos);
}

// 没有任何启用输出时编码包无消费者，配置必须失败。
TEST(ConfigTest, RequiresAnEnabledOutput) {
    AppConfig config;
    OutputConfig output;
    output.enabled = false;
    config.outputs.push_back(output);

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "outputs");
}

// --validate-config 使用同一 Validate，因此未知日志级别不能漏到启动阶段才失败。
TEST(ConfigTest, ValidateOnlyModeRejectsUnknownLogLevel) {
    AppConfig config;
    config.outputs.emplace_back();
    config.runtime.log_level = "verbose";

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "runtime.log_level");
}

// 44100 Hz * 7 ms 不是整数样本数，无法构造完整固定 PCM 块。
TEST(ConfigTest, RejectsFractionalAudioBlockSize) {
    AppConfig config;
    config.outputs.emplace_back();
    config.audio.sample_rate = 44'100;
    config.audio.frame_duration_ms = 7;

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "audio.frame_duration_ms");
}

// 健康超时过短会把正常慢阶段误判成卡死，因此需要跨字段校验。
TEST(ConfigTest, StallTimeoutMustAllowSlowestConfiguredStageToProgress) {
    AppConfig config;
    config.outputs.emplace_back();
    config.audio.frame_duration_ms = 1'000;
    config.audio.queue_capacity_ms = 1'000;
    config.monitoring.health_interval_ms = 100;
    config.monitoring.worker_stall_timeout_ms = 500;

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "monitoring.worker_stall_timeout_ms");
}

// Sink 人工延迟不得达到关闭期限，否则 Stop 不能满足配置承诺。
TEST(ConfigTest, SinkDelayCannotExceedShutdownDeadline) {
    AppConfig config;
    config.outputs.emplace_back();
    config.runtime.shutdown_timeout_ms = 500;
    config.outputs.front().write_delay_ms = 500;

    auto result = ConfigLoader::Validate(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, "outputs[0].write_delay_ms");
}

// FFmpeg 未编译时必须在配置阶段拒绝真实编码，不能静默回退到 Checksum。
TEST(ConfigTest, FfmpegMediaConfigurationRequiresCompiledFeature) {
    auto parsed = ConfigLoader::Parse(
        R"({"video_encoder":{"backend":"ffmpeg"},"audio_encoder":{"backend":"ffmpeg"},"outputs":[{"type":"mp4","path":"recording.mp4","overflow_policy":"block_producer","push_timeout_ms":1000}]})");

#if RKAV_WITH_FFMPEG
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().video_encoder.codec_name, "libx264");
    EXPECT_EQ(parsed.value().audio_encoder.codec_name, "aac");
    EXPECT_EQ(parsed.value().outputs.front().type, "mp4");
#else
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().operation, "video_encoder.backend");
#endif
}

TEST(ConfigTest, ParsesAndValidatesCpuOverlay) {
    auto parsed = ConfigLoader::Parse(
        R"({"overlay":{"enabled":true,"backend":"cpu","line_width":3,"draw_labels":false,"wait_for_result_ms":100}})");

    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed.value().overlay.enabled);
    EXPECT_EQ(parsed.value().overlay.line_width, 3);
    EXPECT_FALSE(parsed.value().overlay.draw_labels);
    EXPECT_EQ(parsed.value().overlay.wait_for_result_ms, 100);

    auto invalid = ConfigLoader::Parse(
        R"({"overlay":{"wait_for_result_ms":201},"inference":{"max_result_age_ms":200}})");
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().operation, "overlay.wait_for_result_ms");
}

}  // namespace
}  // namespace rkav
