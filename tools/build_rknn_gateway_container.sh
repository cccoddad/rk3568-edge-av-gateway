#!/usr/bin/env sh
# Builds the RKNN gateway with an Ubuntu 22.04 sysroot that is compatible with board glibc 2.35.
set -eu

LC_ALL=C
export LC_ALL

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_container_engine=${RKAV_CONTAINER_ENGINE:-docker}
rkav_image=${RKAV_RKNN_BUILD_IMAGE:-rkav/aarch64-rknn-build:ubuntu22.04}

if [ -z "${RKNN_SDK_ROOT:-}" ]; then
    echo "RKNN_SDK_ROOT must point to runtime/RK356X/Linux/librknn_api" >&2
    exit 2
fi

if ! command -v "$rkav_container_engine" >/dev/null 2>&1; then
    echo "container engine not found: $rkav_container_engine" >&2
    exit 2
fi

if [ ! -f "$RKNN_SDK_ROOT/include/rknn_api.h" ] || \
    [ ! -f "$RKNN_SDK_ROOT/aarch64/librknnrt.so" ]; then
    echo "RKNN development files are incomplete under: $RKNN_SDK_ROOT" >&2
    exit 2
fi

case $(uname -m) in
    x86_64|amd64)
        ;;
    *)
        echo "the RKNN cross-build container requires an x86_64 host" >&2
        exit 2
        ;;
esac

rkav_uid=$(id -u)
rkav_gid=$(id -g)

"$rkav_container_engine" build \
    --file "$rkav_project_root/tools/docker/rknn-gateway-build.Dockerfile" \
    --tag "$rkav_image" \
    "$rkav_project_root/tools/docker"

"$rkav_container_engine" run --rm \
    --user "$rkav_uid:$rkav_gid" \
    --env HOME=/tmp/rkav-home \
    --env RKNN_SDK_ROOT=/opt/rknn-sdk \
    --env RKAV_RKNN_BUILD_DIR=/workspace/build/cross-aarch64-rknn-ubuntu22.04 \
    --env "RKAV_BUILD_JOBS=${RKAV_BUILD_JOBS:-4}" \
    --volume "$rkav_project_root:/workspace" \
    --volume "$RKNN_SDK_ROOT:/opt/rknn-sdk:ro" \
    --workdir /workspace \
    "$rkav_image" \
    sh ./tools/build_rknn_gateway.sh
