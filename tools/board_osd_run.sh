#!/usr/bin/env sh
# 文件作用：板端 OSD 验收与 2/12 小时长稳的统一运行器（冒烟、长稳、SIGTERM 三模式）。
# 主要知识点：唯一证据目录、dmesg 前后快照、H264 staging 收集、退出码与最终指标抽取、
#             BusyBox 兼容（无 bash 扩展、无数组、set -u 不 set -e）。
set -u

rkav_mode=${1:-smoke} # smoke | soak | signal
rkav_config=${2:-}
rkav_duration=${3:-}

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_gateway=${RKAV_EXECUTABLE:-/userdata/rkav/mpp-rtsp-reconnect-20260914-1540-7788/rkav-gateway}
rkav_staging=${RKAV_H264_STAGING:-/userdata/rkav/osd-h264-staging}
rkav_output_root=${RKAV_OUTPUT_ROOT:-/userdata/rkav}

case "$rkav_mode" in
    smoke|soak|signal) ;;
    *) echo "usage: $0 smoke|soak|signal <config.json> <duration_seconds>" >&2; exit 2 ;;
esac
if [ -z "$rkav_config" ] || [ ! -r "$rkav_config" ]; then
    echo "config not readable: $rkav_config" >&2
    exit 2
fi
case "$rkav_duration" in
    '') echo "duration_seconds required" >&2; exit 2 ;;
    *[!0-9]*) echo "duration must be a non-negative integer" >&2; exit 2 ;;
esac
if [ ! -x "$rkav_gateway" ]; then
    echo "gateway not executable: $rkav_gateway" >&2
    exit 2
fi

# 唯一结果目录：模式 + 板端时间戳，杜绝覆盖历史证据。
rkav_timestamp=$(date +%Y%m%d-%H%M%S)
rkav_label="osd-${rkav_mode}-${rkav_timestamp}"
rkav_output_dir="$rkav_output_root/$rkav_label"
if ! mkdir "$rkav_output_dir" 2>/dev/null; then
    echo "cannot create unique output dir: $rkav_output_dir" >&2
    exit 2
fi

# 前置门禁：三硬件被占用、配置校验失败都在起线程前失败。
if pidof rkav-gateway >/dev/null 2>&1; then
    echo "GATEWAY_ALREADY_RUNNING" | tee "$rkav_output_dir/blocked.txt"
    exit 4
fi
"$rkav_gateway" --validate-config --config "$rkav_config" > "$rkav_output_dir/validate.log" 2>&1
rkav_validate_status=$?
if [ "$rkav_validate_status" -ne 0 ]; then
    echo "CONFIG_VALIDATION_FAILED exit=$rkav_validate_status"
    exit "$rkav_validate_status"
fi

{
    echo "mode=$rkav_mode"
    echo "duration_seconds=$rkav_duration"
    echo "config=$rkav_config"
    echo "gateway=$rkav_gateway"
    date
    cat /proc/uptime
    uname -a
    df -h "$rkav_output_root"
    pidof MediaServer >/dev/null 2>&1 && echo "zlm=running" || echo "zlm=stopped"
} > "$rkav_output_dir/environment.txt"

dmesg > "$rkav_output_dir/dmesg.before" 2>&1

# H264 sink 不会自建目录；冒烟配置写入 staging，起跑前确保它存在。
mkdir -p "$rkav_staging" 2>/dev/null || true

