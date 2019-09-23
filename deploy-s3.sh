#!/bin/bash

# Bash setup (exit on error)
set -e

# Build
# docker exec -it exmg-lls-container make -j4
# docker cp exmg-lls-container:/app/ffmpeg ffmpeg
# docker cp exmg-lls-container:/app/ffmpeg_g ffmpeg_g

# Deploy
version_file="version"

wget -O $version_file "https://s3-eu-west-1.amazonaws.com/joep-lls-deploy/version"
version=$(cat "$version_file")
version=$((version+1))

echo $version > $version_file

filename="ffmpeg-exmg-lls-linux-$version"

echo "Deploying version $version to https://s3-eu-west-1.amazonaws.com/joep-lls-deploy/$filename"

cp ffmpeg_g $filename
aws s3 cp $filename s3://joep-lls-deploy/
aws s3api put-object-acl --bucket joep-lls-deploy --key $filename --acl public-read

aws s3 cp $version_file s3://joep-lls-deploy/
aws s3api put-object-acl --bucket joep-lls-deploy --key $version_file --acl public-read

rm $filename