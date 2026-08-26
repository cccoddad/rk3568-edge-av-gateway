// 文件作用：以 Linux ALSA PCM 内核 UAPI 采集 UAC 麦克风的交错 S16_LE PCM。
// 主要知识点：无 libasound 静态部署、hw:C,D 到字符设备、poll、READI_FRAMES、XRUN 恢复。
#include "rkav/capture/alsa_audio_capture.h"

#include <fcntl.h>
#include <poll.h>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rkav {
namespace {

Error AlsaError(ErrorCategory category, std::string_view operation, std::string message,
                int native_code = 0, bool retryable = false) {
    if (native_code != 0) {
        message += ": " + std::error_code(native_code, std::generic_category()).message();
    }
    return Error{category,           native_code, "alsa_audio", std::string(operation),
                 std::move(message), retryable};
}

int IoctlRetry(int file_descriptor, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(file_descriptor, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

Result<std::string> DevicePath(std::string_view device) {
    if (device.starts_with("/dev/")) {
        return Result<std::string>::Success(std::string(device));
    }
    if (!device.starts_with("hw:")) {
        return Result<std::string>::Failure(
            AlsaError(ErrorCategory::kInvalidConfig, "device",
                      "expected ALSA device in hw:CARD,DEVICE form"));
    }
    const std::string_view values = device.substr(3);
    const std::size_t separator = values.find(',');
    if (separator == std::string_view::npos ||
        values.find(',', separator + 1U) != std::string_view::npos) {
        return Result<std::string>::Failure(AlsaError(ErrorCategory::kInvalidConfig, "device",
                                                      "expected exactly one comma in ALSA device"));
    }
    int card = -1;
    int pcm_device = -1;
    const auto [card_end, card_error] =
        std::from_chars(values.data(), values.data() + separator, card);
    const auto [pcm_end, pcm_error] =
        std::from_chars(values.data() + separator + 1U, values.data() + values.size(), pcm_device);
    if (card_error != std::errc{} || card_end != values.data() + separator ||
        pcm_error != std::errc{} || pcm_end != values.data() + values.size() || card < 0 ||
        pcm_device < 0) {
        return Result<std::string>::Failure(AlsaError(ErrorCategory::kInvalidConfig, "device",
                                                      "invalid ALSA hw:CARD,DEVICE value"));
    }
    return Result<std::string>::Success("/dev/snd/pcmC" + std::to_string(card) + "D" +
                                        std::to_string(pcm_device) + "c");
}

void InitializeAny(struct snd_pcm_hw_params& parameters) {
    for (auto& mask : parameters.masks) {
        for (auto& word : mask.bits) {
            word = ~0U;
        }
    }
    for (auto& interval : parameters.intervals) {
        interval.min = 0U;
        interval.max = std::numeric_limits<unsigned int>::max();
        interval.openmin = 0U;
        interval.openmax = 0U;
        interval.integer = 0U;
        interval.empty = 0U;
    }
    parameters.rmask = ~0U;
    parameters.cmask = 0U;
    parameters.info = ~0U;
}

void SetMask(struct snd_pcm_hw_params& parameters, int parameter, unsigned int value) {
    const auto index = static_cast<std::size_t>(parameter - SNDRV_PCM_HW_PARAM_FIRST_MASK);
    for (auto& word : parameters.masks[index].bits) {
        word = 0U;
    }
    parameters.masks[index].bits[value / 32U] = 1U << (value % 32U);
    parameters.cmask |= 1U << static_cast<unsigned int>(parameter);
    parameters.rmask |= 1U << static_cast<unsigned int>(parameter);
}

void SetInterval(struct snd_pcm_hw_params& parameters, int parameter, unsigned int value) {
    const auto index = static_cast<std::size_t>(parameter - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL);
    struct snd_interval& interval = parameters.intervals[index];
    interval.min = value;
    interval.max = value;
    interval.openmin = 0U;
    interval.openmax = 0U;
    interval.integer = 1U;
    interval.empty = 0U;
    parameters.cmask |= 1U << static_cast<unsigned int>(parameter);
    parameters.rmask |= 1U << static_cast<unsigned int>(parameter);
}

int IntervalMinimum(const struct snd_pcm_hw_params& parameters, int parameter) {
    const auto index = static_cast<std::size_t>(parameter - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL);
    const unsigned int value = parameters.intervals[index].min;
    return value <= static_cast<unsigned int>(std::numeric_limits<int>::max())
               ? static_cast<int>(value)
               : 0;
}

Error ReadError(std::string_view operation, int native_code) {
    if (native_code == EPIPE || native_code == ESTRPIPE) {
        return AlsaError(ErrorCategory::kXrun, operation, "ALSA capture overrun", native_code,
                         true);
    }
    if (native_code == ENODEV || native_code == ENXIO) {
        return AlsaError(ErrorCategory::kDeviceDisconnected, operation,
                         "ALSA capture device disconnected", native_code, true);
    }
    return AlsaError(ErrorCategory::kIo, operation, "ALSA PCM operation failed", native_code, true);
}

Error PollError(int file_descriptor) {
    struct snd_pcm_status status {};
    if (IoctlRetry(file_descriptor, SNDRV_PCM_IOCTL_STATUS, &status) < 0) {
        const int native_code = errno;
        if (native_code == ENODEV || native_code == ENXIO || native_code == EIO) {
            return AlsaError(ErrorCategory::kDeviceDisconnected, "poll",
                             "ALSA capture device disconnected", native_code, true);
        }
        return ReadError("query_status", native_code);
    }
    if (status.state == SNDRV_PCM_STATE_DISCONNECTED) {
        return AlsaError(ErrorCategory::kDeviceDisconnected, "poll",
                         "ALSA capture device disconnected", ENODEV, true);
    }
    if (status.state == SNDRV_PCM_STATE_XRUN) {
        return AlsaError(ErrorCategory::kXrun, "poll", "ALSA capture overrun", EPIPE, true);
    }
    if (status.state == SNDRV_PCM_STATE_SUSPENDED) {
        return AlsaError(ErrorCategory::kXrun, "poll", "ALSA capture suspended", ESTRPIPE, true);
    }
    return AlsaError(ErrorCategory::kIo, "poll", "ALSA device reported an unexpected poll error",
                     0, true);
}

}  // namespace

AlsaAudioCapture::AlsaAudioCapture(std::shared_ptr<IClock> clock) : clock_(std::move(clock)) {}

AlsaAudioCapture::~AlsaAudioCapture() { Close(); }

Result<AudioCapabilities> AlsaAudioCapture::Open(const AudioConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<AudioCapabilities>::Failure(
            AlsaError(ErrorCategory::kInvalidState, "open", "capture device is already open"));
    }
    auto device_path = DevicePath(config.device);
    if (!device_path) {
        return Result<AudioCapabilities>::Failure(device_path.error());
    }
    file_descriptor_ = ::open(device_path.value().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (file_descriptor_ < 0) {
        const int native_code = errno;
        const ErrorCategory category =
            native_code == ENOENT ? ErrorCategory::kDeviceNotFound : ErrorCategory::kIo;
        return Result<AudioCapabilities>::Failure(
            AlsaError(category, "open", "cannot open " + device_path.value(), native_code, true));
    }
    const auto fail_and_close = [this](Error error) {
        CloseUnlocked();
        return Result<AudioCapabilities>::Failure(std::move(error));
    };

    struct snd_pcm_info info {};
    info.stream = SNDRV_PCM_STREAM_CAPTURE;
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_INFO, &info) < 0) {
        return fail_and_close(ReadError("query_info", errno));
    }

