#!/usr/bin/env sh
# Validates codecs, stream shape, duration, timestamp monotonicity and initial A/V skew.
set -eu

rkav_file=${1:-}
rkav_expected_seconds=${2:-0}
rkav_duration_tolerance=${3:-1.0}

if [ -z "$rkav_file" ] || [ ! -f "$rkav_file" ]; then
    echo "usage: $0 FILE.mp4 [EXPECTED_SECONDS] [TOLERANCE_SECONDS]" >&2
    exit 2
fi
if ! command -v ffprobe >/dev/null 2>&1; then
    echo "ffprobe is required" >&2
    exit 2
fi

rkav_video_codec=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name \
    -of default=noprint_wrappers=1:nokey=1 "$rkav_file")
rkav_audio_codec=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name \
    -of default=noprint_wrappers=1:nokey=1 "$rkav_file")
rkav_video_size=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
    -of csv=p=0:s=x "$rkav_file")
rkav_audio_shape=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate,channels \
    -of csv=p=0:s=x "$rkav_file")
rkav_duration=$(ffprobe -v error -show_entries format=duration \
    -of default=noprint_wrappers=1:nokey=1 "$rkav_file")
rkav_video_start=$(ffprobe -v error -select_streams v:0 -show_entries stream=start_time \
    -of default=noprint_wrappers=1:nokey=1 "$rkav_file")
rkav_audio_start=$(ffprobe -v error -select_streams a:0 -show_entries stream=start_time \
    -of default=noprint_wrappers=1:nokey=1 "$rkav_file")

test "$rkav_video_codec" = h264
test "$rkav_audio_codec" = aac
awk -v value="$rkav_duration" 'BEGIN { exit !(value + 0 > 0) }'
awk -v video="$rkav_video_start" -v audio="$rkav_audio_start" 'BEGIN {
    difference = video - audio
    if (difference < 0) difference = -difference
    exit !(difference <= 0.100)
}'
if [ "$rkav_expected_seconds" != 0 ]; then
    awk -v actual="$rkav_duration" -v expected="$rkav_expected_seconds" \
        -v tolerance="$rkav_duration_tolerance" 'BEGIN {
        difference = actual - expected
        if (difference < 0) difference = -difference
        exit !(difference <= tolerance)
    }'
fi

for rkav_stream in v:0 a:0; do
    ffprobe -v error -select_streams "$rkav_stream" \
        -show_entries packet=pts_time,dts_time -of compact=p=0:nk=0 "$rkav_file" |
        awk -F '|' -v stream="$rkav_stream" '
        {
            pts = ""; dts = ""
            for (field_index = 1; field_index <= NF; ++field_index) {
                split($field_index, item, "=")
                if (item[1] == "pts_time") pts = item[2]
                if (item[1] == "dts_time") dts = item[2]
            }
            if (pts == "" || pts == "N/A" || dts == "" || dts == "N/A") exit 2
            if (count > 0 && pts + 0 < last_pts) exit 3
            if (count > 0 && dts + 0 < last_dts) exit 4
            last_pts = pts + 0
            last_dts = dts + 0
            count++
        }
        END { if (count == 0) exit 5 }
        ' || {
            echo "non-monotonic or missing timestamps in $rkav_stream" >&2
            exit 1
        }
done

printf '%s\n' \
    "mp4_validation=passed" \
    "file=$rkav_file" \
    "video_codec=$rkav_video_codec" \
    "audio_codec=$rkav_audio_codec" \
    "video_size=$rkav_video_size" \
    "audio_shape=$rkav_audio_shape" \
    "duration_seconds=$rkav_duration" \
    "video_start_seconds=$rkav_video_start" \
    "audio_start_seconds=$rkav_audio_start"
