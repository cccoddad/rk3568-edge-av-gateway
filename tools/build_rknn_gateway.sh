#!/usr/bin/env sh
# Cross-builds the C++ gateway with RKNN while keeping compatibility with board glibc 2.35.
set -eu

LC_ALL=C
export LC_ALL

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_build_dir=${RKAV_RKNN_BUILD_DIR:-"$rkav_project_root/build/cross-aarch64-rknn"}
rkav_output_dir="$rkav_project_root/out/aarch64-rknn-gateway"

if [ -z "${RKNN_SDK_ROOT:-}" ]; then
    echo "RKNN_SDK_ROOT must point to runtime/RK356X/Linux/librknn_api" >&2
    exit 2
fi

for rkav_command in cmake ninja aarch64-linux-gnu-g++ file readelf; do
    if ! command -v "$rkav_command" >/dev/null 2>&1; then
        echo "required command not found: $rkav_command" >&2
        exit 2
    fi
done

set -- cmake -S "$rkav_project_root" -B "$rkav_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$rkav_project_root/cmake/Toolchains/aarch64-linux-gnu-dynamic.cmake" \
    -DRKAV_BUILD_TESTS=OFF \
    -DRKAV_ENABLE_MOCK=ON \
    -DRKAV_WITH_V4L2=ON \
    -DRKAV_WITH_ALSA=ON \
    -DRKAV_WITH_JPEG=ON \
    -DRKAV_WITH_RKNN=ON \
    -DRKNN_SDK_ROOT="$RKNN_SDK_ROOT"
if [ -n "${RKAV_LIBJPEG_TURBO_SOURCE_DIR:-}" ]; then
    if [ ! -f "$RKAV_LIBJPEG_TURBO_SOURCE_DIR/CMakeLists.txt" ]; then
        echo "libjpeg-turbo source is incomplete: $RKAV_LIBJPEG_TURBO_SOURCE_DIR" >&2
        exit 2
    fi
    set -- "$@" \
        "-DFETCHCONTENT_SOURCE_DIR_LIBJPEG_TURBO=$RKAV_LIBJPEG_TURBO_SOURCE_DIR"
fi
"$@"
cmake --build "$rkav_build_dir" -j "${RKAV_BUILD_JOBS:-4}"

mkdir -p "$rkav_output_dir"
cp "$rkav_build_dir/rkav-gateway" "$rkav_output_dir/rkav-gateway"
cp "$rkav_project_root/config/rk3568-rknn-rgb.json" "$rkav_output_dir/rk3568-rknn-rgb.json"
cp "$rkav_project_root/config/rk3568-rknn-mjpeg.json" \
    "$rkav_output_dir/rk3568-rknn-mjpeg.json"
cp "$rkav_project_root/config/rk3568-rknn-mjpeg-alsa.json" \
    "$rkav_output_dir/rk3568-rknn-mjpeg-alsa.json"

file "$rkav_output_dir/rkav-gateway"
readelf -d "$rkav_output_dir/rkav-gateway" | grep 'Shared library'

if ! file "$rkav_output_dir/rkav-gateway" | grep -q 'ARM aarch64'; then
    echo "rkav-gateway is not an aarch64 executable" >&2
    exit 1
fi
if ! readelf -d "$rkav_output_dir/rkav-gateway" | grep -q 'librknnrt.so'; then
    echo "rkav-gateway does not depend on librknnrt.so" >&2
    exit 1
fi
if readelf -d "$rkav_output_dir/rkav-gateway" | grep -q 'libstdc++.so'; then
    echo "rkav-gateway unexpectedly depends on the board libstdc++" >&2
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
    sha256sum \
        rkav-gateway \
        rk3568-rknn-rgb.json \
        rk3568-rknn-mjpeg.json \
        rk3568-rknn-mjpeg-alsa.json \
        > SHA256SUMS
    sha256sum --check SHA256SUMS
)

printf 'rknn_gateway_build=passed\nglibc_ceiling=2.35\nartifacts=%s\n' "$rkav_output_dir"
