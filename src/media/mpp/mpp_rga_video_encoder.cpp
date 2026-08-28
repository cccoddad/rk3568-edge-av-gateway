// 文件作用：使用 RK3568 RGA 把 RGB/BGR 转为 NV12，并通过 MPP 输出 H.264 Annex-B 包。
#include "rkav/media/mpp_rga_video_encoder.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

extern "C" {
#include <rk_mpi.h>
#include <rk_venc_cfg.h>
}
#include <im2d.h>

namespace rkav {
namespace {

Error MppRgaError(std::string_view operation, int code, std::string message) {
    return Error{ErrorCategory::kCodec, code, "mpp_rga_video_encoder", std::string(operation),
                 std::move(message), false};
}

Error DynamicLibraryError(std::string_view operation, std::string_view library) {
    const char* detail = dlerror();
    std::string message{"cannot load "};
    message += library;
    if (detail != nullptr) {
        message += ": ";
        message += detail;
    }
    return MppRgaError(operation, 0, std::move(message));
}

template <typename Function>
bool LoadSymbol(void* library, const char* name, Function* function) {
    dlerror();
    void* raw = dlsym(library, name);
    if (dlerror() != nullptr || raw == nullptr) {
        return false;
    }
    // POSIX dlsym 返回 void*；本项目的 Linux/AArch64 ABI 将函数指针与 void* 等宽。
    static_assert(sizeof(Function) == sizeof(raw));
    std::memcpy(function, &raw, sizeof(*function));
    return true;
}

int ToRgaFormat(PixelFormat format) {
    if (format == PixelFormat::kRgb888) {
        return RK_FORMAT_RGB_888;
    }
    if (format == PixelFormat::kBgr888) {
        return RK_FORMAT_BGR_888;
    }
    return RK_FORMAT_UNKNOWN;
}

// MPP 公共头只定义 packet 的通用 flag，未稳定公开 INTRA 位；H.264 Annex-B 的 IDR NAL 是
// 可跨该差异使用的关键帧证据。MPP 输出配置为每个 IDR 携带 SPS/PPS。
bool ContainsH264IdrNal(const Buffer& payload) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
    const std::size_t size = payload.size();
    for (std::size_t index = 0; index + 4U < size; ++index) {
        std::size_t nal_offset = 0;
        if (bytes[index] == 0U && bytes[index + 1U] == 0U && bytes[index + 2U] == 1U) {
            nal_offset = index + 3U;
        } else if (index + 4U < size && bytes[index] == 0U && bytes[index + 1U] == 0U &&
                   bytes[index + 2U] == 0U && bytes[index + 3U] == 1U) {
            nal_offset = index + 4U;
        }
        if (nal_offset != 0U && nal_offset < size && (bytes[nal_offset] & 0x1FU) == 5U) {
            return true;
        }
    }
    return false;
}

}  // namespace

