#!/usr/bin/env sh
# 文件作用：编排 RK3568 Mock M5 的非特权验收，并产出可审计的证据目录。
# 主要知识点：按阶段 fail-fast、Sanitizer、信号验证和长稳证据归档。
set -eu

rkav_soak_seconds=${1:-1800}
rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_output_dir="$rkav_project_root/out/m5-$(date +%Y%m%d-%H%M%S)"

case "$rkav_soak_seconds" in
    ''|*[!0-9]*) echo "soak duration must be a positive integer" >&2; exit 2 ;;
esac
if [ "$rkav_soak_seconds" -eq 0 ]; then
    echo "soak duration must be greater than zero" >&2
    exit 2
fi
if [ "$(uname -s)" != "Linux" ]; then
    echo "M5 validation must run on the RK3568 Linux board, not $(uname -s)" >&2
    exit 2
fi

mkdir -p "$rkav_output_dir"
sh "$rkav_script_dir/collect_baseline.sh" "$rkav_output_dir/baseline"
sh "$rkav_script_dir/build_and_test.sh" > "$rkav_output_dir/debug-build-and-test.log" 2>&1
RKAV_SIGNAL_OUTPUT_DIR="$rkav_output_dir/signal" sh "$rkav_script_dir/signal_test.sh" \
    > "$rkav_output_dir/signal-test.log" 2>&1

(
    cd "$rkav_project_root"
    cmake --preset asan
    cmake --build --preset asan -j "${RKAV_BUILD_JOBS:-4}"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        ctest --preset asan
) > "$rkav_output_dir/sanitizers.log" 2>&1
(
    cd "$rkav_project_root"
    cmake --preset release
    cmake --build --preset release -j "${RKAV_BUILD_JOBS:-4}"
) > "$rkav_output_dir/release-build.log" 2>&1
RKAV_OUTPUT_ROOT="$rkav_output_dir" sh "$rkav_script_dir/soak_test.sh" "$rkav_soak_seconds" \
    > "$rkav_output_dir/soak.log" 2>&1

printf 'm5_validation=passed\nartifacts=%s\n' "$rkav_output_dir" | tee "$rkav_output_dir/result.txt"
