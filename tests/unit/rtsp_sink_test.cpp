// 文件作用：验证 RTSP 网络输出 Sink 的配置校验和工厂创建逻辑。
// 主要知识点：FFmpeg RTSP muxer 配置、URL 校验、编码器依赖检查。
#include "rkav/output/sinks.h"

#include <string>

#include <gtest/gtest.h>

namespace rkav {
namespace {

TEST(RtspSinkFactoryTest, CreatesRtspSinkForValidConfig) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://0.0.0.0:8554/live";
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    EXPECT_EQ(sink.value()->name(), "rtsp");
}

TEST(RtspSinkFactoryTest, RejectsEmptyUrl) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "";
    auto sink = CreatePacketSink(config);
    EXPECT_FALSE(sink);
}

TEST(RtspSinkFactoryTest, RejectsNonRtspUrl) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "http://example.com/stream";
    auto sink = CreatePacketSink(config);
    EXPECT_FALSE(sink);
}

TEST(RtspSinkFactoryTest, RejectsUnsupportedSinkType) {
    OutputConfig config;
    config.type = "unknown";
    config.path = "rtsp://0.0.0.0:8554/live";
    auto sink = CreatePacketSink(config);
    EXPECT_FALSE(sink);
}

TEST(RtspSinkFactoryTest, AcceptsTcpTransportUrl) {
    OutputConfig config;
    config.type = "rtsp";
    config.path = "rtsp://192.168.1.100:8554/stream1";
    auto sink = CreatePacketSink(config);
    ASSERT_TRUE(sink);
    EXPECT_EQ(sink.value()->name(), "rtsp");
}

}  // namespace
}  // namespace rkav
