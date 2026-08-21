#!/usr/bin/env sh
# Runs the prebuilt gateway on minimal Buildroot and samples /proc/sysfs resources.
set -u

rkav_duration_seconds=${1:-60}
rkav_sample_seconds=${2:-10}
rkav_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rkav_temperature_path=${RKAV_TEMPERATURE_PATH:-/sys/class/thermal/thermal_zone0/temp}
rkav_temperature_limit=${RKAV_TEMPERATURE_LIMIT:-85000}

case "$rkav_duration_seconds" in
    ''|*[!0-9]*) echo "duration must be a positive integer" >&2; exit 2 ;;
esac
case "$rkav_sample_seconds" in
    ''|*[!0-9]*) echo "sample interval must be a positive integer" >&2; exit 2 ;;
esac
if [ "$rkav_duration_seconds" -eq 0 ] || [ "$rkav_sample_seconds" -eq 0 ]; then
    echo "duration and sample interval must be greater than zero" >&2
    exit 2
fi

rkav_executable=${RKAV_EXECUTABLE:-$rkav_script_dir/rkav-gateway}
rkav_config=${RKAV_CONFIG:-$rkav_script_dir/mock.json}
rkav_pid=

rkav_interrupt() {
    if [ -n "$rkav_pid" ] && kill -0 "$rkav_pid" 2>/dev/null; then
        kill -TERM "$rkav_pid" 2>/dev/null || true
        wait "$rkav_pid" 2>/dev/null || true
    fi
    echo "soak test interrupted" >&2
    exit 130
}

trap rkav_interrupt HUP INT TERM

if [ ! -x "$rkav_executable" ]; then
    echo "executable not found: $rkav_executable" >&2
    exit 2
fi
if [ ! -r "$rkav_config" ]; then
    echo "configuration not found: $rkav_config" >&2
    exit 2
fi

rkav_timestamp=$(date +%Y%m%d-%H%M%S)
rkav_output_dir=${RKAV_OUTPUT_DIR:-$rkav_script_dir/soak-buildroot-$rkav_timestamp}
rkav_cpu_count=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null)
if [ -z "$rkav_cpu_count" ] || [ "$rkav_cpu_count" -eq 0 ]; then
    rkav_cpu_count=1
fi

mkdir -p "$rkav_output_dir"
printf '%s\n' \
    "duration_seconds=$rkav_duration_seconds" \
    "sample_seconds=$rkav_sample_seconds" \
    "temperature_path=$rkav_temperature_path" \
    "temperature_limit_millicelsius=$rkav_temperature_limit" \
    "cpu_count=$rkav_cpu_count" > "$rkav_output_dir/environment.txt"
uname -a >> "$rkav_output_dir/environment.txt" 2>&1
free -h >> "$rkav_output_dir/environment.txt" 2>&1
df -h "$rkav_script_dir" >> "$rkav_output_dir/environment.txt" 2>&1

echo "epoch_seconds,elapsed_seconds,rss_kb,threads,cpu_percent,soc_temp_mc,cpu_freq_khz" \
    > "$rkav_output_dir/resources.csv"

"$rkav_executable" --config "$rkav_config" --duration "$rkav_duration_seconds" \
    > "$rkav_output_dir/gateway.log" 2>&1 &
rkav_pid=$!
rkav_started_epoch=$(date +%s)
rkav_previous_process_ticks=
rkav_previous_total_ticks=
rkav_thermal_abort=0

echo "soak test started: pid=$rkav_pid duration=${rkav_duration_seconds}s sample=${rkav_sample_seconds}s"
echo "artifacts: $rkav_output_dir"

while kill -0 "$rkav_pid" 2>/dev/null; do
    if [ ! -r "/proc/$rkav_pid/status" ] || [ ! -r "/proc/$rkav_pid/stat" ]; then
        break
    fi

    rkav_epoch=$(date +%s)
    rkav_elapsed=$((rkav_epoch - rkav_started_epoch))
    rkav_rss=$(awk '$1 == "VmRSS:" {print $2; exit}' "/proc/$rkav_pid/status")
    rkav_threads=$(awk '$1 == "Threads:" {print $2; exit}' "/proc/$rkav_pid/status")
    rkav_process_ticks=$(awk '{print $14 + $15}' "/proc/$rkav_pid/stat")
    rkav_total_ticks=$(awk '/^cpu / {sum=0; for (i=2; i<=NF; ++i) sum+=$i; print sum; exit}' \
        /proc/stat)
    rkav_cpu_percent=
    if [ -n "$rkav_previous_process_ticks" ] && [ -n "$rkav_previous_total_ticks" ]; then
        rkav_cpu_percent=$(awk -v current_process="$rkav_process_ticks" \
            -v previous_process="$rkav_previous_process_ticks" \
            -v current_total="$rkav_total_ticks" -v previous_total="$rkav_previous_total_ticks" \
            -v cpus="$rkav_cpu_count" \
            'BEGIN { total=current_total-previous_total; process=current_process-previous_process;
                if (total > 0) printf "%.2f", 100.0*cpus*process/total; }')
    fi
    rkav_previous_process_ticks=$rkav_process_ticks
    rkav_previous_total_ticks=$rkav_total_ticks

    rkav_temperature=
    if [ -r "$rkav_temperature_path" ]; then
        rkav_temperature=$(tr -d '[:space:]' < "$rkav_temperature_path")
    fi
    rkav_frequency=
    if [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq ]; then
        rkav_frequency=$(tr -d '[:space:]' \
            < /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
    fi

    echo "$rkav_epoch,$rkav_elapsed,$rkav_rss,$rkav_threads,$rkav_cpu_percent,$rkav_temperature,$rkav_frequency" \
        >> "$rkav_output_dir/resources.csv"
    echo "sample: elapsed=${rkav_elapsed}s rss=${rkav_rss}kB threads=$rkav_threads cpu=${rkav_cpu_percent:-warming-up}% temp=${rkav_temperature:-unknown}mC"

    case "$rkav_temperature" in
        ''|*[!0-9]*) ;;
        *)
            if [ "$rkav_temperature" -ge "$rkav_temperature_limit" ]; then
                rkav_thermal_abort=1
                echo "temperature limit reached: $rkav_temperature" \
                    > "$rkav_output_dir/thermal_abort.txt"
                kill -TERM "$rkav_pid" 2>/dev/null || true
                break
            fi
            ;;
    esac
    sleep "$rkav_sample_seconds"
done

wait "$rkav_pid"
rkav_status=$?
trap - HUP INT TERM
if [ "$rkav_thermal_abort" -eq 1 ] && [ "$rkav_status" -eq 0 ]; then
    rkav_status=3
fi
printf '%s\n' \
    "exit_code=$rkav_status" \
    "thermal_abort=$rkav_thermal_abort" \
    "artifacts=$rkav_output_dir" > "$rkav_output_dir/result.txt"
grep -F '"event":"application_stopped"' "$rkav_output_dir/gateway.log" | tail -n 1 \
    > "$rkav_output_dir/final-metrics.log" 2>/dev/null || true
cat "$rkav_output_dir/result.txt"
exit "$rkav_status"
