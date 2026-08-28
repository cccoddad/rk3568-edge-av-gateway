// 文件作用：把 H.264/AAC 编码包交错封装为可原子完成的 MP4 文件。
#include "rkav/output/ffmpeg_mp4_sink.h"

#include <array>
#include <cstring>
#include <filesystem>
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

Error Mp4Error(std::string_view operation, int code, std::string message) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> detail{};
    if (code < 0) {
        av_strerror(code, detail.data(), detail.size());
        message += ": ";
        message += detail.data();
    }
    return Error{ErrorCategory::kIo, code, "ffmpeg_mp4_sink", std::string(operation),
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
        parameters->extradata =
            static_cast<std::uint8_t*>(av_mallocz(padded_size));
        if (parameters->extradata == nullptr) {
            return Result<void>::Failure(
                Mp4Error("configure_stream", AVERROR(ENOMEM), "cannot allocate codec extradata"));
        }
        std::memcpy(parameters->extradata, info.extradata.data(), info.extradata.size());
        parameters->extradata_size = static_cast<int>(info.extradata.size());
    }
    stream.time_base = ToAvRational(info.time_base);
    return Result<void>::Success();
}

}  // namespace

struct FfmpegMp4Sink::Impl {
    std::mutex mutex;
    AVFormatContext* format{nullptr};
    std::optional<int> video_stream_index;
    std::optional<int> audio_stream_index;
    EncodedStreamInfo video_info;
    EncodedStreamInfo audio_info;
    std::filesystem::path final_path;
    std::filesystem::path temporary_path;
    bool open{false};
    bool finalized{false};

    void Release() noexcept {
        if (format != nullptr && format->pb != nullptr) {
            avio_closep(&format->pb);
        }
        avformat_free_context(format);
        format = nullptr;
        video_stream_index.reset();
        audio_stream_index.reset();
        open = false;
    }
};

FfmpegMp4Sink::FfmpegMp4Sink() : impl_(std::make_unique<Impl>()) {}
FfmpegMp4Sink::~FfmpegMp4Sink() { Close(); }

Result<void> FfmpegMp4Sink::Open(const OutputConfig& config,
                                 std::span<const EncodedStreamInfo> streams) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->open) {
        return Result<void>::Failure(
            Mp4Error("open", AVERROR(EINVAL), "sink is already open"));
    }
    impl_->final_path = std::filesystem::path(config.path);
    impl_->temporary_path = std::filesystem::path(config.path + ".part");
    if (impl_->final_path.empty()) {
        return Result<void>::Failure(
            Mp4Error("open", AVERROR(EINVAL), "MP4 output path is empty"));
    }
    std::error_code filesystem_error;
    if (impl_->final_path.has_parent_path()) {
        std::filesystem::create_directories(impl_->final_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return Result<void>::Failure(Mp4Error(
                "create_directory", AVERROR(EIO),
                "cannot create MP4 output directory: " + filesystem_error.message()));
        }
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
    if (!found_video || !found_audio) {
        return Result<void>::Failure(Mp4Error(
            "open", AVERROR(EINVAL), "MP4 requires exactly one H.264 stream and one AAC stream"));
    }

    const std::string temporary = impl_->temporary_path.string();
    const int allocated =
        avformat_alloc_output_context2(&impl_->format, nullptr, "mp4", temporary.c_str());
    if (allocated < 0 || impl_->format == nullptr) {
        impl_->Release();
        return Result<void>::Failure(
            Mp4Error("allocate_context", allocated, "cannot allocate MP4 muxer"));
    }
    AVStream* video_stream = avformat_new_stream(impl_->format, nullptr);
    AVStream* audio_stream = avformat_new_stream(impl_->format, nullptr);
    if (video_stream == nullptr || audio_stream == nullptr) {
        impl_->Release();
        return Result<void>::Failure(
            Mp4Error("create_stream", AVERROR(ENOMEM), "cannot create MP4 streams"));
    }
    auto configured = ConfigureStream(*video_stream, impl_->video_info);
    if (configured) {
        configured = ConfigureStream(*audio_stream, impl_->audio_info);
    }
    if (!configured) {
        impl_->Release();
        return configured;
    }
    impl_->video_stream_index = video_stream->index;
    impl_->audio_stream_index = audio_stream->index;

    const int opened = avio_open(&impl_->format->pb, temporary.c_str(), AVIO_FLAG_WRITE);
    if (opened < 0) {
        impl_->Release();
        return Result<void>::Failure(
            Mp4Error("open_file", opened, "cannot open temporary MP4 file"));
    }
    AVDictionary* options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    const int header = avformat_write_header(impl_->format, &options);
    av_dict_free(&options);
    if (header < 0) {
        impl_->Release();
        return Result<void>::Failure(
            Mp4Error("write_header", header, "cannot write MP4 header"));
    }
    impl_->open = true;
    impl_->finalized = false;
    return Result<void>::Success();
}

