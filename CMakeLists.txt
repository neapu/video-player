cmake_minimum_required(VERSION 3.31)
project(qrhi_player VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:SOURCE_DIR="${CMAKE_SOURCE_DIR}">
)

if (MSVC)
    add_compile_options(/utf-8)
endif ()

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
# spdlog 默认用窄字符 fopen 打开日志文件，无法处理含非 ASCII 字符（如中文用户名）的
# %APPDATA% 路径。必须在 FetchContent_MakeAvailable 之前定义，
# 保证 spdlog 库本身与本项目使用一致的 filename_t，否则会因 ODR 冲突崩溃
if (WIN32)
    add_compile_definitions(SPDLOG_WCHAR_FILENAMES)
endif()
FetchContent_MakeAvailable(spdlog)

add_subdirectory(view)
add_subdirectory(media)