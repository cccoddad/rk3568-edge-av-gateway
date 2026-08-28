#!/usr/bin/env sh
# Builds the native FFmpeg feature and records a unique mock H.264/AAC MP4 evidence directory.
set -eu

LC_ALL=C
export LC_ALL

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_build_dir=${RKAV_FFMPEG_BUILD_DIR:-"$rkav_project_root/build/ffmpeg-native"}
rkav_result_dir=${RKAV_FFMPEG_RESULT_DIR:-"$rkav_project_root/out/ffmpeg-baseline-$(date +%Y%m%d-%H%M%S)-$$"}

for rkav_command in cmake ninja pkg-config ffmpeg ffprobe jq sha256sum; do
    if ! command -v "$rkav_command" >/dev/null 2>&1; then
        echo "required command not found: $rkav_command" >&2
        exit 2
    fi
done
for rkav_module in libavcodec libavformat libavutil libswscale libswresample; do
    if ! pkg-config --exists "$rkav_module"; then
        echo "FFmpeg development module not found: $rkav_module" >&2
        exit 2
    fi
done
if ! ffmpeg -hide_banner -encoders 2>/dev/null | grep -Eq '[[:space:]]libx264[[:space:]]'; then
    echo "the installed FFmpeg build does not provide the libx264 encoder" >&2
    exit 2
fi

mkdir -p "$rkav_result_dir"
jq --arg path "$rkav_result_dir/software-baseline.mp4" \
    '.outputs[0].path = $path' \
    "$rkav_project_root/config/mock-ffmpeg-mp4.json" \
    > "$rkav_result_dir/config.json"

{
    ffmpeg -version | sed -n '1,3p'
    for rkav_module in libavcodec libavformat libavutil libswscale libswresample; do
        printf '%s=' "$rkav_module"
        pkg-config --modversion "$rkav_module"
    done
} > "$rkav_result_dir/dependencies.txt"

cmake -S "$rkav_project_root" -B "$rkav_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DRKAV_BUILD_TESTS=ON \
    -DRKAV_ENABLE_MOCK=ON \
    -DRKAV_WITH_JPEG=OFF \
    -DRKAV_WITH_FFMPEG=ON \
    -DRKAV_WITH_V4L2=OFF \
    -DRKAV_WITH_ALSA=OFF \
    -DRKAV_WITH_RKNN=OFF
cmake --build "$rkav_build_dir" -j "${RKAV_BUILD_JOBS:-4}"
ctest --test-dir "$rkav_build_dir" --output-on-failure --timeout 30 \
    > "$rkav_result_dir/ctest.log"

"$rkav_build_dir/rkav-gateway" --config "$rkav_result_dir/config.json" \
    > "$rkav_result_dir/gateway.log" 2>&1
test -f "$rkav_result_dir/software-baseline.mp4"
test ! -e "$rkav_result_dir/software-baseline.mp4.part"
sh "$rkav_project_root/tools/validate_mp4.sh" \
    "$rkav_result_dir/software-baseline.mp4" 5 0.5 \
    > "$rkav_result_dir/ffprobe-validation.log"

sha256sum \
    "$rkav_build_dir/rkav-gateway" \
    "$rkav_result_dir/config.json" \
    "$rkav_result_dir/software-baseline.mp4" \
    "$rkav_result_dir/gateway.log" \
    "$rkav_result_dir/ffprobe-validation.log" \
    > "$rkav_result_dir/SHA256SUMS"
sha256sum --check "$rkav_result_dir/SHA256SUMS"

cat "$rkav_result_dir/ffprobe-validation.log"
printf '%s\n' \
    "ffmpeg_mock_mp4_baseline=passed" \
    "result_dir=$rkav_result_dir"