    const int requested_samples = config.sample_rate * config.frame_duration_ms / 1000;
    struct snd_pcm_hw_params parameters {};
    InitializeAny(parameters);
    SetMask(parameters, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    SetMask(parameters, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    SetMask(parameters, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    SetInterval(parameters, SNDRV_PCM_HW_PARAM_CHANNELS,
                static_cast<unsigned int>(config.channels));
    SetInterval(parameters, SNDRV_PCM_HW_PARAM_RATE, static_cast<unsigned int>(config.sample_rate));
    SetInterval(parameters, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
                static_cast<unsigned int>(requested_samples));
    SetInterval(parameters, SNDRV_PCM_HW_PARAM_PERIODS, 4U);
    // 先让驱动根据请求收敛所有派生约束，再提交参数。直接 HW_PARAMS 会被部分 UAC 驱动拒绝。
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_HW_REFINE, &parameters) < 0) {
        return fail_and_close(ReadError("refine_hardware_parameters", errno));
    }
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_HW_PARAMS, &parameters) < 0) {
        return fail_and_close(ReadError("set_hardware_parameters", errno));
    }

    const int sample_rate = IntervalMinimum(parameters, SNDRV_PCM_HW_PARAM_RATE);
    const int channels = IntervalMinimum(parameters, SNDRV_PCM_HW_PARAM_CHANNELS);
    const int samples_per_frame = IntervalMinimum(parameters, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    const int periods = IntervalMinimum(parameters, SNDRV_PCM_HW_PARAM_PERIODS);
    if (sample_rate <= 0 || channels <= 0 || samples_per_frame <= 0 || periods <= 0) {
        return fail_and_close(AlsaError(ErrorCategory::kNotSupported, "set_hardware_parameters",
                                        "driver returned invalid PCM parameters"));
    }
    const auto period_frames = static_cast<snd_pcm_uframes_t>(samples_per_frame);
    const auto period_count = static_cast<snd_pcm_uframes_t>(periods);
    if (period_frames > std::numeric_limits<snd_pcm_uframes_t>::max() / period_count) {
        return fail_and_close(AlsaError(ErrorCategory::kResourceExhausted,
                                        "set_hardware_parameters", "PCM buffer size overflow"));
    }
    const snd_pcm_uframes_t buffer_frames = period_frames * period_count;

    struct snd_pcm_sw_params software {};
    software.period_step = 1U;
    software.avail_min = period_frames;
    software.start_threshold = 1U;
    software.stop_threshold = buffer_frames;
    software.xfer_align = 1U;
    software.boundary = buffer_frames;
    const auto maximum_boundary =
        static_cast<snd_pcm_uframes_t>(std::numeric_limits<snd_pcm_sframes_t>::max());
    while (software.boundary <= (maximum_boundary - buffer_frames) / 2U) {
        software.boundary *= 2U;
    }
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_SW_PARAMS, &software) < 0) {
        return fail_and_close(ReadError("set_software_parameters", errno));
    }
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_PREPARE, nullptr) < 0) {
        return fail_and_close(ReadError("prepare", errno));
    }
    // 直接使用内核 UAPI 时没有 alsa-lib 帮助自动触发 Capture，必须显式开始数据流。
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_START, nullptr) < 0) {
        return fail_and_close(ReadError("start", errno));
    }

    config_ = config;
    capabilities_ =
        AudioCapabilities{sample_rate, channels, samples_per_frame, SampleFormat::kS16LE};
    sequence_ = 0;
    next_pts_us_ = clock_->NowUs();
    open_ = true;
    return Result<AudioCapabilities>::Success(capabilities_);
}

