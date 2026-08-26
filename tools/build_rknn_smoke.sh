#!/usr/bin/env sh
# Cross-builds the dynamic RKNN smoke test while keeping libstdc++ and libgcc self-contained.
set -eu

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_source_dir="$rkav_project_root/tools/rknn_smoke"
rkav_build_dir="$rkav_project_root/build/rknn-smoke-aarch64"
rkav_output_dir="$rkav_project_root/out/aarch64-rknn-smoke"

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

printf 'rknn_smoke_build=passed\nartifact=%s\n' "$rkav_output_dir/rknn-smoke"
