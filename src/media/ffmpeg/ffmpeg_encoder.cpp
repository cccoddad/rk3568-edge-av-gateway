// 文件作用：使用 libavcodec/libswscale/libswresample 实现 H.264 和 AAC 软件编码。
#include "rkav/media/ffmpeg_encoder.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace rkav {
namespace {

Error FfmpegError(std::string_view module, std::string_view operation, int code,
                  std::string message) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> detail{};
    if (code < 0) {
        av_strerror(code, detail.data(), detail.size());
        message += ": ";
        message += detail.data();
    }
    return Error{ErrorCategory::kCodec, code, std::string(module), std::string(operation),
                 std::move(message), false};
}

AVPixelFormat ToAvPixelFormat(PixelFormat format) {
    if (format == PixelFormat::kRgb888) {
        return AV_PIX_FMT_RGB24;
    }
    if (format == PixelFormat::kBgr888) {
        return AV_PIX_FMT_BGR24;
    }
    return AV_PIX_FMT_NONE;
}

Rational ToRational(AVRational value) {
    return Rational{static_cast<std::int32_t>(value.num), static_cast<std::int32_t>(value.den)};
}

Result<std::vector<EncodedPacket>> ReceivePackets(
    AVCodecContext* context, StreamKind kind, Codec codec,
    const std::function<std::uint64_t(std::int64_t)>& sequence_for_pts) {
    std::vector<EncodedPacket> output;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_encoder", "receive_packet", AVERROR(ENOMEM), "cannot allocate AVPacket"));
    }

    while (true) {
        const int received = avcodec_receive_packet(context, packet);
        if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
            break;
        }
        if (received < 0) {
            av_packet_free(&packet);
            return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
                "ffmpeg_encoder", "receive_packet", received, "encoder did not return a packet"));
        }
        if (packet->size <= 0 || packet->data == nullptr || packet->pts == AV_NOPTS_VALUE ||
            packet->dts == AV_NOPTS_VALUE) {
            av_packet_free(&packet);
            return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
                "ffmpeg_encoder", "receive_packet", AVERROR_INVALIDDATA,
                "encoder returned an empty packet or missing timestamp"));
        }

        auto payload = Buffer::Allocate(static_cast<std::size_t>(packet->size));
        std::memcpy(payload->data(), packet->data, static_cast<std::size_t>(packet->size));
        EncodedPacket encoded;
        encoded.kind = kind;
        encoded.source_sequence = sequence_for_pts(packet->pts);
        encoded.pts = packet->pts;
        encoded.dts = packet->dts;
        encoded.time_base = ToRational(context->time_base);
        encoded.key_frame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
        encoded.codec = codec;
        encoded.buffer = std::move(payload);
        encoded.duration = packet->duration;
        output.push_back(std::move(encoded));
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return Result<std::vector<EncodedPacket>>::Success(std::move(output));
}

std::vector<std::byte> CopyExtradata(const AVCodecContext& context) {
    if (context.extradata == nullptr || context.extradata_size <= 0) {
        return {};
    }
    const auto size = static_cast<std::size_t>(context.extradata_size);
    std::vector<std::byte> output(size);
    std::memcpy(output.data(), context.extradata, size);
    return output;
}

}  // namespace

struct FfmpegVideoEncoder::Impl {
    std::mutex mutex;
    AVCodecContext* context{nullptr};
    AVFrame* frame{nullptr};
    SwsContext* scaler{nullptr};
    VideoCapabilities input;
    std::map<std::int64_t, std::uint64_t> sequences_by_pts;
    std::uint64_t last_sequence{0};
    bool open{false};
    bool flushed{false};

    void Close() noexcept {
        sws_freeContext(scaler);
        scaler = nullptr;
        av_frame_free(&frame);
        avcodec_free_context(&context);
        sequences_by_pts.clear();
        open = false;
        flushed = false;
    }
};

FfmpegVideoEncoder::FfmpegVideoEncoder() : impl_(std::make_unique<Impl>()) {}
FfmpegVideoEncoder::~FfmpegVideoEncoder() { Close(); }

