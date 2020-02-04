#!/bin/bash

# Bash setup (exit on error)
set -e
export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib:/usr/local/lib/x86_64-linux-gnu
export ffmpeg="./ffmpeg"
export segment_size_in_seconds=6
export window_size_in_segments=20
export window_extra_segments=31536000
export frame_rate_num=25000
export frame_rate_den=1000
export input_width=640
export input_height=480
export video_bitrate="800k"
export audio_bitrate="128k"
export output_resolution="640x480"
export input="/dev/video0"
export stream_id=2009574

sub_folder="$(date +%s)"
#export output="http://localhost:3000/local-output"
export output="http://test:Test123!@p-ep$stream_id.i.akamaientrypoint.net/cmaf/$stream_id/joep10"

# Setup local output
rm -rf "local-output"
mkdir -p "local-output/$sub_folder"

exec $ffmpeg \
       -vsync 1 \
       -threads 4 \
       -hwaccel cuvid\
       -c:v hevc_cuvid \
       -f v4l2 \
       -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=44100 \
       -framerate 25 \
       -i "$input" \
       -pix_fmt yuv420p \
       -flags +global_header \
       -af aresample=async=1 \
       -c:v hevc_nvenc \
       -b:v $video_bitrate \
       -bufsize $video_bitrate \
       -s $output_resolution \
       -preset ll \
       -sc_threshold 0 \
       -force_key_frames "expr:gte(t,n_forced*"$segment_size_in_seconds")" \
       -bf 0 \
       -c:a aac \
       -b:a $audio_bitrate \
       -seg_duration $segment_size_in_seconds \
       -use_timeline 0 \
       -http_user_agent Akamai_Broadcaster_v1.0 \
       -streaming 1 \
       -index_correction 1 \
       -http_persistent 1 \
       -ignore_io_errors 1\
       -media_seg_name  $sub_folder'/segment_$RepresentationID$-$Number%05d$.m4s' \
       -init_seg_name  $sub_folder'/init_$RepresentationID$.m4s' \
       -hls_playlist 0 \
       -window_size $window_size_in_segments \
       -extra_window_size $window_extra_segments \
       -g 30 \
       -f dash $output/out.mpd


