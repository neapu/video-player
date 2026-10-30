set(LIB_NAME video_renderer)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets OpenGLWidgets)

set(SRC_FILES 
    GLVideoRenderer.cpp
    GLVideoRenderer.h
)

add_library(${LIB_NAME} STATIC ${SRC_FILES})

target_link_libraries(${LIB_NAME} PRIVATE
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    media
)