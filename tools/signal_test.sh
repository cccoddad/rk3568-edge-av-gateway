#!/usr/bin/env sh
# 文件作用：验证进程收到 SIGTERM 后走受控停止路径并在期限内退出。
# 主要知识点：后台进程、信号、wait 退出码和结构化日志断言。
set -eu

rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_project_root=$(CDPATH= cd -- "$rkav_script_dir/.." && pwd)
rkav_executable=${RKAV_EXECUTABLE:-$rkav_project_root/build/debug/rkav-gateway}
rkav_config=${RKAV_CONFIG:-$rkav_project_root/config/mock.json}
rkav_output_dir=${RKAV_SIGNAL_OUTPUT_DIR:-$rkav_project_root/out/signal-$(date +%Y%m%d-%H%M%S)}
rkav_startup_seconds=${RKAV_SIGNAL_STARTUP_SECONDS:-2}
rkav_timeout_seconds=${RKAV_SIGNAL_TIMEOUT_SECONDS:-10}

case "$rkav_startup_seconds" in
    ''|*[!0-9]*) echo "signal test startup duration must be a non-negative integer" >&2; exit 2 ;;
esac
case "$rkav_timeout_seconds" in
    ''|*[!0-9]*) echo "signal test timeout must be a non-negative integer" >&2; exit 2 ;;
esac
if [ "$rkav_timeout_seconds" -eq 0 ]; then
    echo "signal test timeout must be greater than zero" >&2
    exit 2
fi
if [ ! -x "$rkav_executable" ]; then
    echo "executable not found: $rkav_executable" >&2
    exit 2
fi

mkdir -p "$rkav_output_dir"
"$rkav_executable" --config "$rkav_config" --duration 0 > "$rkav_output_dir/gateway.log" 2>&1 &
rkav_pid=$!
trap 'kill -TERM "$rkav_pid" 2>/dev/null || true' 0 HUP INT

sleep "$rkav_startup_seconds"
if ! kill -0 "$rkav_pid" 2>/dev/null; then
    echo "gateway exited before SIGTERM; see $rkav_output_dir/gateway.log" >&2
    wait "$rkav_pid" || true
    exit 1
fi
kill -TERM "$rkav_pid"

# watchdog 避免已退出但尚未 wait 的子进程被误判为仍在运行。
(
    sleep "$rkav_timeout_seconds"
    if kill -0 "$rkav_pid" 2>/dev/null; then
        : > "$rkav_output_dir/timed_out"
        kill -KILL "$rkav_pid" 2>/dev/null || true
    fi
) &
rkav_watchdog_pid=$!

set +e
wait "$rkav_pid"
rkav_status=$?
kill "$rkav_watchdog_pid" 2>/dev/null || true
wait "$rkav_watchdog_pid" 2>/dev/null || true
set -e
trap - 0 HUP INT
if [ -f "$rkav_output_dir/timed_out" ]; then
    echo "gateway did not exit within ${rkav_timeout_seconds}s" >&2
    exit 1
fi
if [ "$rkav_status" -ne 0 ]; then
    echo "gateway returned $rkav_status after SIGTERM; see $rkav_output_dir/gateway.log" >&2
    exit "$rkav_status"
fi
if ! grep -F '"event":"application_stopped"' "$rkav_output_dir/gateway.log" | \
    grep -F '"reason":"signal"' >/dev/null; then
    echo "signal stop record not found; see $rkav_output_dir/gateway.log" >&2
    exit 1
fi
printf 'signal_test=passed\nartifacts=%s\n' "$rkav_output_dir"
