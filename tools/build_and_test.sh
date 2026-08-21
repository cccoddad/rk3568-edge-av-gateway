#!/usr/bin/env sh
# 文件作用：在 Linux/RK3568 上一键完成 Debug 配置、编译、测试和配置校验。
# 主要知识点：POSIX shell 严格模式、脚本绝对路径和失败即停止。
set -eu

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) # 脚本所在绝对目录。
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd) # 项目根目录。

cd "$rkav_project_root"
cmake --preset debug
cmake --build --preset debug -j "${RKAV_BUILD_JOBS:-4}"
ctest --preset debug
./build/debug/rkav-gateway --validate-config --config config/mock.json
