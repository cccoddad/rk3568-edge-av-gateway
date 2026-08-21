#!/usr/bin/env sh
# 文件作用：长时间运行 Release 管道，并周期采集进程 RSS 和 CPU 到 CSV。
# 主要知识点：后台进程、PID 监控、环境变量覆盖、退出码保留和测试证据归档。
set -eu

rkav_duration_seconds=${1:-1800} # 第一个参数是总运行秒数，默认 30 分钟。
rkav_sample_seconds=${RKAV_SAMPLE_SECONDS:-10} # 资源采样间隔，默认 10 秒。
rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_executable=${RKAV_EXECUTABLE:-$rkav_project_root/build/release/rkav-gateway}
rkav_config=${RKAV_CONFIG:-$rkav_project_root/config/mock.json}
rkav_timestamp=$(date +%Y%m%d-%H%M%S)
rkav_output_root=${RKAV_OUTPUT_ROOT:-$rkav_project_root/out}
rkav_output_dir="$rkav_output_root/soak-$rkav_timestamp"
rkav_temperature_path=${RKAV_TEMPERATURE_PATH:-}

case "$rkav_duration_seconds" in
    ''|*[!0-9]*) echo "duration must be a positive integer" >&2; exit 2 ;;
esac
if [ "$rkav_duration_seconds" -eq 0 ]; then
    echo "duration must be greater than zero" >&2
    exit 2
fi
if [ ! -x "$rkav_executable" ]; then
    echo "executable not found: $rkav_executable" >&2
    echo "build Release first with: cmake --preset release && cmake --build --preset release" >&2
    exit 2
fi

if [ -z "$rkav_temperature_path" ]; then
    for rkav_candidate in /sys/class/thermal/thermal_zone*/temp; do
        if [ -r "$rkav_candidate" ]; then
            rkav_temperature_path=$rkav_candidate
            break
        fi
    done
fi

mkdir -p "$rkav_output_dir"
echo "epoch_seconds,rss_kb,cpu_percent,temperature_millicelsius" > "$rkav_output_dir/resources.csv"
{
    uname -a
    free -h
    df -hT "$rkav_project_root"
    if [ -n "$rkav_temperature_path" ]; then
        echo "temperature_path=$rkav_temperature_path"
    else
        echo "temperature_path=unavailable"
    fi
} > "$rkav_output_dir/environment.txt"
"$rkav_executable" --config "$rkav_config" --duration "$rkav_duration_seconds" \
    > "$rkav_output_dir/gateway.log" 2>&1 &
rkav_pid=$!

while kill -0 "$rkav_pid" 2>/dev/null; do
    rkav_epoch=$(date +%s)
    rkav_stats=$(ps -p "$rkav_pid" -o rss=,%cpu= 2>/dev/null || true)
    if [ -n "$rkav_stats" ]; then
        set -- $rkav_stats
        rkav_temperature=""
        if [ -n "$rkav_temperature_path" ] && [ -r "$rkav_temperature_path" ]; then
            rkav_temperature=$(tr -d '[:space:]' < "$rkav_temperature_path")
        fi
        echo "$rkav_epoch,$1,$2,$rkav_temperature" >> "$rkav_output_dir/resources.csv"
    fi
    sleep "$rkav_sample_seconds"
done

set +e
wait "$rkav_pid"
rkav_status=$?
set -e
echo "exit_code=$rkav_status" > "$rkav_output_dir/result.txt"
echo "artifacts=$rkav_output_dir" >> "$rkav_output_dir/result.txt"
cat "$rkav_output_dir/result.txt"
exit "$rkav_status"