Result<EncodedStreamInfo> FfmpegVideoEncoder::Open(const VideoEncoderConfig& config,
                                                   const VideoCapabilities& input) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->open) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "open", AVERROR(EINVAL), "encoder is already open"));
    }
    const AVPixelFormat source_format = ToAvPixelFormat(input.format);
    if (source_format == AV_PIX_FMT_NONE || input.width <= 0 || input.height <= 0 || input.fps <= 0) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "open", AVERROR(EINVAL),
            "software H.264 input must be negotiated RGB888 or BGR888"));
    }
    const AVCodec* codec = avcodec_find_encoder_by_name(config.codec_name.c_str());
    if (codec == nullptr || codec->type != AVMEDIA_TYPE_VIDEO || codec->id != AV_CODEC_ID_H264) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "find_encoder", AVERROR_ENCODER_NOT_FOUND,
            "configured FFmpeg encoder is not an H.264 encoder: " + config.codec_name));
    }

    impl_->context = avcodec_alloc_context3(codec);
    impl_->frame = av_frame_alloc();
    if (impl_->context == nullptr || impl_->frame == nullptr) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "allocate", AVERROR(ENOMEM), "cannot allocate encoder state"));
    }
    impl_->context->bit_rate = config.bitrate_bps;
    impl_->context->width = input.width;
    impl_->context->height = input.height;
    impl_->context->time_base = AVRational{1, 1'000'000};
    impl_->context->framerate = AVRational{input.fps, 1};
    impl_->context->gop_size = config.gop_size;
    impl_->context->max_b_frames = 0;
    impl_->context->pix_fmt = AV_PIX_FMT_YUV420P;
    impl_->context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* options = nullptr;
    if (config.codec_name == "libx264") {
        av_dict_set(&options, "preset", config.preset.c_str(), 0);
        av_dict_set(&options, "tune", "zerolatency", 0);
    }
    const int opened = avcodec_open2(impl_->context, codec, &options);
    av_dict_free(&options);
    if (opened < 0) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "open_codec", opened, "cannot open H.264 encoder"));
    }

    impl_->frame->format = impl_->context->pix_fmt;
    impl_->frame->width = impl_->context->width;
    impl_->frame->height = impl_->context->height;
    const int frame_buffer = av_frame_get_buffer(impl_->frame, 32);
    if (frame_buffer < 0) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "allocate_frame", frame_buffer,
            "cannot allocate YUV420P frame"));
    }
    impl_->scaler = sws_getContext(input.width, input.height, source_format, input.width,
                                   input.height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr,
                                   nullptr);
    if (impl_->scaler == nullptr) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "create_scaler", AVERROR(EINVAL),
            "cannot create RGB to YUV420P converter"));
    }

    impl_->input = input;
    impl_->open = true;
    impl_->flushed = false;
    EncodedStreamInfo info;
    info.kind = StreamKind::kVideo;
    info.codec = Codec::kH264;
    info.time_base = ToRational(impl_->context->time_base);
    info.width = input.width;
    info.height = input.height;
    info.bit_rate = config.bitrate_bps;
    info.extradata = CopyExtradata(*impl_->context);
    return Result<EncodedStreamInfo>::Success(std::move(info));
}

Result<std::vector<EncodedPacket>> FfmpegVideoEncoder::Encode(const VideoFrame& frame) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "encode", AVERROR(EINVAL), "encoder is not accepting frames"));
    }
    auto validation = ValidateVideoFrame(frame);
    if (!validation) {
        return Result<std::vector<EncodedPacket>>::Failure(validation.error());
    }
    if (frame.width != impl_->input.width || frame.height != impl_->input.height ||
        frame.format != impl_->input.format) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "encode", AVERROR(EINVAL),
            "video frame does not match negotiated encoder input"));
    }
    const int writable = av_frame_make_writable(impl_->frame);
    if (writable < 0) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "make_writable", writable, "YUV frame is not writable"));
    }
    const std::uint8_t* source_data[4]{
        reinterpret_cast<const std::uint8_t*>(frame.buffer->data()), nullptr, nullptr, nullptr};
    const int source_linesize[4]{frame.stride, 0, 0, 0};
    const int scaled = sws_scale(impl_->scaler, source_data, source_linesize, 0, frame.height,
                                 impl_->frame->data, impl_->frame->linesize);
    if (scaled != frame.height) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "convert", AVERROR_INVALIDDATA,
            "RGB to YUV conversion returned an incomplete frame"));
    }
    impl_->frame->pts = frame.pts_us;
    impl_->sequences_by_pts[impl_->frame->pts] = frame.sequence;
    impl_->last_sequence = frame.sequence;
    const int sent = avcodec_send_frame(impl_->context, impl_->frame);
    if (sent < 0) {
        impl_->sequences_by_pts.erase(impl_->frame->pts);
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "send_frame", sent, "H.264 encoder rejected a frame"));
    }
    return ReceivePackets(
        impl_->context, StreamKind::kVideo, Codec::kH264,
        [this](std::int64_t pts) {
            const auto found = impl_->sequences_by_pts.find(pts);
            if (found == impl_->sequences_by_pts.end()) {
                return impl_->last_sequence;
            }
            const std::uint64_t sequence = found->second;
            impl_->sequences_by_pts.erase(found);
            return sequence;
        });
}

