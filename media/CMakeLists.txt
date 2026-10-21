set(LIB_NAME media)

set(SRC_FILES 
    include/media/IMediaFrame.h
    include/media/IMediaSource.h
    Logger.h
    Logger.cpp
    MediaSource.h
    MediaSource.cpp
    Decoder.h
    Decoder.cpp
    DecodeWorker.h
    DecodeWorker.cpp
    MediaPacket.h
    MediaPacket.cpp
    MediaFrame.h
    MediaFrame.cpp
    Demuxer.h
    Demuxer.cpp
)

if (WIN32)
    list(APPEND SRC_FILES D3d11vaDecoder.h D3d11vaDecoder.cpp)
endif()

add_library(${LIB_NAME} STATIC ${SRC_FILES})

target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(ffmpeg_helper)

target_link_libraries(${LIB_NAME} PRIVATE ffmpeg_helper spdlog::spdlog)