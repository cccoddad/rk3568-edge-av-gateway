// 文件作用：验证 H.264 裸码流 Sink 的临时文件完成语义和音频包消费边界。
#include "rkav/output/sinks.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace rkav {
namespace {

std::filesystem::path UniqueEvidencePath() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("rkav-h264-sink-" + std::to_string(tick) + ".h264");
}

EncodedPacket MakeH264Packet(std::int64_t dts, std::initializer_list<unsigned char> bytes) {
    auto buffer = Buffer::Allocate(bytes.size());
    std::size_t index = 0;
    for (const unsigned char byte : bytes) {
        buffer->data()[index++] = static_cast<std::byte>(byte);
    }
    EncodedPacket packet;
    packet.kind = StreamKind::kVideo;
    packet.pts = dts;
    packet.dts = dts;
    packet.time_base = Rational{1, 1'000'000};
    packet.key_frame = dts == 0;
    packet.codec = Codec::kH264;
    packet.buffer = std::move(buffer);
    packet.duration = 33'333;
    return packet;
}

TEST(H264ElementarySinkTest, FinalizesOnlyAfterFlushAndWritesOnlyVideoPayload) {
    const auto final_path = UniqueEvidencePath();
    const auto partial_path = std::filesystem::path(final_path.string() + ".part");
    std::error_code error;
    std::filesystem::remove(final_path, error);
    std::filesystem::remove(partial_path, error);

    OutputConfig config;
    config.type = "h264";
    config.path = final_path.string();
    H264ElementarySink sink;
    const EncodedStreamInfo video{.kind = StreamKind::kVideo,
                                  .codec = Codec::kH264,
                                  .time_base = Rational{1, 1'000'000},
                                  .width = 320,
                                  .height = 180,
                                  .bit_rate = 1'000'000,
                                  .extradata = {}};
    const EncodedStreamInfo audio{.kind = StreamKind::kAudio,
                                  .codec = Codec::kMockAudioChecksum,
                                  .time_base = Rational{1, 1'000'000},
                                  .sample_rate = 48'000,
                                  .channels = 2,
                                  .extradata = {}};
    const std::array<EncodedStreamInfo, 2> streams{video, audio};

    ASSERT_TRUE(sink.Open(config, streams));
    EXPECT_FALSE(std::filesystem::exists(final_path));
    EXPECT_TRUE(std::filesystem::exists(partial_path));

    EncodedPacket audio_packet;
    audio_packet.kind = StreamKind::kAudio;
    audio_packet.pts = 0;
    audio_packet.dts = 0;
    audio_packet.time_base = Rational{1, 1'000'000};
    audio_packet.key_frame = true;
    audio_packet.codec = Codec::kMockAudioChecksum;
    audio_packet.buffer = Buffer::Allocate(1);
    audio_packet.duration = 20'000;
    ASSERT_TRUE(sink.Write(audio_packet));
    ASSERT_TRUE(sink.Write(MakeH264Packet(0, {0x00, 0x00, 0x01, 0x65, 0x88})));
    ASSERT_TRUE(sink.Flush());

    EXPECT_TRUE(std::filesystem::exists(final_path));
    EXPECT_FALSE(std::filesystem::exists(partial_path));
    std::ifstream input(final_path, std::ios::binary);
    const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(content, std::string("\0\0\1\x65\x88", 5));
    input.close();

    sink.Close();
    std::filesystem::remove(final_path, error);
    EXPECT_FALSE(error);
}

}  // namespace
}  // namespace rkav
