#!/usr/bin/env sh
# Offline AArch64 MPP/RGA candidate build. It never deploys to or starts the board.
set -eu

LC_ALL=C
export LC_ALL

require_file() {
    if [ ! -f "$1" ]; then
        echo "required file is missing: $1" >&2
        exit 2
    fi
}

require_directory() {
    if [ ! -d "$1" ]; then
        echo "required directory is missing: $1" >&2
        exit 2
    fi
}

run_logged() {
    rkav_step=$1
    shift
    echo "===== ${rkav_step} ====="
    rkav_status_file="$rkav_run_dir/.step-status-$$"
    set +e
    (
        "$@"
        rkav_command_rc=$?
        printf '%s\n' "$rkav_command_rc" >"$rkav_status_file"
        exit 0
    ) 2>&1 | tee -a "$rkav_log"
    rkav_tee_rc=$?
    set -e
    if [ ! -f "$rkav_status_file" ]; then
        echo "step did not report an exit code: ${rkav_step}" >&2
        exit 1
    fi
    rkav_rc=$(cat "$rkav_status_file")
    rm -f "$rkav_status_file"
    if [ "$rkav_tee_rc" -ne 0 ]; then
        echo "failed to write build output: ${rkav_step}" >&2
        exit "$rkav_tee_rc"
    fi
    if [ "$rkav_rc" -ne 0 ]; then
        echo "step_failed=${rkav_step}"
        echo "exit_code=${rkav_rc}"
        echo "build_log=$rkav_log"
        exit "$rkav_rc"
    fi
}

rkav_archive=${RKAV_SOURCE_ARCHIVE:?Set RKAV_SOURCE_ARCHIVE to the source archive.}
rkav_mpp_root=${RKAV_MPP_HEADERS_ROOT:?Set RKAV_MPP_HEADERS_ROOT.}
rkav_rga_root=${RKAV_RGA_HEADERS_ROOT:?Set RKAV_RGA_HEADERS_ROOT.}
rkav_rknn_root=${RKNN_SDK_ROOT:?Set RKNN_SDK_ROOT.}
rkav_json_root=${RKAV_NLOHMANN_JSON_SOURCE_DIR:?Set RKAV_NLOHMANN_JSON_SOURCE_DIR.}
rkav_jpeg_root=${RKAV_LIBJPEG_TURBO_SOURCE_DIR:?Set RKAV_LIBJPEG_TURBO_SOURCE_DIR.}
rkav_jobs=${RKAV_BUILD_JOBS:-4}

for rkav_command in tar cmake ninja aarch64-linux-gnu-g++ file readelf sha256sum; do
    command -v "$rkav_command" >/dev/null 2>&1 || {
        echo "required command not found: $rkav_command" >&2
        exit 2
    }
done

require_file "$rkav_archive"
require_file "$rkav_mpp_root/inc/rk_mpi.h"
require_file "$rkav_mpp_root/inc/rk_venc_cfg.h"
require_file "$rkav_rga_root/include/im2d.h"
require_file "$rkav_rknn_root/include/rknn_api.h"
require_file "$rkav_rknn_root/aarch64/librknnrt.so"
require_file "$rkav_json_root/CMakeLists.txt"
require_file "$rkav_jpeg_root/CMakeLists.txt"

rkav_run_dir=${RKAV_MPP_RGA_RUN_DIR:-"/tmp/rkav-mpp-rga-cross-$(date +%Y%m%d-%H%M%S)-$$"}
if [ -e "$rkav_run_dir" ]; then
    echo "refusing to reuse existing result directory: $rkav_run_dir" >&2
    exit 2
fi
mkdir -p "$rkav_run_dir"
rkav_source_dir="$rkav_run_dir/source"
rkav_build_dir="$rkav_run_dir/build"
rkav_output_dir="$rkav_run_dir/out"
rkav_log="$rkav_run_dir/build.log"