Result<std::vector<EncodedPacket>> FfmpegVideoEncoder::Flush() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "flush", AVERROR(EINVAL), "encoder cannot be flushed"));
    }
    impl_->flushed = true;
    const int sent = avcodec_send_frame(impl_->context, nullptr);
    if (sent < 0 && sent != AVERROR_EOF) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_video_encoder", "flush", sent, "cannot start H.264 drain"));
    }
    return ReceivePackets(impl_->context, StreamKind::kVideo, Codec::kH264,
                          [this](std::int64_t pts) {
                              const auto found = impl_->sequences_by_pts.find(pts);
                              if (found == impl_->sequences_by_pts.end()) {
                                  return impl_->last_sequence;
                              }
                              const std::uint64_t sequence = found->second;
                              impl_->sequences_by_pts.erase(found);
                              return sequence;
                          });
}

void FfmpegVideoEncoder::Close() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->Close();
}

struct FfmpegAudioEncoder::Impl {
    std::mutex mutex;
    AVCodecContext* context{nullptr};
    SwrContext* resampler{nullptr};
    AVAudioFifo* fifo{nullptr};
    AudioCapabilities input;
    std::optional<TimestampUs> expected_next_pts_us;
    std::int64_t next_encoded_pts{0};
    std::uint64_t packet_sequence{0};
    bool have_pts{false};
    bool open{false};
    bool flushed{false};

    void Close() noexcept {
        av_audio_fifo_free(fifo);
        fifo = nullptr;
        swr_free(&resampler);
        avcodec_free_context(&context);
        expected_next_pts_us.reset();
        have_pts = false;
        open = false;
        flushed = false;
    }

    Result<void> QueueConverted(const std::uint8_t* input_data, int input_samples) {
        const int capacity = swr_get_out_samples(resampler, input_samples);
        if (capacity < 0) {
            return Result<void>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "resample_capacity", capacity,
                "cannot calculate converted sample capacity"));
        }
        if (capacity == 0) {
            return Result<void>::Success();
        }
        std::uint8_t** converted = nullptr;
        int line_size = 0;
        const int allocated = av_samples_alloc_array_and_samples(
            &converted, &line_size, context->channels, capacity, context->sample_fmt, 0);
        if (allocated < 0) {
            return Result<void>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "allocate_samples", allocated,
                "cannot allocate converted audio samples"));
        }
        const std::uint8_t* source[1]{input_data};
        const int converted_count =
            swr_convert(resampler, converted, capacity, input_data == nullptr ? nullptr : source,
                        input_samples);
        if (converted_count < 0) {
            av_freep(&converted[0]);
            av_freep(&converted);
            return Result<void>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "convert", converted_count,
                "cannot convert PCM sample format"));
        }
        if (converted_count > 0) {
            const int resized = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + converted_count);
            if (resized < 0 || av_audio_fifo_write(fifo, reinterpret_cast<void**>(converted),
                                                  converted_count) != converted_count) {
                av_freep(&converted[0]);
                av_freep(&converted);
                return Result<void>::Failure(FfmpegError(
                    "ffmpeg_audio_encoder", "queue_samples",
                    resized < 0 ? resized : AVERROR(ENOMEM), "cannot append samples to audio FIFO"));
            }
        }
        av_freep(&converted[0]);
        av_freep(&converted);
        return Result<void>::Success();
    }

    Result<std::vector<EncodedPacket>> EncodeFromFifo(int frame_samples, bool allow_padding) {
        const int available = av_audio_fifo_size(fifo);
        const int samples_to_read = std::min(available, frame_samples);
        AVFrame* frame = av_frame_alloc();
        if (frame == nullptr) {
            return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "allocate_frame", AVERROR(ENOMEM),
                "cannot allocate AAC frame"));
        }
        frame->nb_samples = frame_samples;
        frame->format = context->sample_fmt;
        frame->sample_rate = context->sample_rate;
        frame->channels = context->channels;
        frame->channel_layout = context->channel_layout;
        const int allocated = av_frame_get_buffer(frame, 0);
        if (allocated < 0) {
            av_frame_free(&frame);
            return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "allocate_frame", allocated,
                "cannot allocate AAC frame samples"));
        }
        if (av_audio_fifo_read(fifo, reinterpret_cast<void**>(frame->data), samples_to_read) !=
            samples_to_read) {
            av_frame_free(&frame);
            return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "read_fifo", AVERROR_INVALIDDATA,
                "cannot read the requested PCM samples"));
        }
        if (samples_to_read < frame_samples) {
            if (!allow_padding) {
                frame->nb_samples = samples_to_read;
            } else {
                av_samples_set_silence(frame->data, samples_to_read,
                                       frame_samples - samples_to_read,
                                       context->channels, context->sample_fmt);
            }
        }
        frame->pts = next_encoded_pts;
        next_encoded_pts += frame->nb_samples;
        const int sent = avcodec_send_frame(context, frame);
        av_frame_free(&frame);
        if (sent < 0) {
            return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
                "ffmpeg_audio_encoder", "send_frame", sent, "AAC encoder rejected a frame"));
        }
        return ReceivePackets(context, StreamKind::kAudio, Codec::kAac,
                              [this](std::int64_t) { return packet_sequence++; });
    }
};

