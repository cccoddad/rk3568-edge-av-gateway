#!/usr/bin/env sh
# Cross-builds a static FFmpeg prefix for aarch64 inside the rkav Ubuntu 22.04 build image.
# The gateway build mounts <RKAV_FFMPEG_BUILD_DIR>/prefix read-only at /opt/ffmpeg.
# Usage: RKAV_FFMPEG_BUILD_DIR=/path/to/ffmpeg-build sh tools/build_ffmpeg_aarch64_container.sh
set -eu

LC_ALL=C
export LC_ALL

rkav_build_dir=${RKAV_FFMPEG_BUILD_DIR:?Set RKAV_FFMPEG_BUILD_DIR to a host build directory.}
rkav_image=${RKAV_RKNN_BUILD_IMAGE:-rkav/aarch64-rknn-build:ubuntu22.04}
rkav_engine=${RKAV_CONTAINER_ENGINE:-docker}
rkav_ref=${RKAV_FFMPEG_GIT_REF:-n6.1.1}

for rkav_command in "$rkav_engine" mkdir; do
    command -v "$rkav_command" >/dev/null 2>&1 || {
        echo "required command not found: $rkav_command" >&2
        exit 2
    }
done

mkdir -p "$rkav_build_dir/prefix"

cat > "$rkav_build_dir/build_ffmpeg.sh" <<EOS
#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq make >/dev/null

if [ ! -d /work/src/.git ]; then
    git clone --depth 1 --branch $rkav_ref https://gitee.com/mirrors/ffmpeg.git /work/src
fi

cd /work/src
./configure \\
    --prefix=/work/prefix \\
    --enable-cross-compile \\
    --cross-prefix=aarch64-linux-gnu- \\
    --arch=aarch64 \\
    --target-os=linux \\
    --disable-programs \\
    --disable-doc \\
    --disable-avdevice \\
    --disable-postproc \\
    --disable-autodetect \\
    --disable-shared \\
    --enable-static

make -j"\${RKAV_FFMPEG_JOBS:-4}"
make install
sed -i 's#/work/prefix#/opt/ffmpeg#g' /work/prefix/lib/pkgconfig/*.pc
echo FFMPEG_BUILD_DONE
EOS

"$rkav_engine" run --rm \
    --env "RKAV_FFMPEG_JOBS=${RKAV_BUILD_JOBS:-4}" \
    --volume "$rkav_build_dir:/work" \
    --workdir /work \
    "$rkav_image" \
    bash /work/build_ffmpeg.sh

ls -l "$rkav_build_dir/prefix/lib/pkgconfig"
