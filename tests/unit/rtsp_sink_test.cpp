// 文件作用：验证 RTSP 网络输出 Sink 的工厂创建、URL 校验、流契约和重连等待语义。
// 主要知识点：FFmpeg RTSP muxer 使用约束、工厂模式、输入校验顺序和可重试错误。
#include "rkav/output/sinks.h"

#include <array>
#include <string>

#include <gtest/gtest.h>

#include "rkav/common/buffer.h"

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

EncodedPacket MakeVideoPacket(std::int64_t pts_us) {
    EncodedPacket packet;
    packet.kind = StreamKind::kVideo;
    packet.codec = Codec::kH264;
    packet.time_base = Rational{1, 1'000'000};
    packet.pts = pts_us;
    packet.dts = pts_us;
    packet.key_frame = true;
    packet.buffer = Buffer::Allocate(16);
    return packet;
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

// 开启重连的可选输出允许服务器暂不可达：Open 成功进入等待，写入返回可重试错误。
TEST(RtspSinkReconnectTest, OptionalSinkWaitsForServerWhenReconnectEnabled) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://127.0.0.1:18554/live";
    config.required = false;
    config.reconnect_interval_ms = 1000;
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    const std::array<EncodedStreamInfo, 1> streams{MakeH264VideoStream()};
    ASSERT_TRUE(sink.value()->Open(config, streams));

    auto written = sink.value()->Write(MakeVideoPacket(0));
    ASSERT_FALSE(written);
    EXPECT_TRUE(written.error().retryable);
    sink.value()->Flush();
    sink.value()->Close();
}

// required 输出保持启动即失败语义，重连只作用于运行中的断连。
TEST(RtspSinkReconnectTest, RequiredSinkFailsFastWhenServerIsDown) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://127.0.0.1:18554/live";
    config.required = true;
    config.reconnect_interval_ms = 1000;
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    const std::array<EncodedStreamInfo, 1> streams{MakeH264VideoStream()};
    EXPECT_FALSE(sink.value()->Open(config, streams));
}

// 关闭重连时保持旧语义：服务器不可达直接打开失败，且错误不可重试。
TEST(RtspSinkReconnectTest, DisabledReconnectFailsOpenWithoutRetry) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://127.0.0.1:18554/live";
    config.required = false;
    config.reconnect_interval_ms = 0;
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    const std::array<EncodedStreamInfo, 1> streams{MakeH264VideoStream()};
    auto opened = sink.value()->Open(config, streams);
    ASSERT_FALSE(opened);
    EXPECT_FALSE(opened.error().retryable);
}

}  // namespace
}  // namespace rkav