struct MppRgaVideoEncoder::Impl {
    struct Symbols {
        void* mpp_library{nullptr};
        void* rga_library{nullptr};
        decltype(&mpp_create) create{nullptr};
        decltype(&mpp_init) init{nullptr};
        decltype(&mpp_destroy) destroy{nullptr};
        decltype(&mpp_enc_cfg_init) config_init{nullptr};
        decltype(&mpp_enc_cfg_deinit) config_deinit{nullptr};
        decltype(&mpp_enc_cfg_set_s32) config_set_s32{nullptr};
        decltype(&mpp_buffer_group_get) buffer_group_get{nullptr};
        decltype(&mpp_buffer_group_put) buffer_group_put{nullptr};
        decltype(&mpp_buffer_get_with_tag) buffer_get{nullptr};
        decltype(&mpp_buffer_put_with_caller) buffer_put{nullptr};
        decltype(&mpp_buffer_get_ptr_with_caller) buffer_ptr{nullptr};
        decltype(&mpp_frame_init) frame_init{nullptr};
        decltype(&mpp_frame_deinit) frame_deinit{nullptr};
        decltype(&mpp_frame_set_width) frame_set_width{nullptr};
        decltype(&mpp_frame_set_height) frame_set_height{nullptr};
        decltype(&mpp_frame_set_hor_stride) frame_set_hor_stride{nullptr};
        decltype(&mpp_frame_set_ver_stride) frame_set_ver_stride{nullptr};
        decltype(&mpp_frame_set_fmt) frame_set_fmt{nullptr};
        decltype(&mpp_frame_set_pts) frame_set_pts{nullptr};
        decltype(&mpp_frame_set_eos) frame_set_eos{nullptr};
        decltype(&mpp_frame_set_buffer) frame_set_buffer{nullptr};
        decltype(&mpp_packet_deinit) packet_deinit{nullptr};
        decltype(&mpp_packet_get_pos) packet_get_pos{nullptr};
        decltype(&mpp_packet_get_length) packet_get_length{nullptr};
        decltype(&mpp_packet_get_pts) packet_get_pts{nullptr};
        decltype(&mpp_packet_get_dts) packet_get_dts{nullptr};
        decltype(&mpp_packet_get_eos) packet_get_eos{nullptr};
        decltype(&wrapbuffer_virtualaddr_t) wrap_virtual_address{nullptr};
        decltype(&imcvtcolor_t) convert_color{nullptr};

        Result<void> Open() {
            mpp_library = dlopen("librockchip_mpp.so.0", RTLD_NOW | RTLD_LOCAL);
            if (mpp_library == nullptr) {
                return Result<void>::Failure(DynamicLibraryError("load_mpp", "librockchip_mpp.so.0"));
            }
            rga_library = dlopen("librga.so.2.1.0", RTLD_NOW | RTLD_LOCAL);
            if (rga_library == nullptr) {
                Close();
                return Result<void>::Failure(DynamicLibraryError("load_rga", "librga.so.2.1.0"));
            }
#define RKAV_LOAD_MPP(member, symbol) \
    if (!LoadSymbol(mpp_library, symbol, &member)) { \
        Close(); \
        return Result<void>::Failure(MppRgaError("load_mpp_symbol", 0, "missing MPP symbol: " symbol)); \
    }
            RKAV_LOAD_MPP(create, "mpp_create");
            RKAV_LOAD_MPP(init, "mpp_init");
            RKAV_LOAD_MPP(destroy, "mpp_destroy");
            RKAV_LOAD_MPP(config_init, "mpp_enc_cfg_init");
            RKAV_LOAD_MPP(config_deinit, "mpp_enc_cfg_deinit");
            RKAV_LOAD_MPP(config_set_s32, "mpp_enc_cfg_set_s32");
            RKAV_LOAD_MPP(buffer_group_get, "mpp_buffer_group_get");
            RKAV_LOAD_MPP(buffer_group_put, "mpp_buffer_group_put");
            RKAV_LOAD_MPP(buffer_get, "mpp_buffer_get_with_tag");
            RKAV_LOAD_MPP(buffer_put, "mpp_buffer_put_with_caller");
            RKAV_LOAD_MPP(buffer_ptr, "mpp_buffer_get_ptr_with_caller");
            RKAV_LOAD_MPP(frame_init, "mpp_frame_init");
            RKAV_LOAD_MPP(frame_deinit, "mpp_frame_deinit");
            RKAV_LOAD_MPP(frame_set_width, "mpp_frame_set_width");
            RKAV_LOAD_MPP(frame_set_height, "mpp_frame_set_height");
            RKAV_LOAD_MPP(frame_set_hor_stride, "mpp_frame_set_hor_stride");
            RKAV_LOAD_MPP(frame_set_ver_stride, "mpp_frame_set_ver_stride");
            RKAV_LOAD_MPP(frame_set_fmt, "mpp_frame_set_fmt");
            RKAV_LOAD_MPP(frame_set_pts, "mpp_frame_set_pts");
            RKAV_LOAD_MPP(frame_set_eos, "mpp_frame_set_eos");
            RKAV_LOAD_MPP(frame_set_buffer, "mpp_frame_set_buffer");
            RKAV_LOAD_MPP(packet_deinit, "mpp_packet_deinit");
            RKAV_LOAD_MPP(packet_get_pos, "mpp_packet_get_pos");
            RKAV_LOAD_MPP(packet_get_length, "mpp_packet_get_length");
            RKAV_LOAD_MPP(packet_get_pts, "mpp_packet_get_pts");
            RKAV_LOAD_MPP(packet_get_dts, "mpp_packet_get_dts");
            RKAV_LOAD_MPP(packet_get_eos, "mpp_packet_get_eos");
#undef RKAV_LOAD_MPP
#define RKAV_LOAD_RGA(member, symbol) \
    if (!LoadSymbol(rga_library, symbol, &member)) { \
        Close(); \
        return Result<void>::Failure(MppRgaError("load_rga_symbol", 0, "missing RGA symbol: " symbol)); \
    }
            RKAV_LOAD_RGA(wrap_virtual_address, "wrapbuffer_virtualaddr_t");
            RKAV_LOAD_RGA(convert_color, "imcvtcolor_t");
#undef RKAV_LOAD_RGA
            return Result<void>::Success();
        }

