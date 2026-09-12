// 文件作用：把 H.264/AAC 编码包通过 FFmpeg RTSP muxer 推送到网络。
// 主要知识点：RTSP 协议、avformat network I/O、TCP 传输选择、会话生命周期与断连重连。
#include "rkav/output/ffmpeg_rtsp_sink.h"

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "rkav/common/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

namespace rkav {
namespace {

Error RtspError(std::string_view operation, int code, std::string message,
                bool retryable = false) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> detail{};
    if (code < 0) {
        av_strerror(code, detail.data(), detail.size());
        message += ": ";
        message += detail.data();
    }
    return Error{ErrorCategory::kIo, code, "ffmpeg_rtsp_sink", std::string(operation),
                 std::move(message), retryable};
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
    bool has_audio{false};
    bool open{false};
    bool connected{false};  // 当前是否存在已写入流头的 RTSP 会话。
    bool flushed{false};
    bool reconnect_enabled{false};  // 由 reconnect_interval_ms > 0 决定。
    int reconnect_interval_ms{0};   // 断连后的最小重连间隔；0 表示关闭重连。
    std::chrono::steady_clock::time_point next_reconnect_at{};  // 下次允许重连的时刻。

    /// 功能：只释放当前 RTSP 会话，保留 Sink 的配置和流信息。
    void ReleaseFormat() noexcept {
        if (format != nullptr) {
            // RTSP muxer 设置 AVFMT_NOFILE，网络连接由 muxer 内部管理；
            // 仅对需要文件 IO 的 muxer 关闭由调用方打开的 AVIOContext。
            if (format->pb != nullptr && (format->oformat->flags & AVFMT_NOFILE) == 0) {
                avio_closep(&format->pb);
            }
            avformat_free_context(format);
            format = nullptr;
        }
        video_stream_index.reset();
        audio_stream_index.reset();
        connected = false;
    }

    /// 功能：释放全部资源并恢复初始状态。
    void Release() noexcept {
        ReleaseFormat();
        open = false;
        flushed = false;
        reconnect_enabled = false;
        reconnect_interval_ms = 0;
    }

    /// 功能：按配置间隔安排下一次重连尝试。
    void ScheduleNextReconnect() {
        next_reconnect_at = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(reconnect_interval_ms);
    }

    /// 功能：判断是否到达允许重连的时刻；关闭重连时恒为 false。
    [[nodiscard]] bool ReconnectDue() const {
        return reconnect_enabled && std::chrono::steady_clock::now() >= next_reconnect_at;
    }