echo "===== Offline MPP/RGA AArch64 build ====="
echo "result_dir=$rkav_run_dir"
echo "source_archive=$rkav_archive"
sha256sum "$rkav_archive"

echo "===== Extract source into a new result directory ====="
mkdir -p "$rkav_source_dir"
tar -xzf "$rkav_archive" -C "$rkav_source_dir"
require_file "$rkav_source_dir/CMakeLists.txt"
require_file "$rkav_source_dir/tools/build_rknn_gateway.sh"

run_logged "Configure (offline dependencies)" \
    cmake -S "$rkav_source_dir" -B "$rkav_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$rkav_source_dir/cmake/Toolchains/aarch64-linux-gnu-dynamic.cmake" \
    -DRKAV_BUILD_TESTS=OFF \
    -DRKAV_ENABLE_MOCK=ON \
    -DRKAV_WITH_V4L2=ON \
    -DRKAV_WITH_ALSA=ON \
    -DRKAV_WITH_JPEG=ON \
    -DRKAV_WITH_RKNN=ON \
    -DRKAV_WITH_MPP=ON \
    -DRKAV_WITH_RGA=ON \
    -DRKNN_SDK_ROOT="$rkav_rknn_root" \
    -DRKAV_MPP_HEADERS_ROOT="$rkav_mpp_root" \
    -DRKAV_RGA_HEADERS_ROOT="$rkav_rga_root" \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$rkav_json_root" \
    -DFETCHCONTENT_SOURCE_DIR_LIBJPEG_TURBO="$rkav_jpeg_root"

run_logged "Build AArch64 gateway" \
    cmake --build "$rkav_build_dir" -j "$rkav_jobs"

require_file "$rkav_build_dir/rkav-gateway"
mkdir -p "$rkav_output_dir"
cp "$rkav_build_dir/rkav-gateway" "$rkav_output_dir/rkav-gateway"
cp "$rkav_source_dir/config/rk3568-rknn-mjpeg-alsa-mpp-h264.json" \
    "$rkav_output_dir/rk3568-rknn-mjpeg-alsa-mpp-h264.json"

echo "===== Validate generated artifact ====="
file "$rkav_output_dir/rkav-gateway"
readelf -d "$rkav_output_dir/rkav-gateway" | grep 'Shared library'
file "$rkav_output_dir/rkav-gateway" | grep -q 'ARM aarch64'
readelf -d "$rkav_output_dir/rkav-gateway" | grep -q 'librknnrt.so'
if readelf -d "$rkav_output_dir/rkav-gateway" | grep -q 'libstdc++.so'; then
    echo "rkav-gateway unexpectedly depends on target libstdc++" >&2
    exit 1
fi

rkav_glibc_versions=$(readelf --version-info "$rkav_output_dir/rkav-gateway" |
    grep -o 'GLIBC_[0-9][0-9.]*' | sort -Vu)
printf '%s\n' 'required_glibc_versions:' "$rkav_glibc_versions"
for rkav_glibc_version in $rkav_glibc_versions; do
    rkav_version=${rkav_glibc_version#GLIBC_}
    rkav_major=${rkav_version%%.*}
    rkav_remainder=${rkav_version#*.}
    rkav_minor=${rkav_remainder%%.*}
    if [ "$rkav_major" -gt 2 ] || {
        [ "$rkav_major" -eq 2 ] && [ "$rkav_minor" -gt 35 ]
    }; then
        echo "rkav-gateway requires $rkav_glibc_version, newer than board glibc 2.35" >&2
        exit 1
    fi
done

(
    cd "$rkav_output_dir"
    sha256sum rkav-gateway rk3568-rknn-mjpeg-alsa-mpp-h264.json > SHA256SUMS
    sha256sum --check SHA256SUMS
)

echo "mpp_rga_aarch64_build=passed"
echo "artifact_dir=$rkav_output_dir"
echo "build_log=$rkav_log"
