#!/usr/bin/env sh
# Cross-builds static Linux/aarch64 gateway and test binaries from WSL/Ubuntu.
set -eu

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_build_dir="$rkav_project_root/build/cross-aarch64-static"
rkav_output_dir="$rkav_project_root/out/aarch64-static"

for rkav_command in cmake ninja aarch64-linux-gnu-g++ file; do
    if ! command -v "$rkav_command" >/dev/null 2>&1; then
        echo "required command not found: $rkav_command" >&2
        exit 2
    fi
done

cd "$rkav_project_root"
cmake --preset cross-aarch64-static
cmake --build --preset cross-aarch64-static -j "${RKAV_BUILD_JOBS:-4}"

mkdir -p "$rkav_output_dir"
cp "$rkav_build_dir/rkav-gateway" "$rkav_output_dir/rkav-gateway"
cp "$rkav_build_dir/tests/rkav_tests" "$rkav_output_dir/rkav_tests"
cp "$rkav_project_root/config/mock.json" "$rkav_output_dir/mock.json"
cp "$rkav_project_root/config/rk3568-v4l2.json" "$rkav_output_dir/rk3568-v4l2.json"
cp "$rkav_project_root/tools/soak_test_buildroot.sh" "$rkav_output_dir/soak_test_buildroot.sh"
chmod +x "$rkav_output_dir/soak_test_buildroot.sh"

file "$rkav_output_dir/rkav-gateway"
file "$rkav_output_dir/rkav_tests"
if ! file "$rkav_output_dir/rkav-gateway" | grep -q 'ARM aarch64'; then
    echo "gateway is not an aarch64 executable" >&2
    exit 1
fi
if ! file "$rkav_output_dir/rkav-gateway" | grep -q 'statically linked'; then
    echo "gateway is not statically linked" >&2
    exit 1
fi

printf 'cross_build=passed\nartifacts=%s\n' "$rkav_output_dir"
