// 文件作用：验证 RTSP 网络输出 Sink 的工厂创建、URL 校验和流契约检查。
// 主要知识点：FFmpeg RTSP muxer 使用约束、工厂模式、输入校验顺序。
#include "rkav/output/sinks.h"

#include <array>
#include <string>

#include <gtest/gtest.h>

namespace rkav {
namespace {

EncodedStreamInfo MakeH264VideoStream() {
    return EncodedStreamInfo{.kind = StreamKind::kVideo,
                             .codec = Codec::kH264,
                             .time_base = Rational{1, 1'000'000},
                             .width = 320,
                             .height = 180,
                             .bit_rate = 1'000'000,
                             .extradata = {}};
}

TEST(RtspSinkFactoryTest, CreatesRtspSinkForValidConfig) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://0.0.0.0:8554/live";
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    EXPECT_EQ(sink.value()->name(), "rtsp");
}

TEST(RtspSinkFactoryTest, RejectsUnsupportedSinkType) {
    OutputConfig config;
    config.type = "unknown";
    config.path = "rtsp://0.0.0.0:8554/live";
    auto sink = CreatePacketSink(config);
    EXPECT_FALSE(sink);
}

// URL 校验发生在 Sink Open 阶段：空地址和错误协议都必须被拒绝。
TEST(RtspSinkOpenTest, RejectsEmptyUrl) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "";
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    const std::array<EncodedStreamInfo, 1> streams{MakeH264VideoStream()};
    EXPECT_FALSE(sink.value()->Open(config, streams));
}

TEST(RtspSinkOpenTest, RejectsNonRtspUrl) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "http://example.com/stream";
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    const std::array<EncodedStreamInfo, 1> streams{MakeH264VideoStream()};
    EXPECT_FALSE(sink.value()->Open(config, streams));
}

// RTSP 输出至少需要一路 H.264 视频流，音频可以缺省。
TEST(RtspSinkOpenTest, RejectsMissingVideoStream) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://127.0.0.1:8554/live";
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    EXPECT_FALSE(sink.value()->Open(config, {}));
}

}  // namespace
}  // namespace rkav