Result<void> FfmpegMp4Sink::Write(const EncodedPacket& packet) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->finalized) {
        return Result<void>::Failure(
            Mp4Error("write", AVERROR(EINVAL), "sink is not writable"));
    }
    auto validation = ValidatePacket(packet);
    if (!validation) {
        return validation;
    }
    const EncodedStreamInfo& info =
        packet.kind == StreamKind::kVideo ? impl_->video_info : impl_->audio_info;
    if (packet.codec != info.codec) {
        return Result<void>::Failure(
            Mp4Error("write", AVERROR_INVALIDDATA, "packet codec changed after MP4 header"));
    }
    const int stream_index = packet.kind == StreamKind::kVideo
                                 ? *impl_->video_stream_index
                                 : *impl_->audio_stream_index;
    AVPacket* mux_packet = av_packet_alloc();
    if (mux_packet == nullptr) {
        return Result<void>::Failure(
            Mp4Error("allocate_packet", AVERROR(ENOMEM), "cannot allocate mux packet"));
    }
    const int allocated = av_new_packet(mux_packet, static_cast<int>(packet.buffer->size()));
    if (allocated < 0) {
        av_packet_free(&mux_packet);
        return Result<void>::Failure(
            Mp4Error("allocate_packet", allocated, "cannot allocate mux packet payload"));
    }
    std::memcpy(mux_packet->data, packet.buffer->data(), packet.buffer->size());
    mux_packet->pts = packet.pts;
    mux_packet->dts = packet.dts;
    mux_packet->duration = packet.duration;
    mux_packet->stream_index = stream_index;
    mux_packet->pos = -1;
    if (packet.key_frame) {
        mux_packet->flags |= AV_PKT_FLAG_KEY;
    }
    av_packet_rescale_ts(mux_packet, ToAvRational(packet.time_base),
                         impl_->format->streams[stream_index]->time_base);
    const int written = av_interleaved_write_frame(impl_->format, mux_packet);
    av_packet_free(&mux_packet);
    if (written < 0) {
        return Result<void>::Failure(
            Mp4Error("write_packet", written, "cannot interleave packet into MP4"));
    }
    return Result<void>::Success();
}

Result<void> FfmpegMp4Sink::Flush() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->finalized) {
        return Result<void>::Failure(
            Mp4Error("flush", AVERROR(EINVAL), "sink cannot be finalized"));
    }
    const int trailer = av_write_trailer(impl_->format);
    if (trailer < 0) {
        return Result<void>::Failure(
            Mp4Error("write_trailer", trailer, "cannot finalize MP4 index"));
    }
    const int closed = avio_closep(&impl_->format->pb);
    if (closed < 0) {
        return Result<void>::Failure(
            Mp4Error("close_file", closed, "cannot close temporary MP4 file"));
    }
    std::error_code filesystem_error;
    std::filesystem::rename(impl_->temporary_path, impl_->final_path, filesystem_error);
    if (filesystem_error) {
        return Result<void>::Failure(Mp4Error(
            "publish_file", AVERROR(EIO),
            "cannot atomically publish MP4 file: " + filesystem_error.message()));
    }
    impl_->finalized = true;
    return Result<void>::Success();
}

void FfmpegMp4Sink::Close() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->Release();
}

}  // namespace rkav