Result<AudioFrame> AlsaAudioCapture::Read(std::stop_token stop) {
    if (stop.stop_requested()) {
        return Result<AudioFrame>::Failure(
            AlsaError(ErrorCategory::kCancelled, "read", "read was cancelled"));
    }
    std::scoped_lock lock(mutex_);
    if (!open_ || file_descriptor_ < 0) {
        if (stop.stop_requested()) {
            return Result<AudioFrame>::Failure(
                AlsaError(ErrorCategory::kCancelled, "read", "read was cancelled"));
        }
        return Result<AudioFrame>::Failure(
            AlsaError(ErrorCategory::kInvalidState, "read", "capture device is not open"));
    }
    const std::size_t frames = static_cast<std::size_t>(capabilities_.samples_per_frame);
    const std::size_t channels = static_cast<std::size_t>(capabilities_.channels);
    if (frames > std::numeric_limits<std::size_t>::max() / channels ||
        frames * channels > std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t)) {
        return Result<AudioFrame>::Failure(
            AlsaError(ErrorCategory::kResourceExhausted, "read", "PCM frame size overflow"));
    }
    std::shared_ptr<Buffer> buffer;
    try {
        buffer = Buffer::Allocate(frames * channels * sizeof(std::int16_t));
    } catch (const std::bad_alloc&) {
        return Result<AudioFrame>::Failure(
            AlsaError(ErrorCategory::kResourceExhausted, "read", "cannot allocate PCM buffer"));
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.capture_timeout_ms);
    std::size_t completed_frames = 0;
    while (completed_frames < frames && !stop.stop_requested()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return Result<AudioFrame>::Failure(AlsaError(
                ErrorCategory::kTimeout, "poll", "timed out waiting for PCM samples", 0, true));
        }
        const int poll_timeout = static_cast<int>(std::min<std::int64_t>(remaining.count(), 50));
        pollfd descriptor{file_descriptor_, POLLIN, 0};
        const int poll_result = ::poll(&descriptor, 1, poll_timeout);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Result<AudioFrame>::Failure(ReadError("poll", errno));
        }
        if (poll_result == 0) {
            continue;
        }
        if ((descriptor.revents & static_cast<short>(POLLHUP | POLLNVAL)) != 0) {
            return Result<AudioFrame>::Failure(AlsaError(ErrorCategory::kDeviceDisconnected, "poll",
                                                         "ALSA device reported a poll error", 0,
                                                         true));
        }
        if ((descriptor.revents & POLLERR) != 0) {
            return Result<AudioFrame>::Failure(PollError(file_descriptor_));
        }
        struct snd_xferi transfer {};
        transfer.buf =
            reinterpret_cast<std::int16_t*>(buffer->data()) + completed_frames * channels;
        transfer.frames = static_cast<snd_pcm_uframes_t>(frames - completed_frames);
        const int ioctl_result =
            IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_READI_FRAMES, &transfer);
        if (ioctl_result < 0) {
            const int native_code = errno;
            if (native_code == EAGAIN) {
                continue;
            }
            return Result<AudioFrame>::Failure(ReadError("read_interleaved", native_code));
        }
        const snd_pcm_sframes_t read_frames = transfer.result;
        if (read_frames < 0) {
            const auto positive_error = static_cast<unsigned long>(-(read_frames + 1)) + 1UL;
            if (positive_error > static_cast<unsigned long>(std::numeric_limits<int>::max())) {
                return Result<AudioFrame>::Failure(AlsaError(
                    ErrorCategory::kIo, "read_interleaved", "ALSA returned an invalid error"));
            }
            const int native_code = static_cast<int>(positive_error);
            if (native_code == EAGAIN) {
                continue;
            }
            return Result<AudioFrame>::Failure(ReadError("read_interleaved", native_code));
        }
        if (read_frames == 0) {
            continue;
        }
        const std::size_t received = static_cast<std::size_t>(read_frames);
        if (received > frames - completed_frames) {
            return Result<AudioFrame>::Failure(AlsaError(ErrorCategory::kIo, "read_interleaved",
                                                         "driver returned too many PCM frames"));
        }
        completed_frames += received;
    }
    if (stop.stop_requested()) {
        return Result<AudioFrame>::Failure(
            AlsaError(ErrorCategory::kCancelled, "read", "read was cancelled"));
    }

    const TimestampUs pts_us = next_pts_us_;
    const TimestampUs duration_us =
        static_cast<TimestampUs>(capabilities_.samples_per_frame) * 1'000'000 /
        capabilities_.sample_rate;
    next_pts_us_ += duration_us;
    AudioFrame frame{sequence_++,
                     pts_us,
                     capabilities_.sample_rate,
                     capabilities_.channels,
                     capabilities_.samples_per_frame,
                     capabilities_.format,
                     std::move(buffer)};
    return Result<AudioFrame>::Success(std::move(frame));
}

Result<void> AlsaAudioCapture::Recover() {
    std::scoped_lock lock(mutex_);
    if (!open_ || file_descriptor_ < 0) {
        return Result<void>::Failure(
            AlsaError(ErrorCategory::kInvalidState, "recover", "capture device is not open"));
    }
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_PREPARE, nullptr) < 0) {
        return Result<void>::Failure(ReadError("recover", errno));
    }
    if (IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_START, nullptr) < 0) {
        return Result<void>::Failure(ReadError("restart", errno));
    }
    return Result<void>::Success();
}

void AlsaAudioCapture::Close() noexcept {
    std::scoped_lock lock(mutex_);
    CloseUnlocked();
}

void AlsaAudioCapture::CloseUnlocked() noexcept {
    if (file_descriptor_ >= 0) {
        static_cast<void>(IoctlRetry(file_descriptor_, SNDRV_PCM_IOCTL_DROP, nullptr));
        static_cast<void>(::close(file_descriptor_));
    }
    file_descriptor_ = -1;
    sequence_ = 0;
    next_pts_us_ = 0;
    open_ = false;
}

}  // namespace rkav