rkav_signal_note=""
if [ "$rkav_mode" = "signal" ]; then
    # SIGTERM 模式：duration 0 = 跑到信号为止，10 秒后发 SIGTERM。
    "$rkav_gateway" --config "$rkav_config" --duration 0 > "$rkav_output_dir/run.log" 2>&1 &
    rkav_pid=$!
    sleep 10
    if ! kill -0 "$rkav_pid" 2>/dev/null; then
        echo "gateway exited before SIGTERM" | tee "$rkav_output_dir/blocked.txt"
        wait "$rkav_pid" || true
        exit 1
    fi
    kill -TERM "$rkav_pid"
    # 看门狗：10 秒不退则 KILL，记录超时。
    (
        sleep 10
        if kill -0 "$rkav_pid" 2>/dev/null; then
            echo "SIGTERM_TIMEOUT" > "$rkav_output_dir/timed_out"
            kill -KILL "$rkav_pid" 2>/dev/null || true
        fi
    ) &
    rkav_watchdog=$!
    wait "$rkav_pid"
    rkav_status=$?
    kill "$rkav_watchdog" 2>/dev/null || true
    wait "$rkav_watchdog" 2>/dev/null || true
    if [ -f "$rkav_output_dir/timed_out" ]; then
        rkav_signal_note="sigterm_timeout"
        rkav_status=1
    fi
else
    "$rkav_gateway" --config "$rkav_config" --duration "$rkav_duration" \
        > "$rkav_output_dir/run.log" 2>&1
    rkav_status=$?
fi

dmesg > "$rkav_output_dir/dmesg.after" 2>&1

# H264 staging 移入证据目录（冒烟产物），随后清空 staging 供下次使用。
if [ -d "$rkav_staging" ]; then
    find "$rkav_staging" -type f -name '*.h264*' 2>/dev/null | while read -r rkav_file; do
        mv "$rkav_file" "$rkav_output_dir/" 2>/dev/null || true
    done
    rmdir "$rkav_staging" 2>/dev/null || true
fi

# 抽取停止事件与 OSD 计数；长稳日志大，只取首尾事件。
grep -F '"event":"application_stopped"' "$rkav_output_dir/run.log" | tail -n 1 \
    > "$rkav_output_dir/final-metrics.log" 2>/dev/null || true
if [ ! -s "$rkav_output_dir/final-metrics.log" ]; then
    grep -F '"event":"metrics_snapshot"' "$rkav_output_dir/run.log" | tail -n 1 \
        > "$rkav_output_dir/final-metrics.log" 2>/dev/null || true
fi
grep -F '"event":"video_backend_opened"' "$rkav_output_dir/run.log" | head -n 1 \
    > "$rkav_output_dir/first-events.log" 2>/dev/null || true
grep -F '"level":"error"' "$rkav_output_dir/run.log" | head -n 20 \
    > "$rkav_output_dir/errors.log" 2>/dev/null || true

# final-metrics.log 是嵌套 JSON，内部引号被转义为 \"，匹配时用 [^0-9]* 兼容两种写法。
rkav_applied=$(grep -o 'overlay_applied_total[^0-9]*[0-9][0-9]*' "$rkav_output_dir/final-metrics.log" 2>/dev/null \
    | head -n 1 | grep -o '[0-9][0-9]*$')
rkav_skipped=$(grep -o 'overlay_skipped_total[^0-9]*[0-9][0-9]*' "$rkav_output_dir/final-metrics.log" 2>/dev/null \
    | head -n 1 | grep -o '[0-9][0-9]*$')
rkav_errors=$(grep -o 'errors_total[^0-9]*[0-9][0-9]*' "$rkav_output_dir/final-metrics.log" 2>/dev/null \
    | head -n 1 | grep -o '[0-9][0-9]*$')
: "${rkav_applied:=missing}"
: "${rkav_skipped:=missing}"
: "${rkav_errors:=missing}"

{
    echo "exit_code=$rkav_status"
    echo "signal_note=$rkav_signal_note"
    echo "overlay_applied_total=$rkav_applied"
    echo "overlay_skipped_total=$rkav_skipped"
    echo "errors_total=$rkav_errors"
} > "$rkav_output_dir/result.txt"

( cd "$rkav_output_dir" && sha256sum dmesg.before dmesg.after environment.txt \
    result.txt 2>/dev/null ) > "$rkav_output_dir/evidence.sha256" || true

echo "RUN_DONE mode=$rkav_mode exit=$rkav_status applied=$rkav_applied skipped=$rkav_skipped errors=$rkav_errors"
echo "artifacts=$rkav_output_dir"
exit "$rkav_status"
