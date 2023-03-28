#!/bin/bash
################################################################################
# Copyright (c) 2023 ModalAI, Inc. All rights reserved.
################################################################################

# Script to test different camera resolutions for voxl-camera-server and start streamer

# Traditional/HIRES_STREAM 4K: 4096x2160
# Traditional/HIRES_STREAM 2K: 2048x1536
# Traditional 1080P: 1920×1080
# Traditional 720P: 1280x720
# Traditional/HIRES_STREAM 720P: 1024x768
# Traditional 640x480: 640x480

hires_resolution_width=''
hires_resolution_height=''

preview_resolution_width=''
preview_resolution_height=''

print_usage() {
  printf "Usage:\n For 4K preview and streamer pass in -a flag.\n For 2K preview and streamer pass in -b flag.\n For 720p preview and streamer pass in -e flag\n"
}

while getopts 'abcdefh' flag; do
  case "${flag}" in
    a) hires_resolution_width='4096'; hires_resolution_height='2160'; preview_resolution_width='4096'; preview_resolution_height='2160' ;;
    b) hires_resolution_width='2048'; hires_resolution_height='1536'; preview_resolution_width='2048'; preview_resolution_height='1536' ;;
    c) preview_resolution_width='1920'; preview_resolution_height='1080' ;; 
    d) preview_resolution_width='1280'; preview_resolution_height='720' ;;
    e) hires_resolution_width='1024'; hires_resolution_height='768'; preview_resolution_width='1024'; preview_resolution_height='768' ;;
    f) preview_resolution_width='640'; preview_resolution_height='480' ;;
    h) print_usage
       exit 1 ;;
  esac
done

if ((OPTIND == 1))
then
    echo "No options specified"
    print_usage
    exit
fi

sed -i "/preview_width/c\ \t\t\t\t\t\t\"preview_width\" : $preview_resolution_width," voxl-camera-server.conf
sed -i "/preview_height/c\ \t\t\t\t\t\t\"preview_height\" : $preview_resolution_height," voxl-camera-server.conf
sed -i "/stream_width/c\ \t\t\t\t\t\t\"stream_width\" : $hires_resolution_width," voxl-camera-server.conf
sed -i "/stream_height/c\ \t\t\t\t\t\t\"stream_height\" : $hires_resolution_height," voxl-camera-server.conf
sed -i "/record_width/c\ \t\t\t\t\t\t\"record_width\" : $hires_resolution_width," voxl-camera-server.conf
sed -i "/record_height/c\ \t\t\t\t\t\t\"record_height\" : $hires_resolution_height," voxl-camera-server.conf

# Push edited camera-server file to /etc/modalai and start service
adb push voxl-camera-server.conf /etc/modalai/voxl-camera-server.conf
adb shell systemctl start voxl-camera-server

# Configure voxl-streamer to intake hires_stream and start service
# No need to worry about decimator since using hires_stream should ignore decimator
adb shell voxl-configure-streamer -i hires_stream
adb shell systemctl start voxl-streamer

sleep 5s

echo "Value of voxl-camera-server is: "
adb shell systemctl is-active voxl-camera-server
echo "Value of voxl-streamer is: "
adb shell systemctl is-active voxl-streamer
