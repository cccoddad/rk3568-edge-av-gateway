#!/usr/bin/env sh
# Downloads the exact RK356X RKNN 1.4.0 development files without cloning the full repository.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <destination-directory>" >&2
    exit 2
fi

rkav_destination=$1
rkav_api_dir="$rkav_destination/runtime/RK356X/Linux/librknn_api"
rkav_header_sha256=2bb8008935f6ea3df421fcc3d294887c804fff5c08e7f29c330b557bd31d65c2
rkav_runtime_sha256=0ebc1b408f897863a91a1b9ed60f3838a801386c7b1ef7c54d55ead624cd8347
rkav_header_blob=10b3f95af39d7b68257ea3e666d6b6be456572de
rkav_runtime_blob=5c886a02670bf258b3ba2c44e98e87fa5fd51fcb
rkav_api_base=https://api.github.com/repos/airockchip/rknpu2/git/blobs

for rkav_command in curl sha256sum; do
    if ! command -v "$rkav_command" >/dev/null 2>&1; then
        echo "required command not found: $rkav_command" >&2
        exit 2
    fi
done

mkdir -p "$rkav_api_dir/include" "$rkav_api_dir/aarch64"

rkav_download() {
    rkav_blob=$1
    rkav_output=$2
    curl --fail --location --retry 10 --retry-all-errors --connect-timeout 20 --max-time 600 \
        -H 'Accept: application/vnd.github.raw+json' \
        -H 'X-GitHub-Api-Version: 2022-11-28' \
        -H 'User-Agent: rk3568-development' \
        "$rkav_api_base/$rkav_blob" --output "$rkav_output"
}

rkav_download "$rkav_header_blob" "$rkav_api_dir/include/rknn_api.h"
rkav_download "$rkav_runtime_blob" "$rkav_api_dir/aarch64/librknnrt.so"

printf '%s  %s\n' "$rkav_header_sha256" "$rkav_api_dir/include/rknn_api.h" | sha256sum --check
printf '%s  %s\n' "$rkav_runtime_sha256" "$rkav_api_dir/aarch64/librknnrt.so" | sha256sum --check

printf '%s\n' \
    'repository=https://github.com/airockchip/rknpu2.git' \
    'tag=v1.4.0' \
    'commit=ef74cafa8013ffafe6d932f38ce2ea26d37a0283' \
    'platform=RK356X/Linux/aarch64' \
    >"$rkav_destination/SOURCE.txt"

printf 'rknn_sdk=ready\nroot=%s\n' "$rkav_api_dir"
