set(LIB_NAME media)

set(SRC_FILES 
    include/media/IMediaFrame.h
    include/media/IMediaSource.h
    MediaSource.h
    MediaSource.cpp
)

add_library(${LIB_NAME} STATIC ${SRC_FILES})

target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)