        void Close() noexcept {
            if (rga_library != nullptr) {
                dlclose(rga_library);
                rga_library = nullptr;
            }
            if (mpp_library != nullptr) {
                dlclose(mpp_library);
                mpp_library = nullptr;
            }
        }
    } symbols;

    std::mutex mutex;
    VideoCapabilities input;
    MppCtx context{nullptr};
    MppApi* api{nullptr};
    MppEncCfg config{nullptr};
    MppBufferGroup buffer_group{nullptr};
    MppBuffer nv12_buffer{nullptr};
    std::map<std::int64_t, std::uint64_t> sequences_by_pts;
    int source_rga_format{RK_FORMAT_UNKNOWN};
    int width{0};
    int height{0};
    int stride{0};
    bool open{false};
    bool flushed{false};

    void Close() noexcept {
        if (context != nullptr) {
            symbols.destroy(context);
            context = nullptr;
            api = nullptr;
        }
        if (config != nullptr) {
            symbols.config_deinit(config);
            config = nullptr;
        }
        if (nv12_buffer != nullptr) {
            symbols.buffer_put(nv12_buffer, __func__);
            nv12_buffer = nullptr;
        }
        if (buffer_group != nullptr) {
            symbols.buffer_group_put(buffer_group);
            buffer_group = nullptr;
        }
        sequences_by_pts.clear();
        open = false;
        flushed = false;
        symbols.Close();
    }
};

MppRgaVideoEncoder::MppRgaVideoEncoder() : impl_(std::make_unique<Impl>()) {}
MppRgaVideoEncoder::~MppRgaVideoEncoder() { Close(); }

