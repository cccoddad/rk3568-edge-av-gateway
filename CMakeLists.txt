# 文件作用：定义整个项目的库分层、第三方依赖、可执行程序、安装规则和测试入口。
# 主要知识点：现代 CMake target、PUBLIC/PRIVATE 依赖传播、FetchContent 和特性开关。
cmake_minimum_required(VERSION 3.20)
project(rkav_gateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Options.cmake)
include(FetchContent)
find_package(Threads REQUIRED)

find_package(nlohmann_json 3.11.3 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# rkav_core 只放平台无关类型、算法和配置，不能依赖 V4L2/RKNN 等硬件 SDK。
add_library(rkav_core
    src/common/clock.cpp
    src/common/error.cpp
    src/common/logger.cpp
    src/common/types.cpp
    src/config/config.cpp
    src/monitor/metrics.cpp
    src/vision/geometry.cpp
)
target_include_directories(rkav_core PUBLIC include)
target_link_libraries(rkav_core
    PUBLIC
        nlohmann_json::nlohmann_json
        Threads::Threads
        rkav_project_options
    PRIVATE
        rkav_project_warnings
)
target_compile_definitions(rkav_core PUBLIC
    RKAV_VERSION="${PROJECT_VERSION}"
    RKAV_ENABLE_MOCK=$<BOOL:${RKAV_ENABLE_MOCK}>
    RKAV_WITH_V4L2=$<BOOL:${RKAV_WITH_V4L2}>
    RKAV_WITH_ALSA=$<BOOL:${RKAV_WITH_ALSA}>
    RKAV_WITH_RKNN=$<BOOL:${RKAV_WITH_RKNN}>
    RKAV_WITH_RGA=$<BOOL:${RKAV_WITH_RGA}>
    RKAV_WITH_MPP=$<BOOL:${RKAV_WITH_MPP}>
)

# Mock 单独成库，未来真实后端可并存而无需删除测试后端。
if(RKAV_ENABLE_MOCK)
    add_library(rkav_mock
        src/capture/mock/mock_audio_capture.cpp
        src/capture/mock/mock_video_capture.cpp
        src/media/mock/checksum_encoder.cpp
        src/vision/mock/mock_inference_engine.cpp
    )
    target_include_directories(rkav_mock PUBLIC include)
    target_link_libraries(rkav_mock
        PUBLIC rkav_core rkav_project_options
        PRIVATE rkav_project_warnings
    )
endif()

# rkav_app 负责管道组装、输出路由和生命周期。
add_library(rkav_app
    src/app/application.cpp
    src/output/packet_router.cpp
    src/output/sinks.cpp
)
target_include_directories(rkav_app PUBLIC include)
target_link_libraries(rkav_app
    PUBLIC rkav_core rkav_project_options
    PRIVATE rkav_project_warnings
)
if(RKAV_ENABLE_MOCK)
    target_link_libraries(rkav_app PUBLIC rkav_mock)
endif()

# main 只负责进程入口，核心逻辑通过 rkav_app 复用和测试。
add_executable(rkav-gateway app/main.cpp)
target_link_libraries(rkav-gateway PRIVATE rkav_app rkav_project_warnings rkav_project_options)

install(TARGETS rkav-gateway RUNTIME DESTINATION bin)
install(FILES config/mock.json DESTINATION etc/rkav)

# 测试依赖只在显式启用时下载和编译，不影响最终板端最小构建。
if(RKAV_BUILD_TESTS)
    enable_testing()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    find_package(GTest 1.15.2 CONFIG QUIET)
    if(NOT GTest_FOUND)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    add_subdirectory(tests)
endif()