FfmpegAudioEncoder::FfmpegAudioEncoder() : impl_(std::make_unique<Impl>()) {}
FfmpegAudioEncoder::~FfmpegAudioEncoder() { Close(); }

Result<EncodedStreamInfo> FfmpegAudioEncoder::Open(const AudioEncoderConfig& config,
                                                   const AudioCapabilities& input) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->open) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "open", AVERROR(EINVAL), "encoder is already open"));
    }
    if (input.format != SampleFormat::kS16LE || input.sample_rate <= 0 || input.channels <= 0) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "open", AVERROR(EINVAL),
            "software AAC input must be negotiated S16_LE PCM"));
    }
    const AVCodec* codec = avcodec_find_encoder_by_name(config.codec_name.c_str());
    if (codec == nullptr || codec->type != AVMEDIA_TYPE_AUDIO || codec->id != AV_CODEC_ID_AAC) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "find_encoder", AVERROR_ENCODER_NOT_FOUND,
            "configured FFmpeg encoder is not an AAC encoder: " + config.codec_name));
    }
    impl_->context = avcodec_alloc_context3(codec);
    if (impl_->context == nullptr) {
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "allocate", AVERROR(ENOMEM),
            "cannot allocate AAC encoder context"));
    }
    impl_->context->bit_rate = config.bitrate_bps;
    impl_->context->sample_rate = input.sample_rate;
    impl_->context->sample_fmt = AV_SAMPLE_FMT_FLTP;
    impl_->context->time_base = AVRational{1, input.sample_rate};
    impl_->context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    impl_->context->channels = input.channels;
    impl_->context->channel_layout =
        static_cast<std::uint64_t>(av_get_default_channel_layout(input.channels));
    const int opened = avcodec_open2(impl_->context, codec, nullptr);
    if (opened < 0) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "open_codec", opened, "cannot open AAC encoder"));
    }

    const std::int64_t input_layout = av_get_default_channel_layout(input.channels);
    impl_->resampler = swr_alloc_set_opts(
        nullptr, static_cast<std::int64_t>(impl_->context->channel_layout),
        impl_->context->sample_fmt, impl_->context->sample_rate, input_layout, AV_SAMPLE_FMT_S16,
        input.sample_rate, 0, nullptr);
    int created = impl_->resampler == nullptr ? AVERROR(ENOMEM) : swr_init(impl_->resampler);
    if (created < 0) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "create_resampler", created,
            "cannot create S16 to encoder sample converter"));
    }
    impl_->fifo = av_audio_fifo_alloc(impl_->context->sample_fmt,
                                      impl_->context->channels, 1);
    if (impl_->fifo == nullptr) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "create_fifo", AVERROR(ENOMEM),
            "cannot allocate bounded AAC reframe buffer"));
    }

    impl_->input = input;
    impl_->expected_next_pts_us.reset();
    impl_->packet_sequence = 0;
    impl_->have_pts = false;
    impl_->open = true;
    impl_->flushed = false;
    EncodedStreamInfo info;
    info.kind = StreamKind::kAudio;
    info.codec = Codec::kAac;
    info.time_base = ToRational(impl_->context->time_base);
    info.sample_rate = impl_->context->sample_rate;
    info.channels = impl_->context->channels;
    info.bit_rate = config.bitrate_bps;
    info.extradata = CopyExtradata(*impl_->context);
    return Result<EncodedStreamInfo>::Success(std::move(info));
}