Result<EncodedStreamInfo> MppRgaVideoEncoder::Open(const VideoEncoderConfig& config,
                                                    const VideoCapabilities& input) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->open) {
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("open", 0, "MPP/RGA encoder is already open"));
    }
    if ((input.format != PixelFormat::kRgb888 && input.format != PixelFormat::kBgr888) ||
        input.width <= 0 || input.height <= 0 || input.fps <= 0 || (input.width % 2) != 0 ||
        (input.height % 2) != 0 || config.bitrate_bps <= 0 || config.gop_size <= 0) {
        return Result<EncodedStreamInfo>::Failure(MppRgaError(
            "open", 0, "MPP H.264 requires even-sized RGB888/BGR888 input and positive bitrate/GOP"));
    }
    auto libraries = impl_->symbols.Open();
    if (!libraries) {
        return Result<EncodedStreamInfo>::Failure(libraries.error());
    }
    impl_->input = input;
    impl_->width = input.width;
    impl_->height = input.height;
    impl_->stride = (input.width + 15) & ~15;
    impl_->source_rga_format = ToRgaFormat(input.format);

    MPP_RET result = impl_->symbols.buffer_group_get(&impl_->buffer_group, MPP_BUFFER_TYPE_ION,
                                                       MPP_BUFFER_INTERNAL, "rkav", __func__);
    if (result != MPP_OK) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("allocate_buffer_group", result, "cannot allocate MPP ION buffer group"));
    }
    const std::size_t nv12_size = static_cast<std::size_t>(impl_->stride) *
                                  static_cast<std::size_t>(impl_->height) * 3U / 2U;
    result = impl_->symbols.buffer_get(impl_->buffer_group, &impl_->nv12_buffer, nv12_size, "rkav",
                                        __func__);
    if (result != MPP_OK || impl_->symbols.buffer_ptr(impl_->nv12_buffer, __func__) == nullptr) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("allocate_nv12_buffer", result, "cannot allocate writable MPP NV12 buffer"));
    }
    result = impl_->symbols.create(&impl_->context, &impl_->api);
    if (result != MPP_OK || impl_->api == nullptr) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("create_context", result, "mpp_create did not return an encoder API"));
    }
    result = impl_->symbols.init(impl_->context, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (result != MPP_OK) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("initialize_context", result, "cannot initialize MPP H.264 encoder"));
    }
    RK_S64 timeout_ms = 0;
    result = impl_->api->control(impl_->context, MPP_SET_INPUT_TIMEOUT, &timeout_ms);
    if (result == MPP_OK) {
        result = impl_->api->control(impl_->context, MPP_SET_OUTPUT_TIMEOUT, &timeout_ms);
    }
    if (result != MPP_OK) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("configure_timeout", result, "cannot configure nonblocking MPP I/O"));
    }
    result = impl_->symbols.config_init(&impl_->config);
    if (result != MPP_OK) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("create_config", result, "cannot allocate MPP encoder config"));
    }
    result = impl_->api->control(impl_->context, MPP_ENC_GET_CFG, impl_->config);
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "prep:width", impl_->width);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "prep:height", impl_->height);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "prep:hor_stride", impl_->stride);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "prep:ver_stride", impl_->height);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "prep:format", MPP_FMT_YUV420SP);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:mode", MPP_ENC_RC_MODE_CBR);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:bps_target", config.bitrate_bps);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:bps_max", config.bitrate_bps * 3 / 2);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:bps_min", config.bitrate_bps / 2);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:fps_in_num", input.fps);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:fps_in_denom", 1);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:fps_out_num", input.fps);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:fps_out_denom", 1);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "rc:gop", config.gop_size);
    }
    if (result == MPP_OK) {
        result = impl_->symbols.config_set_s32(impl_->config, "codec:type", MPP_VIDEO_CodingAVC);
    }
    if (result == MPP_OK) {
        result = impl_->api->control(impl_->context, MPP_ENC_SET_CFG, impl_->config);
    }
    if (result == MPP_OK) {
        MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        result = impl_->api->control(impl_->context, MPP_ENC_SET_HEADER_MODE, &header_mode);
    }
    if (result != MPP_OK) {
        impl_->Close();
        return Result<EncodedStreamInfo>::Failure(
            MppRgaError("configure_encoder", result, "MPP rejected H.264 encoder configuration"));
    }
    impl_->open = true;
    impl_->flushed = false;
    EncodedStreamInfo info;
    info.kind = StreamKind::kVideo;
    info.codec = Codec::kH264;
    info.time_base = Rational{1, 1'000'000};
    info.width = input.width;
    info.height = input.height;
    info.bit_rate = config.bitrate_bps;
    return Result<EncodedStreamInfo>::Success(std::move(info));
}

