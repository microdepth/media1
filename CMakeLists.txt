cmake_minimum_required(VERSION 3.26)
project(media1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Release)

add_executable(media1 main.cpp
        stb_image_write.h
        stb_image.h)

find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swscale swresample)
target_include_directories(media1 PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(media1 PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(media1 PRIVATE ${FFMPEG_LIBRARIES})
