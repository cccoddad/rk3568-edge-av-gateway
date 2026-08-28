// 文件作用：验证固定 JPEG、损坏输入、尺寸变化、连续解码和真实摄像头样本。
// 主要知识点：压缩帧契约、元数据保持、可恢复错误与确定性回归。
#include "rkav/media/jpeg_video_decoder.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "rkav/common/checksum.h"

namespace rkav {
namespace {

constexpr std::string_view kThreeByTwoJpegBase64 =
    "/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsK"
    "CwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQU"
    "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAACAAMDASIA"
    "AhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAA"
    "F9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6"
    "Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqr"
    "KztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEB"
    "AQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcR"
    "MiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpj"
    "ZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyM"
    "nK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD6O+EnwP8Ahzr3h/XrrU/A"
    "HhfUblfFviS2Wa70a2lcRRa3exRRhmQnakaIir0VUUDAAFFFFc9D+FD0X5G1b+LL1Z//2Q==";

std::vector<std::byte> DecodeBase64(std::string_view input) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::byte> output;
    output.reserve(input.size() * 3U / 4U);
    std::uint32_t accumulator = 0U;
    int available_bits = -8;
    for (const char character : input) {
        if (character == '=') {
            break;
        }
        const std::size_t value = alphabet.find(character);
        if (value == std::string_view::npos) {
            continue;
        }
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        available_bits += 6;
        if (available_bits >= 0) {
            const auto byte_value =
                static_cast<unsigned char>((accumulator >> available_bits) & 0xFFU);
            output.push_back(static_cast<std::byte>(byte_value));
            available_bits -= 8;
        }
    }
    return output;
}

VideoFrame CompressedFrame(std::vector<std::byte> bytes, int declared_width = 3,
                           int declared_height = 2, std::uint64_t sequence = 7U,
                           TimestampUs pts_us = 123'456) {
    return VideoFrame{sequence,
                      pts_us,
                      declared_width,
                      declared_height,
                      0,
                      PixelFormat::kMjpeg,
                      std::make_shared<Buffer>(std::move(bytes)),
                      FrameMemory{MemoryKind::kCpu, -1}};
}

TEST(JpegVideoDecoderTest, DecodesFixedJpegAndPreservesSourceMetadata) {
    JpegVideoDecoder decoder;
    ASSERT_TRUE(decoder.Open());

    auto result = decoder.Decode(CompressedFrame(DecodeBase64(kThreeByTwoJpegBase64)));

    ASSERT_TRUE(result) << DescribeError(result.error());
    EXPECT_EQ(result.value().sequence, 7U);
    EXPECT_EQ(result.value().pts_us, 123'456);
    EXPECT_EQ(result.value().width, 3);
    EXPECT_EQ(result.value().height, 2);
    EXPECT_EQ(result.value().stride, 9);
    EXPECT_EQ(result.value().format, PixelFormat::kRgb888);
    EXPECT_EQ(result.value().memory.kind, MemoryKind::kCpu);
    ASSERT_NE(result.value().buffer, nullptr);
    EXPECT_EQ(result.value().buffer->size(), 18U);
    EXPECT_TRUE(ValidateVideoFrame(result.value()));
}

TEST(JpegVideoDecoderTest, UsesJpegHeaderDimensionsWhenSourceMetadataChanges) {
    JpegVideoDecoder decoder;
    ASSERT_TRUE(decoder.Open());

    auto result = decoder.Decode(
        CompressedFrame(DecodeBase64(kThreeByTwoJpegBase64), 1280, 720, 99U, 456'789));

    ASSERT_TRUE(result) << DescribeError(result.error());
    EXPECT_EQ(result.value().width, 3);
    EXPECT_EQ(result.value().height, 2);
    EXPECT_EQ(result.value().sequence, 99U);
    EXPECT_EQ(result.value().pts_us, 456'789);
}

TEST(JpegVideoDecoderTest, ReportsCorruptJpegAsRetryableFrameError) {
    JpegVideoDecoder decoder;
    ASSERT_TRUE(decoder.Open());
    std::vector<std::byte> corrupt{std::byte{0xFF}, std::byte{0xD8}, std::byte{0x00},
                                  std::byte{0x01}, std::byte{0x02}};

    auto result = decoder.Decode(CompressedFrame(std::move(corrupt)));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::kCodec);
    EXPECT_TRUE(result.error().retryable);
}

TEST(JpegVideoDecoderTest, RecoversAfterCorruptFrameOnTheSameDecoder) {
    JpegVideoDecoder decoder;
    ASSERT_TRUE(decoder.Open());

    std::vector<std::byte> corrupt{std::byte{0xFF}, std::byte{0xD8}, std::byte{0x00}};
    auto corrupt_result = decoder.Decode(CompressedFrame(std::move(corrupt)));
    ASSERT_FALSE(corrupt_result);
    ASSERT_TRUE(corrupt_result.error().retryable);

    auto recovered = decoder.Decode(
        CompressedFrame(DecodeBase64(kThreeByTwoJpegBase64), 3, 2, 8U, 234'567));

    ASSERT_TRUE(recovered) << DescribeError(recovered.error());
    EXPECT_EQ(recovered.value().sequence, 8U);
    EXPECT_EQ(recovered.value().pts_us, 234'567);
    EXPECT_EQ(recovered.value().width, 3);
    EXPECT_EQ(recovered.value().height, 2);
}

TEST(JpegVideoDecoderTest, ContinuouslyDecodesFramesDeterministically) {
    JpegVideoDecoder decoder;
    ASSERT_TRUE(decoder.Open());
    std::uint64_t expected_checksum = 0U;
    for (std::uint64_t sequence = 0U; sequence < 20U; ++sequence) {
        auto result = decoder.Decode(
            CompressedFrame(DecodeBase64(kThreeByTwoJpegBase64), 3, 2, sequence));
        ASSERT_TRUE(result) << DescribeError(result.error());
        const std::uint64_t checksum = Fnv1a64(result.value().buffer->span());
        if (sequence == 0U) {
            expected_checksum = checksum;
        }
        EXPECT_EQ(checksum, expected_checksum);
        EXPECT_EQ(result.value().sequence, sequence);
    }
}

TEST(JpegVideoDecoderTest, DecodesRealCameraSampleWhenProvided) {
    const char* const path = std::getenv("RKAV_CAMERA_JPEG");
    if (path == nullptr || path[0] == '\0') {
        GTEST_SKIP() << "RKAV_CAMERA_JPEG is not set";
    }
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input) << "cannot open camera JPEG: " << path;
    const std::vector<char> file_bytes{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
    std::vector<std::byte> jpeg_bytes;
    jpeg_bytes.reserve(file_bytes.size());
    for (const char value : file_bytes) {
        jpeg_bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }

    JpegVideoDecoder decoder;
    ASSERT_TRUE(decoder.Open());
    auto result = decoder.Decode(CompressedFrame(std::move(jpeg_bytes), 1280, 720));

    ASSERT_TRUE(result) << DescribeError(result.error());
    EXPECT_EQ(result.value().width, 1280);
    EXPECT_EQ(result.value().height, 720);
    EXPECT_EQ(result.value().stride, 3840);
    EXPECT_EQ(result.value().buffer->size(), 2'764'800U);
}

}  // namespace
}  // namespace rkav
