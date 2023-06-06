cmake_minimum_required(VERSION 3.10.2)

project(cam2rtpfile VERSION 0.1.0)  #Project name

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

set(CMAKE_CXX_STANDARD 14) #setting C++ 14 standard
add_compile_options("$<$<CONFIG:Debug>:-O0;-g3;-ggdb>")
find_package(PkgConfig) #finding pkg-config is a helper tool

#using pkg-config to get Gstreamer
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
#add thread support
find_package(Threads REQUIRED)

#including GStreamer header files directory
include_directories(
        include
        ${GLIB_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
)

#linking GStreamer library directory
link_directories(
        include
        ${GLIB_LIBRARY_DIRS}
        ${GSTREAMER_LIBRARY_DIRS}
)

#building target executable
add_executable(${PROJECT_NAME} src/stream.cpp)

#linking Gstreamer library with target executable
target_link_libraries(${PROJECT_NAME} ${GLIB_LIBRARIES} ${GSTREAMER_LIBRARIES} Threads::Threads)