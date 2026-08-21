// 文件作用：验证 Mock 音视频源和 Checksum 编码器的内容、故障恢复及时间戳契约。
// 主要知识点：ManualClock、确定性数据、摘要比较、XRUN 和编码状态校验。
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "rkav/capture/mock_audio_capture.h"
#include "rkav/capture/mock_video_capture.h"
#include "rkav/common/checksum.h"
#include "rkav/media/checksum_encoder.h"

namespace rkav {
namespace {

// 第 2 块注入一次 XRUN，恢复后仍应成功返回 sequence=2、PTS=40ms 的同一块。
TEST(MockAudioCaptureTest, RecoversXrunWithoutCreatingTimestampGap) {
    auto clock = std::make_shared<ManualClock>();
    MockAudioCapture capture(clock);
    AudioConfig config;
    config.realtime = false;
    config.sample_rate = 8'000;
    config.frequency_hz = 100.0;
    config.failure.xrun_every_blocks = 2;
    ASSERT_TRUE(capture.Open(config));

    auto frame0 = capture.Read({});
    auto frame1 = capture.Read({});
    auto xrun = capture.Read({});
    ASSERT_TRUE(frame0);
    ASSERT_TRUE(frame1);
    ASSERT_FALSE(xrun);
    EXPECT_EQ(xrun.error().category, ErrorCategory::kXrun);
    ASSERT_TRUE(capture.Recover());

    auto recovered = capture.Read({});
    ASSERT_TRUE(recovered);
    EXPECT_EQ(recovered.value().sequence, 2U);
    EXPECT_EQ(recovered.value().pts_us, 40'000);
}

// 连续视频帧布局都应合法，移动框和帧号使两帧摘要不同。
TEST(MockVideoCaptureTest, GeneratesChangingFramesWithValidLayout) {
    auto clock = std::make_shared<ManualClock>();
    MockVideoCapture capture(clock);
    VideoConfig config;
    config.width = 64;
    config.height = 48;
    config.realtime = false;
    ASSERT_TRUE(capture.Open(config));

    auto first = capture.Read({});
    auto second = capture.Read({});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(ValidateVideoFrame(first.value()));
    ASSERT_TRUE(ValidateVideoFrame(second.value()));
    EXPECT_NE(Fnv1a64(first.value().buffer->span()), Fnv1a64(second.value().buffer->span()));
}

// 第一块持续 20ms，下一块直接从 40ms 开始应被判定为缺失一块。
TEST(ChecksumAudioEncoderTest, RejectsMissingAudioBlock) {
    ChecksumAudioEncoder encoder;
    ASSERT_TRUE(encoder.Open(AudioEncoderConfig{}));
    auto samples = Buffer::Allocate(320U);
    AudioFrame first{0, 0, 8'000, 1, 160, SampleFormat::kS16LE, samples};
    AudioFrame discontinuous{1, 40'000, 8'000, 1, 160, SampleFormat::kS16LE, samples};

    ASSERT_TRUE(encoder.Encode(first));
    auto result = encoder.Encode(discontinuous);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::kCodec);
}

// 关键帧周期为 3 时，sequence=3 必须标记为关键帧并使用 Mock codec。
TEST(ChecksumVideoEncoderTest, MarksConfiguredKeyframes) {
    ChecksumVideoEncoder encoder;
    VideoEncoderConfig config;
    config.mock_keyframe_interval = 3;
    ASSERT_TRUE(encoder.Open(config));
    VideoFrame frame{3, 1000, 16, 16, 48, PixelFormat::kRgb888, Buffer::Allocate(16U * 16U * 3U),
                     {}};

    auto encoded = encoder.Encode(frame);
    ASSERT_TRUE(encoded);
    ASSERT_EQ(encoded.value().size(), 1U);
    EXPECT_TRUE(encoded.value().front().key_frame);
    EXPECT_EQ(encoded.value().front().codec, Codec::kMockVideoChecksum);
}

}  // namespace
}  // namespace rkav
