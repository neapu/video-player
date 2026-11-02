set(LIB_NAME rhi_video_renderer)

set(SRC_FILES
    QRhiVideoRenderer.cpp
    QRhiVideoRenderer.h
    ShaderResource.h
    ShaderResource.cpp
    YuvShaderResource.cpp
    YuvShaderResource.h
)

if (WIN32)
    list(APPEND SRC_FILES
        D3D11ShaderResource.cpp
        D3D11ShaderResource.h
    )
endif()

add_library(${LIB_NAME} ${SRC_FILES})

find_package(Qt6 6.8 REQUIRED COMPONENTS ShaderTools)

qt_add_shaders(${LIB_NAME} "shaders"
    FILES
    shaders/video.vert
    shaders/yuv420p.frag
    shaders/nv12.frag
)

# 6.8 以前不需要单独找 GuiPrivate，可以直接链接，6.9 以后才需要
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.9")
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

# 即使是 6.8 也需要链接 GuiPrivate，只是不需要 find_package
# ShaderTools 只负责编译着色器，不用链接
target_link_libraries(${LIB_NAME} PUBLIC Qt6::Gui Qt6::Widgets Qt6::GuiPrivate)
target_link_libraries(${LIB_NAME} PRIVATE media)