#!/bin/bash

#host_ip=192.168.75.111
#host_ip=192.168.75.169
#host_ip=192.168.1.92
#testbox2
host_ip=192.168.75.111

#ssh supervisor@$host_ip killall ffmpeg-exmg-lls-linux

docker exec -it exmg-lls-container make -j4
docker cp exmg-lls-container:/app/ffmpeg ffmpeg
docker cp exmg-lls-container:/app/ffmpeg_g ffmpeg_g

mv ffmpeg_g ffmpeg-exmg-lls-linux
#rsync -z ffmpeg-exmg-lls-linux supervisor@$host_ip:/home/supervisor/playtrivia-video-tests/
#rsync -z ffmpeg-exmg-lls-linux supervisor@$host_ip:/home/supervisor/video-encoder/
rsync -z ffmpeg-exmg-lls-linux supervisor@$host_ip:/opt/exmg-video-encoder/
#rsync -z ffmpeg-exmg-lls-linux adminjoep@192.168.0.14:/mnt/testbox2/opt/exmg-video-encoder/


#ssh supervisor@$host_ip /home/supervisor/playtrivia-video-tests/ffmpeg-akamai.sh testbox2 0 0