    /// 功能：为当前 URL 新建 RTSP 会话并写入流头；失败时释放半初始化上下文。
    Result<void> Connect() {
        ReleaseFormat();
        // 显式选择 rtsp muxer：它标记 AVFMT_NOFILE，ANNOUNCE 连接在
        // avformat_write_header 内部完成，不能对网络 muxer 调用 avio_open。
        const int allocated =
            avformat_alloc_output_context2(&format, nullptr, "rtsp", url.c_str());
        if (allocated < 0 || format == nullptr) {
            ReleaseFormat();
            return Result<void>::Failure(
                RtspError("allocate_context", allocated, "cannot allocate RTSP muxer context"));
        }
        AVStream* video_stream = avformat_new_stream(format, nullptr);
        if (video_stream == nullptr) {
            ReleaseFormat();
            return Result<void>::Failure(
                RtspError("create_stream", AVERROR(ENOMEM), "cannot create RTSP video stream"));
        }
        auto configured = ConfigureStream(*video_stream, video_info);
        if (!configured) {
            ReleaseFormat();
            return configured;
        }
        video_stream_index = video_stream->index;

        if (has_audio) {
            AVStream* audio_stream = avformat_new_stream(format, nullptr);
            if (audio_stream == nullptr) {
                ReleaseFormat();
                return Result<void>::Failure(RtspError("create_stream", AVERROR(ENOMEM),
                                                       "cannot create RTSP audio stream"));
            }
            configured = ConfigureStream(*audio_stream, audio_info);
            if (!configured) {
                ReleaseFormat();
                return configured;
            }
            audio_stream_index = audio_stream->index;
        }

        // TCP 传输更可靠，适合嵌入式场景；RTSP muxer 只支持向服务器推流（ANNOUNCE），
        // FFmpeg 的 listen 选项仅存在于 demuxer，不能用于输出侧。
        AVDictionary* options = nullptr;
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        const int header = avformat_write_header(format, &options);
        av_dict_free(&options);
        if (header < 0) {
            ReleaseFormat();
            return Result<void>::Failure(
                RtspError("write_header", header, "cannot write RTSP stream header"));
        }
        connected = true;
        return Result<void>::Success();
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
    if (!impl_->url.starts_with("rtsp://")) {
        return Result<void>::Failure(
            RtspError("open", AVERROR(EINVAL), "RTSP output URL must start with rtsp://"));
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

    impl_->has_audio = found_audio;
    impl_->reconnect_enabled = config.reconnect_interval_ms > 0;
    impl_->reconnect_interval_ms = config.reconnect_interval_ms;

    auto connected = impl_->Connect();  // 首次建立 RTSP 会话的结果。
    if (!connected) {
        // required 输出保持启动即失败；可选输出开启重连后先进入等待状态，由 Write 触发重连。
        if (config.required || !impl_->reconnect_enabled) {
            impl_->Release();
            return connected;
        }
        impl_->open = true;
        impl_->flushed = false;
        impl_->ScheduleNextReconnect();
        Logger::Instance().Log(LogLevel::kWarn, "ffmpeg_rtsp_sink", "rtsp_waiting_for_server",
                               "RTSP server is unreachable; reconnecting on write",
                               {{"url", impl_->url},
                                {"retry_interval_ms",
                                 std::to_string(impl_->reconnect_interval_ms)}});
        return Result<void>::Success();
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

    if (!impl_->connected) {
        if (!impl_->ReconnectDue()) {
            // 重连未到期：本包按丢包处理，返回可重试错误且不阻塞后续包。
            return Result<void>::Failure(
                RtspError("write", AVERROR(ECONNRESET),
                          "RTSP session is disconnected; packet dropped before next reconnect",
                          true));
        }
        auto reconnected = impl_->Connect();  // 本次重连尝试结果。
        if (!reconnected) {
            impl_->ScheduleNextReconnect();
            return Result<void>::Failure(
                RtspError("reconnect", reconnected.error().native_code,
                          "RTSP reconnect failed: " + reconnected.error().message, true));
        }
        Logger::Instance().Log(LogLevel::kInfo, "ffmpeg_rtsp_sink", "rtsp_reconnected",
                               "RTSP session re-established", {{"url", impl_->url}});
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
        // 连接级失败：结束当前会话；开启重连时由后续 Write 按间隔重建。
        const bool retryable = impl_->reconnect_enabled;
        impl_->ReleaseFormat();
        if (retryable) {
            impl_->ScheduleNextReconnect();
            Logger::Instance().Log(LogLevel::kWarn, "ffmpeg_rtsp_sink", "rtsp_connection_lost",
                                   "RTSP session write failed; reconnect scheduled",
                                   {{"url", impl_->url}, {"native_code", std::to_string(written)}});
        }
        return Result<void>::Failure(RtspError("write_packet", written,
                                               "cannot interleave packet into RTSP stream",
                                               retryable));
    }
    return Result<void>::Success();
}

Result<void> FfmpegRtspSink::Flush() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<void>::Failure(
            RtspError("flush", AVERROR(EINVAL), "RTSP sink cannot be finalized"));
    }
    if (!impl_->connected) {
        // 断连期间没有可收尾的会话，直接结束写入。
        impl_->flushed = true;
        return Result<void>::Success();
    }
    const int trailer = av_write_trailer(impl_->format);
    if (trailer < 0) {
        impl_->ReleaseFormat();
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
