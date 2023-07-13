# Jetson GStreamer
Code for running gstreamer on the jetson nano, which supports very low latency video transmission and the ability to start and stop video recording at any time. Supercedes the use of [mjpg-streamer](https://github.com/xyven1/mjpg-streamer).

## Build Process

### Linux
`sudo apt install cmake build-essential libgstreamer1.0-dev libgstreamer-plugins-good1.0-dev`  
`cmake . && make`  
Output executable is `cam2rtpfile`

### Windows
install gstreamer (complete) and gstreamer dev files (complete)  
install vcpkg  
run `vcpkg integrate install` (elevated)  
run `cmake -B out -S src -DCMAKE_TOOLCHAIN_FILE=C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake` where the path is replaced with whatever the integrate step gave you  
open `out/cam2rtpfile.sln` and then build the solution  
the output will be in `out/(Release|Debug)/cam2rtpfile.exe`  
