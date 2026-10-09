set(LIB_NAME video_renderer)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.9")
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

set(SRC_FILES 
    VideoRenderer.cpp
    VideoRenderer.h
)

add_library(${LIB_NAME} STATIC ${SRC_FILES})

target_link_libraries(${LIB_NAME} PRIVATE
    Qt6::Gui
    Qt6::Widgets
    Qt6::GuiPrivate
    media
)