# Jetson GStreamer
Code for running gstreamer on the jetson nano, which supports very low latency video transmission and the ability to start and stop video recording at any time. Supercedes the use of [mjpg-streamer](https://github.com/xyven1/mjpg-streamer).

## Build Process
`sudo apt install cmake libgstreamer1.0-dev libgstreamer-plugins-good1.0-dev`  
`cmake . && make`  
Output executable is `cam2rtpfile`

