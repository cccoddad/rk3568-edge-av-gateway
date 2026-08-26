#!/usr/bin/env sh
# Cross-builds the dynamic RKNN smoke test and enforces the target board's glibc ceiling.
set -eu

# Keep file/readelf output stable because the validation below parses their English identifiers.
LC_ALL=C
export LC_ALL

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_source_dir="$rkav_project_root/tools/rknn_smoke"
rkav_build_dir="$rkav_project_root/build/rknn-smoke-aarch64"
rkav_output_dir="$rkav_project_root/out/aarch64-rknn-smoke"

if [ -z "${RKNN_SDK_ROOT:-}" ]; then
    echo "RKNN_SDK_ROOT must point to runtime/RK356X/Linux/librknn_api" >&2
    exit 2
fi

for rkav_command in cmake ninja aarch64-linux-gnu-gcc file readelf; do
    if ! command -v "$rkav_command" >/dev/null 2>&1; then
        echo "required command not found: $rkav_command" >&2
        exit 2
    fi
done

cmake -S "$rkav_source_dir" -B "$rkav_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$rkav_project_root/cmake/Toolchains/aarch64-linux-gnu-dynamic.cmake" \
    -DRKNN_SDK_ROOT="$RKNN_SDK_ROOT"
cmake --build "$rkav_build_dir" -j "${RKAV_BUILD_JOBS:-4}"

mkdir -p "$rkav_output_dir"
cp "$rkav_build_dir/rknn-smoke" "$rkav_output_dir/rknn-smoke"

file "$rkav_output_dir/rknn-smoke"
readelf -d "$rkav_output_dir/rknn-smoke" | grep 'Shared library'
if ! file "$rkav_output_dir/rknn-smoke" | grep -q 'ARM aarch64'; then
    echo "rknn-smoke is not an aarch64 executable" >&2
    exit 1
fi
if ! readelf -d "$rkav_output_dir/rknn-smoke" | grep -q 'librknnrt.so'; then
    echo "rknn-smoke does not depend on librknnrt.so" >&2
    exit 1
fi

rkav_glibc_versions=$(readelf --version-info "$rkav_output_dir/rknn-smoke" |
    grep -o 'GLIBC_[0-9][0-9.]*' | sort -Vu)
printf '%s\n' 'required_glibc_versions:' "$rkav_glibc_versions"
for rkav_glibc_version in $rkav_glibc_versions; do
    rkav_version=${rkav_glibc_version#GLIBC_}
    rkav_major=${rkav_version%%.*}
    rkav_remainder=${rkav_version#*.}
    rkav_minor=${rkav_remainder%%.*}
    if [ "$rkav_major" -gt 2 ] || { [ "$rkav_major" -eq 2 ] && [ "$rkav_minor" -gt 35 ]; }; then
        echo "rknn-smoke requires $rkav_glibc_version, newer than board glibc 2.35" >&2
        exit 1
    fi
done

printf 'rknn_smoke_build=passed\nglibc_ceiling=2.35\nartifact=%s\n' \
    "$rkav_output_dir/rknn-smoke"
