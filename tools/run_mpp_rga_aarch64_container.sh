#!/usr/bin/env sh
# Builds the MPP/RGA candidate with the compatible Ubuntu 22.04 container.
# This script never deploys to or starts the RK3568 board.
set -eu

LC_ALL=C
export LC_ALL

require_file() {
    if [ ! -f "$1" ]; then
        echo "required file is missing: $1" >&2
        exit 2
    fi
}

rkav_archive=${RKAV_SOURCE_ARCHIVE:?Set RKAV_SOURCE_ARCHIVE to the source archive.}
rkav_archive_sha256=${RKAV_SOURCE_SHA256:-}
rkav_mpp_root=${RKAV_MPP_HEADERS_ROOT:?Set RKAV_MPP_HEADERS_ROOT.}
rkav_rga_root=${RKAV_RGA_HEADERS_ROOT:?Set RKAV_RGA_HEADERS_ROOT.}
rkav_rknn_root=${RKNN_SDK_ROOT:?Set RKNN_SDK_ROOT.}
rkav_json_root=${RKAV_NLOHMANN_JSON_SOURCE_DIR:?Set RKAV_NLOHMANN_JSON_SOURCE_DIR.}
rkav_jpeg_root=${RKAV_LIBJPEG_TURBO_SOURCE_DIR:?Set RKAV_LIBJPEG_TURBO_SOURCE_DIR.}

for rkav_command in docker tar sha256sum tee; do
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

rkav_actual_sha256=$(sha256sum "$rkav_archive" | awk '{print $1}')
echo "source_archive_sha256=$rkav_actual_sha256"
if [ -n "$rkav_archive_sha256" ] && [ "$rkav_actual_sha256" != "$rkav_archive_sha256" ]; then
    echo "source archive SHA-256 does not match the expected value" >&2
    exit 1
fi

rkav_run_dir=${RKAV_MPP_RGA_RUN_DIR:-"/tmp/rkav-mpp-rga-container-$(date +%Y%m%d-%H%M%S)-$$"}
if [ -e "$rkav_run_dir" ]; then
    echo "refusing to reuse existing result directory: $rkav_run_dir" >&2
    exit 2
fi
mkdir -p "$rkav_run_dir/source"
tar -xzf "$rkav_archive" -C "$rkav_run_dir/source"
require_file "$rkav_run_dir/source/tools/build_rknn_gateway_container.sh"

rkav_log="$rkav_run_dir/container-build.log"
rkav_status_file="$rkav_run_dir/.container-status"
echo "result_dir=$rkav_run_dir"
echo "build_log=$rkav_log"
echo "===== Check Docker daemon ====="
docker info >/dev/null

echo "===== Build MPP/RGA candidate in Ubuntu 22.04 container ====="
set +e
(
    env \
        RKAV_ENABLE_MPP_RGA=1 \
        RKAV_BUILD_JOBS="${RKAV_BUILD_JOBS:-4}" \
        RKAV_MPP_HEADERS_ROOT="$rkav_mpp_root" \
        RKAV_RGA_HEADERS_ROOT="$rkav_rga_root" \
        RKNN_SDK_ROOT="$rkav_rknn_root" \
        RKAV_NLOHMANN_JSON_SOURCE_DIR="$rkav_json_root" \
        RKAV_LIBJPEG_TURBO_SOURCE_DIR="$rkav_jpeg_root" \
        sh "$rkav_run_dir/source/tools/build_rknn_gateway_container.sh"
    rkav_command_rc=$?
    printf '%s\n' "$rkav_command_rc" >"$rkav_status_file"
    exit 0
) 2>&1 | tee "$rkav_log"
rkav_tee_rc=$?
set -e

if [ ! -f "$rkav_status_file" ]; then
    echo "container build did not report an exit code" >&2
    exit 1
fi
rkav_command_rc=$(cat "$rkav_status_file")
rm -f "$rkav_status_file"
if [ "$rkav_tee_rc" -ne 0 ]; then
    echo "failed to write container build log" >&2
    exit "$rkav_tee_rc"
fi
if [ "$rkav_command_rc" -ne 0 ]; then
    echo "mpp_rga_container_build=failed"
    echo "exit_code=$rkav_command_rc"
    echo "build_log=$rkav_log"
    exit "$rkav_command_rc"
fi

rkav_artifact_dir="$rkav_run_dir/source/out/aarch64-rknn-mpp-rga-gateway-ubuntu22.04"
require_file "$rkav_artifact_dir/rkav-gateway"
require_file "$rkav_artifact_dir/SHA256SUMS"
echo "===== Container build result ====="
cat "$rkav_artifact_dir/SHA256SUMS"
echo "mpp_rga_container_build=passed"
echo "artifact_dir=$rkav_artifact_dir"
echo "build_log=$rkav_log"
