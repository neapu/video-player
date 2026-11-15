set(EXE_NAME video_player)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(${EXE_NAME}
    main.cpp
    MainWindow.h
    MainWindow.cpp
    Controller.h
    Controller.cpp
    LogManager.h
    LogManager.cpp
    VideoSink.h

    components/PlayOrPauseButton.h
    components/PlayOrPauseButton.cpp
    components/PlaybackSlider.h
    components/PlaybackSlider.cpp
    components/VolumeButton.h
    components/VolumeButton.cpp
)

qt_add_resources(${EXE_NAME} "player_icons"
    PREFIX "/icons"
    BASE "images"
    FILES
        images/play.svg
        images/pause.svg
        images/stop.svg
        images/volume-up.svg
        images/volume-down.svg
        images/volume-mute.svg
)

# add_subdirectory(gl_video_renderer)
add_subdirectory(rhi_video_renderer)
add_subdirectory(audio_renderer)

target_link_libraries(${EXE_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    spdlog
    media
    # gl_video_renderer
    rhi_video_renderer
    audio_renderer
)

if (WIN32)
    # Debug 配置为控制台应用程序，方便调试；Release 保持 GUI 程序。
    # 用生成器表达式而非 CMAKE_BUILD_TYPE 判断，兼容多配置生成器（VS / Ninja Multi-Config）
    set_target_properties(${EXE_NAME} PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    )

    file(GLOB FFMPEG_DLLS CONFIGURE_DEPENDS "${FFMPEG_DIR}/bin/*.dll")
    if (NOT FFMPEG_DLLS)
        message(FATAL_ERROR "No FFmpeg DLLs found in ${FFMPEG_DIR}/bin.")
    endif()
    add_custom_command(TARGET ${EXE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FFMPEG_DLLS}
            $<TARGET_FILE_DIR:${EXE_NAME}>
        COMMENT "Deploying FFmpeg DLLs..."
        VERBATIM
    )

    # 配置Windows平台下，自动拷贝Qt依赖库到bin目录
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${EXE_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" 
                --$<LOWER_CASE:$<CONFIG>>
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                $<TARGET_FILE:${EXE_NAME}>
            COMMENT "Running windeployqt to deploy Qt dependencies..."
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found. Please ensure it is installed and available in the PATH.")
    endif()
endif()