Result<std::vector<EncodedPacket>> MppRgaVideoEncoder::Encode(const VideoFrame& frame) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open || impl_->flushed) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("encode", 0, "MPP/RGA encoder is not accepting frames"));
    }
    auto validation = ValidateVideoFrame(frame);
    if (!validation) {
        return Result<std::vector<EncodedPacket>>::Failure(validation.error());
    }
    if (frame.width != impl_->width || frame.height != impl_->height ||
        frame.format != impl_->input.format) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("encode", 0, "video frame does not match negotiated MPP input"));
    }
    void* destination = impl_->symbols.buffer_ptr(impl_->nv12_buffer, __func__);
    if (destination == nullptr) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("convert", 0, "MPP NV12 buffer is not mapped"));
    }
    const rga_buffer_t source = impl_->symbols.wrap_virtual_address(
        frame.buffer->data(), frame.width, frame.height, frame.stride, frame.height,
        impl_->source_rga_format);
    const rga_buffer_t target = impl_->symbols.wrap_virtual_address(
        destination, impl_->width, impl_->height, impl_->stride, impl_->height,
        RK_FORMAT_YCbCr_420_SP);
    const IM_STATUS conversion = impl_->symbols.convert_color(
        source, target, impl_->source_rga_format, RK_FORMAT_YCbCr_420_SP,
        IM_COLOR_SPACE_DEFAULT, 1);
    if (conversion != IM_STATUS_SUCCESS) {
        return Result<std::vector<EncodedPacket>>::Failure(MppRgaError(
            "rga_convert", conversion, "RGA RGB/BGR to NV12 conversion failed"));
    }
    MppFrame mpp_frame{nullptr};
    MPP_RET result = impl_->symbols.frame_init(&mpp_frame);
    if (result != MPP_OK) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("create_frame", result, "cannot create MPP input frame"));
    }
    impl_->symbols.frame_set_width(mpp_frame, static_cast<RK_U32>(impl_->width));
    impl_->symbols.frame_set_height(mpp_frame, static_cast<RK_U32>(impl_->height));
    impl_->symbols.frame_set_hor_stride(mpp_frame, static_cast<RK_U32>(impl_->stride));
    impl_->symbols.frame_set_ver_stride(mpp_frame, static_cast<RK_U32>(impl_->height));
    impl_->symbols.frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
    impl_->symbols.frame_set_pts(mpp_frame, frame.pts_us);
    impl_->symbols.frame_set_buffer(mpp_frame, impl_->nv12_buffer);
    result = impl_->api->encode_put_frame(impl_->context, mpp_frame);
    impl_->symbols.frame_deinit(&mpp_frame);
    if (result != MPP_OK) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("submit_frame", result, "MPP rejected the converted NV12 frame"));
    }
    impl_->sequences_by_pts[frame.pts_us] = frame.sequence;
    std::vector<EncodedPacket> output;
    while (true) {
        MppPacket packet{nullptr};
        result = impl_->api->encode_get_packet(impl_->context, &packet);
        if (result == MPP_ERR_TIMEOUT || packet == nullptr) {
            break;
        }
        if (result != MPP_OK) {
            return Result<std::vector<EncodedPacket>>::Failure(
                MppRgaError("receive_packet", result, "MPP failed while reading H.264 output"));
        }
        const std::size_t length = impl_->symbols.packet_get_length(packet);
        void* position = impl_->symbols.packet_get_pos(packet);
        if (length == 0U || position == nullptr) {
            impl_->symbols.packet_deinit(&packet);
            return Result<std::vector<EncodedPacket>>::Failure(
                MppRgaError("receive_packet", 0, "MPP returned an empty H.264 packet"));
        }
        auto payload = Buffer::Allocate(length);
        std::memcpy(payload->data(), position, length);
        const std::int64_t pts = impl_->symbols.packet_get_pts(packet);
        const auto found = impl_->sequences_by_pts.find(pts);
        EncodedPacket encoded;
        encoded.kind = StreamKind::kVideo;
        encoded.source_sequence = found == impl_->sequences_by_pts.end() ? frame.sequence : found->second;
        encoded.pts = pts;
        encoded.dts = impl_->symbols.packet_get_dts(packet);
        encoded.duration = 1'000'000 / impl_->input.fps;
        encoded.time_base = Rational{1, 1'000'000};
        encoded.key_frame = ContainsH264IdrNal(*payload);
        encoded.codec = Codec::kH264;
        encoded.buffer = std::move(payload);
        impl_->symbols.packet_deinit(&packet);
        output.push_back(std::move(encoded));
    }
    return Result<std::vector<EncodedPacket>>::Success(std::move(output));
}

