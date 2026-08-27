#!/usr/bin/env sh
# Builds and runs the hardware-independent RKNN smoke tests.
set -eu

LC_ALL=C
export LC_ALL

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_source_dir="$rkav_project_root/tools/rknn_smoke"
rkav_build_dir="$rkav_project_root/build/rknn-smoke-host-tests"

rkav_cc=${CC:-cc}
if ! command -v "$rkav_cc" >/dev/null 2>&1; then
    echo "C compiler not found: $rkav_cc" >&2
    exit 2
fi

mkdir -p "$rkav_build_dir"
"$rkav_cc" \
    -std=c17 \
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    "$rkav_source_dir/arguments.c" \
    "$rkav_source_dir/test_arguments.c" \
    -o "$rkav_build_dir/arguments-test"

"$rkav_build_dir/arguments-test"

"$rkav_cc" \
    -std=c17 \
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    "$rkav_source_dir/yolov5_postprocess.c" \
    "$rkav_source_dir/test_yolov5_postprocess.c" \
    -lm \
    -o "$rkav_build_dir/yolov5-postprocess-test"

"$rkav_build_dir/yolov5-postprocess-test"
