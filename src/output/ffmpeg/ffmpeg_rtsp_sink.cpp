// 文件作用：把 H.264/AAC 编码包通过 FFmpeg RTSP muxer 推送到网络。
// 主要知识点：RTSP 协议、avformat network I/O、TCP/UDP 传输选择、连接生命周期。
#include "rkav/output/ffmpeg_rtsp_sink.h"

#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

namespace rkav {
namespace {

Error RtspError(std::string_view operation, int code, std::string message) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> detail{};
    if (code < 0) {
        av_strerror(code, detail.data(), detail.size());
        message += ": ";
        message += detail.data();
    }
    return Error{ErrorCategory::kIo, code, "ffmpeg_rtsp_sink", std::string(operation),
                 std::move(message), false};
}

AVRational ToAvRational(Rational value) {
    return AVRational{static_cast<int>(value.numerator), static_cast<int>(value.denominator)};
}

Result<void> ConfigureStream(AVStream& stream, const EncodedStreamInfo& info) {
    auto validation = ValidateStreamInfo(info);
    if (!validation) {
        return validation;
    }
    AVCodecParameters* parameters = stream.codecpar;
    parameters->codec_type =
        info.kind == StreamKind::kVideo ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
    parameters->codec_id = info.codec == Codec::kH264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_AAC;
    parameters->codec_tag = 0;
    parameters->bit_rate = info.bit_rate;
    if (info.kind == StreamKind::kVideo) {
        parameters->width = info.width;
        parameters->height = info.height;
    } else {
        parameters->sample_rate = info.sample_rate;
        parameters->channels = info.channels;
        parameters->channel_layout =
            static_cast<std::uint64_t>(av_get_default_channel_layout(info.channels));
    }
    if (!info.extradata.empty()) {
        const std::size_t padded_size = info.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE;
        parameters->extradata = static_cast<std::uint8_t*>(av_mallocz(padded_size));
        if (parameters->extradata == nullptr) {
            return Result<void>::Failure(
                RtspError("configure_stream", AVERROR(ENOMEM), "cannot allocate codec extradata"));
        }
        std::memcpy(parameters->extradata, info.extradata.data(), info.extradata.size());
        parameters->extradata_size = static_cast<int>(info.extradata.size());
    }
    stream.time_base = ToAvRational(info.time_base);
    return Result<void>::Success();
}

}  // namespace

struct FfmpegRtspSink::Impl {
    std::mutex mutex;
    AVFormatContext* format{nullptr};
    std::optional<int> video_stream_index;
    std::optional<int> audio_stream_index;
    EncodedStreamInfo video_info;
    EncodedStreamInfo audio_info;
    std::string url;
    bool open{false};
    bool flushed{false};

    void Release() noexcept {
        if (format != nullptr) {
            if (format->pb != nullptr) {
                avio_closep(&format->pb);
            }
            avformat_free_context(format);
            format = nullptr;
        }
        video_stream_index.reset();
        audio_stream_index.reset();
        open = false;
        flushed = false;
    }
};

FfmpegRtspSink::FfmpegRtspSink() : impl_(std::make_unique<Impl>()) {}
FfmpegRtspSink::~FfmpegRtspSink() { Close(); }

