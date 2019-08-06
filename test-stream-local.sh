#!/bin/bash

# Bash setup (exit on error)
set -e

# Configuration
# - 1 second segment size (instead of 6) to reduce end-to-end latency
# - 640x480 resolution since that fits our requirements and is easy on system performance
# - Extra window size of 1 year (365*24*60*60=31536000 1 second segments) to prevent deleting old segments
#   (this works around an issue (see LLS-33) in ffmpeg causing it to crash after X (OSX X=149) segments)
export stream_id=664379
#export ffmpeg="valgrind --leak-check=yes ./ffmpeg_g"
export ffmpeg="./ffmpeg-exmg-lls-linux"
export segment_size_in_seconds=6
export window_size_in_segments=20
export window_extra_segments=31536000
export frame_rate=30
export input_width=640
export input_height=480
export video_bitrate="800k"
export audio_bitrate="128k"
export output_resolution="640x480"
export local_host="http://127.0.0.1:8080"



# Arguments
export event_name=joep33
export video_index=0

export output="$local_host/api/livestream"
export stream_url="$output"

export dash_stream_url="$stream_url/out.mpd"
export hls_stream_url="$stream_url/master.m3u8"

export input="/dev/video0"

queue_size=`expr $input_width \* $input_height \* 2 \* $frame_rate`
sub_folder="$(date +%s)"


# Setup local output
if [[ "$event_name" == "local" ]]
then
  rm -rf "local-output"
  mkdir -p "local-output/$sub_folder"
fi

timestamp=$(($(date +%s%N)/1000000))
timestamp2=$(expr $timestamp - 12000)
echo "Timestamp: $timestamp, timestamp2: $timestamp2"

exec $ffmpeg \
       -f v4l2 \
       -probesize 10M \
       -framerate $frame_rate \
       -pixel_format uyvy422 \
       -i "$input" \
       -pix_fmt yuv420p \
       -flags +global_header \
       -r $frame_rate \
       -af aresample=async=1 \
       -c:v libx264 \
       -preset medium \
       -b:v $video_bitrate \
       -bufsize $video_bitrate \
       -nal-hrd vbr \
       -s $output_resolution \
       -force_key_frames "expr:gte(t,n_forced*"$segment_size_in_seconds")" \
       -bf 0 \
       -x264opts scenecut=-1:rc_lookahead=0 \
       -seg_duration $segment_size_in_seconds \
       -use_timeline 0 \
       -http_user_agent Akamai_Broadcaster_v1.0 \
       -streaming 1 \
       -index_correction 1 \
       -http_persistent 1 \
       -ignore_io_errors 1\
       -media_seg_name $sub_folder'/segment_$RepresentationID$-$Number%05d$.m4s' \
       -init_seg_name $sub_folder'/init_$RepresentationID$.m4s' \
       -hls_playlist 1 \
       -window_size $window_size_in_segments \
       -extra_window_size $window_extra_segments \
       -start_time_ms $timestamp2 \
       $output/out.mpd

# Argument graveyard
#
# This seems to only result in <UTCTiming ..> with the URL being added to the MPD file:
# -utc_timing_url https://time.akamai.com/?iso \

# bf=0 betekend geen b frames

#       -x264opts scenecut=-1:rc_lookahead=0 \

# x264 
# zerolatency tune:
#  --bframes 0 --force-cfr --no-mbtree
#  --sync-lookahead 0 --sliced-threads
#  --rc-lookahead 0

# audio
#      -sample_fmt s16 \
#      -f alsa \
#      -thread_queue_size 512 \
#			 -ac 2 \
#			 -i hw:0,0 \

# other
# this gives issues on local server. with 6s segment size I get this error after segment2:
# URL read error:  -541478725
# -http_persistent 1 \

# -loglevel verbose \
