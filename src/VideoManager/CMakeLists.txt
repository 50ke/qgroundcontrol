find_package(Qt6 REQUIRED COMPONENTS Core)

include("${PROJECT_SOURCE_DIR}/libs/mdk-sdk/lib/cmake/FindMDK.cmake")
add_definitions(-DWIN32_LEAN_AND_MEAN)


qt_add_library(VideoManager STATIC
    SubtitleWriter.cc
    SubtitleWriter.h
    VideoManager.cc
    VideoManager.h
    LidarManager.h LidarManager.cpp
    mdkplayer.h mdkplayer.cpp
)

# option(QGC_ENABLE_VIDEOSTREAMING "Enable video streaming" ON)
# cmake_print_variables(QGC_ENABLE_VIDEOSTREAMING)
# if(NOT QGC_ENABLE_VIDEOSTREAMING)
#     message(STATUS "Video streaming support disabled")
#     return()
# endif()
# message(STATUS "Video streaming support enabled")

add_subdirectory(VideoReceiver)

target_link_libraries(VideoManager
    PRIVATE
        API
        Camera
        FactSystem
        GStreamerReceiver
        QmlControls
        QtMultimediaReceiver
        Settings
        Utilities
        Vehicle
        VideoReceiver
    PUBLIC
        Qt6::Core
        QGC
        mdk
        setupapi
        advapi32
        ws2_32
)

target_include_directories(VideoManager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