Result<std::vector<EncodedPacket>> FfmpegAudioEncoder::Encode(const AudioFrame& frame) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "encode", AVERROR(EINVAL), "encoder is not accepting frames"));
    }
    auto validation = ValidateAudioFrame(frame);
    if (!validation) {
        return Result<std::vector<EncodedPacket>>::Failure(validation.error());
    }
    if (frame.sample_rate != impl_->input.sample_rate || frame.channels != impl_->input.channels ||
        frame.format != impl_->input.format) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "encode", AVERROR(EINVAL),
            "audio frame does not match negotiated encoder input"));
    }
    if (impl_->expected_next_pts_us.has_value() &&
        frame.pts_us != *impl_->expected_next_pts_us) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "timestamp", AVERROR_INVALIDDATA,
            "audio PTS is not continuous"));
    }
    const TimestampUs duration_us =
        static_cast<TimestampUs>(frame.samples_per_channel) * 1'000'000 / frame.sample_rate;
    impl_->expected_next_pts_us = frame.pts_us + duration_us;
    if (!impl_->have_pts) {
        impl_->next_encoded_pts =
            av_rescale_q(frame.pts_us, AVRational{1, 1'000'000}, impl_->context->time_base);
        impl_->have_pts = true;
    }
    auto queued = impl_->QueueConverted(
        reinterpret_cast<const std::uint8_t*>(frame.buffer->data()), frame.samples_per_channel);
    if (!queued) {
        return Result<std::vector<EncodedPacket>>::Failure(queued.error());
    }

    std::vector<EncodedPacket> output;
    const int frame_size = impl_->context->frame_size > 0 ? impl_->context->frame_size : 1024;
    while (av_audio_fifo_size(impl_->fifo) >= frame_size) {
        auto encoded = impl_->EncodeFromFifo(frame_size, false);
        if (!encoded) {
            return encoded;
        }
        auto& packets = encoded.value();
        output.insert(output.end(), std::make_move_iterator(packets.begin()),
                      std::make_move_iterator(packets.end()));
    }
    return Result<std::vector<EncodedPacket>>::Success(std::move(output));
}

Result<std::vector<EncodedPacket>> FfmpegAudioEncoder::Flush() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "flush", AVERROR(EINVAL), "encoder cannot be flushed"));
    }
    impl_->flushed = true;
    auto delayed = impl_->QueueConverted(nullptr, 0);
    if (!delayed) {
        return Result<std::vector<EncodedPacket>>::Failure(delayed.error());
    }

    std::vector<EncodedPacket> output;
    const int frame_size = impl_->context->frame_size > 0 ? impl_->context->frame_size : 1024;
    while (av_audio_fifo_size(impl_->fifo) > 0) {
        const int available = av_audio_fifo_size(impl_->fifo);
        const bool supports_small_last =
            (impl_->context->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) != 0;
        const int samples = supports_small_last ? std::min(available, frame_size) : frame_size;
        auto encoded = impl_->EncodeFromFifo(samples, !supports_small_last);
        if (!encoded) {
            return encoded;
        }
        auto& packets = encoded.value();
        output.insert(output.end(), std::make_move_iterator(packets.begin()),
                      std::make_move_iterator(packets.end()));
    }
    const int sent = avcodec_send_frame(impl_->context, nullptr);
    if (sent < 0 && sent != AVERROR_EOF) {
        return Result<std::vector<EncodedPacket>>::Failure(FfmpegError(
            "ffmpeg_audio_encoder", "flush", sent, "cannot start AAC drain"));
    }
    auto drained = ReceivePackets(impl_->context, StreamKind::kAudio, Codec::kAac,
                                  [this](std::int64_t) { return impl_->packet_sequence++; });
    if (!drained) {
        return drained;
    }
    auto& packets = drained.value();
    output.insert(output.end(), std::make_move_iterator(packets.begin()),
                  std::make_move_iterator(packets.end()));
    return Result<std::vector<EncodedPacket>>::Success(std::move(output));
}

void FfmpegAudioEncoder::Close() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->Close();
}

}  // namespace rkav
