#!/usr/bin/env sh
# 文件作用：在实际 Linux 板卡上采集 M5 环境基线，输出可归档的原始证据。
# 主要知识点：只读系统检查、可选命令降级和可重复的证据目录。
set -eu

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_timestamp=$(date +%Y%m%d-%H%M%S)
rkav_output_dir=${1:-$rkav_project_root/out/baseline/$rkav_timestamp}

mkdir -p "$rkav_output_dir"

rkav_write_command() {
    rkav_label=$1
    shift
    printf '\n$ %s\n' "$*" >> "$rkav_output_dir/system.txt"
    if command -v "$1" >/dev/null 2>&1; then
        "$@" >> "$rkav_output_dir/system.txt" 2>&1 || true
    else
        printf 'unavailable: %s\n' "$1" >> "$rkav_output_dir/system.txt"
    fi
}

: > "$rkav_output_dir/system.txt"
printf 'collected_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$rkav_output_dir/system.txt"
printf 'hostname=%s\n' "$(hostname 2>/dev/null || echo unavailable)" >> "$rkav_output_dir/system.txt"
rkav_write_command uname uname -a
rkav_write_command os-release cat /etc/os-release
rkav_write_command lscpu lscpu
rkav_write_command free free -h
rkav_write_command df df -hT
rkav_write_command cmake cmake --version
rkav_write_command cxx c++ --version
rkav_write_command ninja ninja --version
rkav_write_command git git --version

printf '\n$ cat /proc/device-tree/model\n' >> "$rkav_output_dir/system.txt"
if [ -r /proc/device-tree/model ]; then
    tr '\000' '\n' < /proc/device-tree/model >> "$rkav_output_dir/system.txt"
else
    echo 'unavailable' >> "$rkav_output_dir/system.txt"
fi

: > "$rkav_output_dir/thermal.txt"
for rkav_zone in /sys/class/thermal/thermal_zone*; do
    if [ -r "$rkav_zone/temp" ]; then
        rkav_type=unknown
        if [ -r "$rkav_zone/type" ]; then
            rkav_type=$(tr -d '[:space:]' < "$rkav_zone/type")
        fi
        printf '%s,type=%s,temp_millicelsius=%s\n' "$rkav_zone" "$rkav_type" \
            "$(tr -d '[:space:]' < "$rkav_zone/temp")" >> "$rkav_output_dir/thermal.txt"
    fi
done

printf 'baseline_artifacts=%s\n' "$rkav_output_dir"