Result<void> FfmpegRtspSink::Open(const OutputConfig& config,
                                  std::span<const EncodedStreamInfo> streams) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->open) {
        return Result<void>::Failure(
            RtspError("open", AVERROR(EINVAL), "RTSP sink is already open"));
    }

    impl_->url = config.path;
    if (impl_->url.empty()) {
        return Result<void>::Failure(
            RtspError("open", AVERROR(EINVAL), "RTSP output URL is empty"));
    }

    bool found_video = false;
    bool found_audio = false;
    for (const auto& stream : streams) {
        if (stream.kind == StreamKind::kVideo && stream.codec == Codec::kH264 && !found_video) {
            impl_->video_info = stream;
            found_video = true;
        } else if (stream.kind == StreamKind::kAudio && stream.codec == Codec::kAac &&
                   !found_audio) {
            impl_->audio_info = stream;
            found_audio = true;
        }
    }
    if (!found_video) {
        return Result<void>::Failure(RtspError(
            "open", AVERROR(EINVAL), "RTSP output requires at least one H.264 video stream"));
    }

    // RTSP muxer：format 名称由 URL 协议决定，FFmpeg 自动选择 rtsp muxer。
    const int allocated = avformat_alloc_output_context2(&impl_->format, nullptr, nullptr,
                                                         impl_->url.c_str());
    if (allocated < 0 || impl_->format == nullptr) {
        impl_->Release();
        return Result<void>::Failure(
            RtspError("allocate_context", allocated, "cannot allocate RTSP muxer context"));
    }

    AVStream* video_stream = avformat_new_stream(impl_->format, nullptr);
    if (video_stream == nullptr) {
        impl_->Release();
        return Result<void>::Failure(
            RtspError("create_stream", AVERROR(ENOMEM), "cannot create RTSP video stream"));
    }
    auto configured = ConfigureStream(*video_stream, impl_->video_info);
    if (!configured) {
        impl_->Release();
        return configured;
    }
    impl_->video_stream_index = video_stream->index;

    if (found_audio) {
        AVStream* audio_stream = avformat_new_stream(impl_->format, nullptr);
        if (audio_stream == nullptr) {
            impl_->Release();
            return Result<void>::Failure(
                RtspError("create_stream", AVERROR(ENOMEM), "cannot create RTSP audio stream"));
        }
        configured = ConfigureStream(*audio_stream, impl_->audio_info);
        if (!configured) {
            impl_->Release();
            return configured;
        }
        impl_->audio_stream_index = audio_stream->index;
    }

    // RTSP 输出不需要打开文件，avformat_alloc_output_context2 已通过 URL 识别协议。
    // 但如果 format context 没有 pb，则需要为网络输出创建 AVIOContext。
    if (impl_->format->pb == nullptr) {
        const int opened = avio_open(&impl_->format->pb, impl_->url.c_str(), AVIO_FLAG_WRITE);
        if (opened < 0) {
            impl_->Release();
            return Result<void>::Failure(
                RtspError("open_url", opened, "cannot open RTSP URL: " + impl_->url));
        }
    }

    // 设置 RTSP 传输方式：TCP 更可靠，适合嵌入式场景。
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    // 设置 GOP 大小和初始关键帧，让客户端能更快收到第一帧。
    av_dict_set(&options, "gop_size", "30", 0);
    const int header = avformat_write_header(impl_->format, &options);
    av_dict_free(&options);
    if (header < 0) {
        impl_->Release();
        return Result<void>::Failure(
            RtspError("write_header", header, "cannot write RTSP stream header"));
    }
    impl_->open = true;
    impl_->flushed = false;
    return Result<void>::Success();
}

Result<void> FfmpegRtspSink::Write(const EncodedPacket& packet) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<void>::Failure(
            RtspError("write", AVERROR(EINVAL), "RTSP sink is not writable"));
    }
    auto validation = ValidatePacket(packet);
    if (!validation) {
        return validation;
    }

    const EncodedStreamInfo& info =
        packet.kind == StreamKind::kVideo ? impl_->video_info : impl_->audio_info;
    if (packet.codec != info.codec) {
        return Result<void>::Failure(
            RtspError("write", AVERROR_INVALIDDATA, "packet codec changed after RTSP header"));
    }

    std::optional<int> stream_index;
    if (packet.kind == StreamKind::kVideo && impl_->video_stream_index.has_value()) {
        stream_index = impl_->video_stream_index;
    } else if (packet.kind == StreamKind::kAudio && impl_->audio_stream_index.has_value()) {
        stream_index = impl_->audio_stream_index;
    }
    if (!stream_index.has_value()) {
        return Result<void>::Failure(
            RtspError("write", AVERROR(EINVAL), "no matching stream for packet kind"));
    }

    AVPacket* mux_packet = av_packet_alloc();
    if (mux_packet == nullptr) {
        return Result<void>::Failure(
            RtspError("allocate_packet", AVERROR(ENOMEM), "cannot allocate RTSP mux packet"));
    }
    const int allocated = av_new_packet(mux_packet, static_cast<int>(packet.buffer->size()));
    if (allocated < 0) {
        av_packet_free(&mux_packet);
        return Result<void>::Failure(
            RtspError("allocate_packet", allocated, "cannot allocate RTSP mux packet payload"));
    }
    std::memcpy(mux_packet->data, packet.buffer->data(), packet.buffer->size());
    mux_packet->pts = packet.pts;
    mux_packet->dts = packet.dts;
    mux_packet->duration = packet.duration;
    mux_packet->stream_index = *stream_index;
    mux_packet->pos = -1;
    if (packet.key_frame) {
        mux_packet->flags |= AV_PKT_FLAG_KEY;
    }
    av_packet_rescale_ts(mux_packet, ToAvRational(packet.time_base),
                         impl_->format->streams[*stream_index]->time_base);
    const int written = av_interleaved_write_frame(impl_->format, mux_packet);
    av_packet_free(&mux_packet);
    if (written < 0) {
        return Result<void>::Failure(
            RtspError("write_packet", written, "cannot interleave packet into RTSP stream"));
    }
    return Result<void>::Success();
}

Result<void> FfmpegRtspSink::Flush() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<void>::Failure(
            RtspError("flush", AVERROR(EINVAL), "RTSP sink cannot be finalized"));
    }
    const int trailer = av_write_trailer(impl_->format);
    if (trailer < 0) {
        return Result<void>::Failure(
            RtspError("write_trailer", trailer, "cannot finalize RTSP stream"));
    }
    impl_->flushed = true;
    return Result<void>::Success();
}

void FfmpegRtspSink::Close() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->Release();
}

}  // namespace rkav