Result<std::vector<EncodedPacket>> MppRgaVideoEncoder::Flush() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->open) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("flush", 0, "MPP/RGA encoder is not open"));
    }
    if (impl_->flushed) {
        return Result<std::vector<EncodedPacket>>::Success({});
    }
    MppFrame eos_frame{nullptr};
    MPP_RET result = impl_->symbols.frame_init(&eos_frame);
    if (result != MPP_OK) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("flush", result, "cannot create MPP end-of-stream frame"));
    }
    impl_->symbols.frame_set_eos(eos_frame, 1U);
    result = impl_->api->encode_put_frame(impl_->context, eos_frame);
    impl_->symbols.frame_deinit(&eos_frame);
    if (result != MPP_OK) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("flush", result, "MPP rejected the end-of-stream frame"));
    }
    std::vector<EncodedPacket> output;
    bool saw_eos = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!saw_eos && std::chrono::steady_clock::now() < deadline) {
        MppPacket packet{nullptr};
        result = impl_->api->encode_get_packet(impl_->context, &packet);
        if (result == MPP_ERR_TIMEOUT || packet == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (result != MPP_OK) {
            return Result<std::vector<EncodedPacket>>::Failure(
                MppRgaError("flush", result, "MPP failed while draining H.264 packets"));
        }
        const std::size_t length = impl_->symbols.packet_get_length(packet);
        void* position = impl_->symbols.packet_get_pos(packet);
        if (length == 0U || position == nullptr) {
            impl_->symbols.packet_deinit(&packet);
            return Result<std::vector<EncodedPacket>>::Failure(
                MppRgaError("flush", 0, "MPP returned an empty packet while draining"));
        }
        auto payload = Buffer::Allocate(length);
        std::memcpy(payload->data(), position, length);
        EncodedPacket encoded;
        encoded.kind = StreamKind::kVideo;
        encoded.pts = impl_->symbols.packet_get_pts(packet);
        encoded.dts = impl_->symbols.packet_get_dts(packet);
        const auto found = impl_->sequences_by_pts.find(encoded.pts);
        encoded.source_sequence = found == impl_->sequences_by_pts.end() ? 0U : found->second;
        encoded.duration = 1'000'000 / impl_->input.fps;
        encoded.time_base = Rational{1, 1'000'000};
        encoded.key_frame = ContainsH264IdrNal(*payload);
        encoded.codec = Codec::kH264;
        encoded.buffer = std::move(payload);
        saw_eos = impl_->symbols.packet_get_eos(packet) != 0U;
        impl_->symbols.packet_deinit(&packet);
        output.push_back(std::move(encoded));
    }
    if (!saw_eos) {
        return Result<std::vector<EncodedPacket>>::Failure(
            MppRgaError("flush", 0, "MPP did not emit end-of-stream within two seconds"));
    }
    impl_->flushed = true;
    return Result<std::vector<EncodedPacket>>::Success(std::move(output));
}

void MppRgaVideoEncoder::Close() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->Close();
}

}  // namespace rkav
