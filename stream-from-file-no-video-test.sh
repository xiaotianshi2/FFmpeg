#!/bin/bash

# Bash setup (exit on error)
set -e
export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib
export ffmpeg="./ffmpeg_svthevc"
export segment_size_in_seconds=6
export window_size_in_segments=20
export window_extra_segments=31536000
export frame_rate_num=25000
export frame_rate_den=1000
export input_width=176
export input_height=144
export video_bitrate="10k"
export audio_bitrate="128k"
export output_resolution="176x144"
export input="/dev/video0"
export stream_id=2009574

sub_folder="$(date +%s)"
#export output="http://localhost:3000/local-output"
export output="http://test:Test123!@p-ep$stream_id.i.akamaientrypoint.net/cmaf/$stream_id/joep10"

# Setup local output
rm -rf "local-output"
mkdir -p "local-output/$sub_folder"
#-f lavfi -i color=c=black@0.2:s=qcif:rate=25 \

exec $ffmpeg \
       -vsync 1 \
       -re \
       -flags +global_header \
       -i GardenAnswer.aac \
       -af aresample=async=1 \
       -force_key_frames "expr:gte(t,n_forced*"$segment_size_in_seconds")" \
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
       -hls_playlist 1 \
       -window_size $window_size_in_segments \
       -extra_window_size $window_extra_segments \
       -g 30 \
       -f dash $output/out.mpd


