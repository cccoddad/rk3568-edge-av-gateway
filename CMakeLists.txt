# 文件作用：定义整个项目的库分层、第三方依赖、可执行程序、安装规则和测试入口。
# 主要知识点：现代 CMake target、PUBLIC/PRIVATE 依赖传播、FetchContent 和特性开关。
cmake_minimum_required(VERSION 3.20)
project(rkav_gateway VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Options.cmake)
include(FetchContent)
find_package(Threads REQUIRED)

# 固定官方 libjpeg-turbo 源码并按上游要求使用 ExternalProject 独立构建静态库，避免依赖
# 板端未知版本的 libjpeg 动态库，也避免第三方全局 CMake 变量污染主工程。
if(RKAV_WITH_JPEG)
    include(ExternalProject)
    set(RKAV_LIBJPEG_PREFIX "${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo")
    set(RKAV_LIBJPEG_SOURCE_DIR "${RKAV_LIBJPEG_PREFIX}/src/libjpeg-turbo")
    set(RKAV_LIBJPEG_BINARY_DIR "${RKAV_LIBJPEG_PREFIX}/build")
    set(RKAV_LIBJPEG_CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_SIMD=OFF
        -DWITH_TURBOJPEG=ON
        -DWITH_TOOLS=OFF
        -DWITH_TESTS=OFF
    )
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND RKAV_LIBJPEG_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif()

    set(RKAV_LIBJPEG_DOWNLOAD_ARGS
        URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.4.1/libjpeg-turbo-3.1.4.1.tar.gz
        URL_HASH SHA256=ecae8008e2cc9ade2f2c1bb9d5e6d4fb73e7c433866a056bd82980741571a022
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if(FETCHCONTENT_SOURCE_DIR_LIBJPEG_TURBO)
        set(RKAV_LIBJPEG_SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_LIBJPEG_TURBO}")
        set(RKAV_LIBJPEG_DOWNLOAD_ARGS DOWNLOAD_COMMAND "")
    endif()

    set(RKAV_TURBOJPEG_LIBRARY
        "${RKAV_LIBJPEG_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}turbojpeg${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    ExternalProject_Add(rkav_libjpeg_turbo_external
        ${RKAV_LIBJPEG_DOWNLOAD_ARGS}
        PREFIX "${RKAV_LIBJPEG_PREFIX}"
        SOURCE_DIR "${RKAV_LIBJPEG_SOURCE_DIR}"
        BINARY_DIR "${RKAV_LIBJPEG_BINARY_DIR}"
        CMAKE_ARGS ${RKAV_LIBJPEG_CMAKE_ARGS}
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
            --target turbojpeg-static
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${RKAV_TURBOJPEG_LIBRARY}"
    )
    add_library(rkav_turbojpeg STATIC IMPORTED GLOBAL)
    set_target_properties(rkav_turbojpeg PROPERTIES
        IMPORTED_LOCATION "${RKAV_TURBOJPEG_LIBRARY}"
    )
    add_dependencies(rkav_turbojpeg rkav_libjpeg_turbo_external)
endif()

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
    src/vision/cpu/cpu_overlay.cpp
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
    RKAV_WITH_JPEG=$<BOOL:${RKAV_WITH_JPEG}>
    RKAV_WITH_FFMPEG=$<BOOL:${RKAV_WITH_FFMPEG}>
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

# FFmpeg 软件媒体基线依赖发行版提供的开发包；编码器是否含 libx264/libopenh264 在 Open 时验证。
if(RKAV_WITH_FFMPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RKAV_FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec>=58
        libavformat>=58
        libavutil>=56
        libswscale>=5
        libswresample>=3
    )
    add_library(rkav_ffmpeg
        src/media/ffmpeg/ffmpeg_encoder.cpp
        src/output/ffmpeg/ffmpeg_mp4_sink.cpp
    )
    target_include_directories(rkav_ffmpeg PUBLIC include)
    target_link_libraries(rkav_ffmpeg
        PUBLIC rkav_core rkav_project_options
        PRIVATE PkgConfig::RKAV_FFMPEG rkav_project_warnings
    )
    # Ubuntu 22.04 FFmpeg 4.4 的 channel_layout 字段在新版本被标为 deprecated；该后端
    # 有意使用旧 ABI 以匹配板端 glibc 2.35 构建基线，其他项目告警仍保持 -Werror。
    if(NOT MSVC)
        target_compile_options(rkav_ffmpeg PRIVATE -Wno-deprecated-declarations)
    endif()
endif()

# MPP 和 RGA 必须一起启用：RGA 把 RGB/BGR 转为 MPP H.264 所需的 NV12。库在目标板运行时
# 通过 dlopen 加载，交叉编译阶段只需要经过核验的头文件，避免把厂商二进制放入仓库。
if(RKAV_WITH_MPP OR RKAV_WITH_RGA)
    if(NOT RKAV_WITH_MPP OR NOT RKAV_WITH_RGA)
        message(FATAL_ERROR "RKAV_WITH_MPP and RKAV_WITH_RGA must be enabled together")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
       NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        message(FATAL_ERROR "RKAV_WITH_MPP/RKAV_WITH_RGA requires a Linux/aarch64 target")
    endif()
    set(RKAV_MPP_HEADERS_ROOT "" CACHE PATH "Rockchip MPP source or SDK root containing inc/rk_mpi.h")
    set(RKAV_RGA_HEADERS_ROOT "" CACHE PATH "Rockchip librga source or SDK root containing include/im2d.h")
    set(RKAV_MPP_INCLUDE_DIR "${RKAV_MPP_HEADERS_ROOT}/inc")
    set(RKAV_RGA_INCLUDE_DIR "${RKAV_RGA_HEADERS_ROOT}/include")
    if(NOT EXISTS "${RKAV_MPP_INCLUDE_DIR}/rk_mpi.h" OR
       NOT EXISTS "${RKAV_MPP_INCLUDE_DIR}/rk_venc_cfg.h" OR
       NOT EXISTS "${RKAV_RGA_INCLUDE_DIR}/im2d.h")
        message(FATAL_ERROR
            "Set RKAV_MPP_HEADERS_ROOT and RKAV_RGA_HEADERS_ROOT to verified Rockchip SDK headers")
    endif()

    add_library(rkav_mpp_rga
        src/media/mpp/mpp_rga_video_encoder.cpp
    )
    target_include_directories(rkav_mpp_rga
        PUBLIC include
    )
    # 厂商公开头文件使用 C 扩展；将其作为系统头文件隔离，不降低项目自身的 -Werror 标准。
    target_include_directories(rkav_mpp_rga
        SYSTEM PRIVATE "${RKAV_MPP_INCLUDE_DIR}" "${RKAV_RGA_INCLUDE_DIR}"
    )
    target_link_libraries(rkav_mpp_rga
        PUBLIC rkav_core rkav_project_options
        PRIVATE ${CMAKE_DL_LIBS} rkav_project_warnings
    )
endif()

# JPEG 解码只依赖平台无关 TurboJPEG API，真实 MJPEG 仅在推理抽帧后解码为 RGB888。
if(RKAV_WITH_JPEG)
    add_library(rkav_jpeg
        src/media/jpeg/jpeg_video_decoder.cpp
    )
    target_include_directories(rkav_jpeg
        PUBLIC include
        PRIVATE "${RKAV_LIBJPEG_SOURCE_DIR}/src" "${RKAV_LIBJPEG_BINARY_DIR}"
    )
    target_link_libraries(rkav_jpeg
        PUBLIC rkav_core rkav_project_options
        PRIVATE rkav_turbojpeg rkav_project_warnings
    )
endif()

# V4L2 仅依赖 Linux 内核 UAPI 和系统调用，不引入厂商 SDK 类型。
if(RKAV_WITH_V4L2)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "RKAV_WITH_V4L2 requires a Linux target")
    endif()
    add_library(rkav_v4l2
        src/capture/v4l2/v4l2_video_capture.cpp
    )
    target_include_directories(rkav_v4l2 PUBLIC include)
    target_link_libraries(rkav_v4l2
        PUBLIC rkav_core rkav_project_options
        PRIVATE rkav_project_warnings
    )
endif()

# ALSA 后端直接调用 Linux PCM 内核 UAPI，不依赖 libasound，因此适合旧 Buildroot 的静态链接。
if(RKAV_WITH_ALSA)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "RKAV_WITH_ALSA requires a Linux target")
    endif()
    add_library(rkav_alsa
        src/capture/alsa/alsa_audio_capture.cpp
    )
    target_include_directories(rkav_alsa PUBLIC include)
    target_link_libraries(rkav_alsa
        PUBLIC rkav_core rkav_project_options
        PRIVATE rkav_project_warnings
    )
endif()

# RKNN 后端只在显式启用时查找锁定的 1.4.0 头文件和 AArch64 运行库。
if(RKAV_WITH_RKNN)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
       NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        message(FATAL_ERROR "RKAV_WITH_RKNN requires a Linux/aarch64 target")
    endif()
    set(RKNN_SDK_ROOT "" CACHE PATH "RK356X Linux librknn_api directory")
    if(NOT RKNN_SDK_ROOT)
        message(FATAL_ERROR "Set RKNN_SDK_ROOT to runtime/RK356X/Linux/librknn_api")
    endif()
    set(RKNN_INCLUDE_DIR "${RKNN_SDK_ROOT}/include")
    set(RKNN_RUNTIME_LIBRARY "${RKNN_SDK_ROOT}/aarch64/librknnrt.so")
    if(NOT EXISTS "${RKNN_INCLUDE_DIR}/rknn_api.h" OR
       NOT EXISTS "${RKNN_RUNTIME_LIBRARY}")
        message(FATAL_ERROR "RKNN 1.4.0 development files are incomplete under ${RKNN_SDK_ROOT}")
    endif()

    add_library(rkav_yolov5_postprocess
        tools/rknn_smoke/yolov5_postprocess.c
    )
    target_include_directories(rkav_yolov5_postprocess PUBLIC tools/rknn_smoke)
    target_compile_options(rkav_yolov5_postprocess PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    )
    target_link_libraries(rkav_yolov5_postprocess PUBLIC m)

    add_library(rkav_rknn
        src/vision/rknn/rknn_inference_engine.cpp
    )
    target_include_directories(rkav_rknn
        PUBLIC include
        PRIVATE "${RKNN_INCLUDE_DIR}"
    )
    target_link_libraries(rkav_rknn
        PUBLIC rkav_core rkav_project_options
        PRIVATE rkav_yolov5_postprocess "${RKNN_RUNTIME_LIBRARY}" rkav_project_warnings
    )
    set_target_properties(rkav_rknn PROPERTIES SKIP_BUILD_RPATH TRUE)
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
if(RKAV_WITH_V4L2)
    target_link_libraries(rkav_app PUBLIC rkav_v4l2)
endif()
if(RKAV_WITH_ALSA)
    target_link_libraries(rkav_app PUBLIC rkav_alsa)
endif()
if(RKAV_WITH_JPEG)
    target_link_libraries(rkav_app PUBLIC rkav_jpeg)
endif()
if(RKAV_WITH_FFMPEG)
    target_link_libraries(rkav_app PUBLIC rkav_ffmpeg)
endif()
if(RKAV_WITH_RKNN)
    target_link_libraries(rkav_app PUBLIC rkav_rknn)
endif()
if(RKAV_WITH_MPP AND RKAV_WITH_RGA)
    target_link_libraries(rkav_app PUBLIC rkav_mpp_rga)
endif()

# main 只负责进程入口，核心逻辑通过 rkav_app 复用和测试。
add_executable(rkav-gateway app/main.cpp)
target_link_libraries(rkav-gateway PRIVATE rkav_app rkav_project_warnings rkav_project_options)
if(RKAV_WITH_RKNN)
    # 板端只有 Buildroot 的旧 libstdc++，因此随程序静态携带 C++ 运行库；glibc 和 RKNN 保持动态。
    target_link_options(rkav-gateway PRIVATE -static-libstdc++ -static-libgcc)
    set_target_properties(rkav-gateway PROPERTIES SKIP_BUILD_RPATH TRUE)
endif()

install(TARGETS rkav-gateway RUNTIME DESTINATION bin)
install(FILES config/mock.json DESTINATION etc/rkav)
if(RKAV_WITH_V4L2)
    install(FILES config/rk3568-v4l2.json DESTINATION etc/rkav)
endif()
if(RKAV_WITH_RKNN)
    install(FILES config/rk3568-rknn-rgb.json DESTINATION etc/rkav)
    if(RKAV_WITH_JPEG)
        install(FILES
            config/rk3568-rknn-mjpeg.json
            config/rk3568-rknn-mjpeg-alsa.json
            DESTINATION etc/rkav
        )
        if(RKAV_WITH_MPP AND RKAV_WITH_RGA)
            install(FILES config/rk3568-rknn-mjpeg-alsa-mpp-h264.json DESTINATION etc/rkav)
        endif()
    endif()
endif()

# 测试依赖只在显式启用时下载和编译，不影响最终板端最小构建。
if(RKAV_BUILD_TESTS)
    enable_testing()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    # Ubuntu 22.04/24.04 分别提供较旧但满足当前基础断言 API 的 GTest；优先复用系统包，
    # 系统没有可用版本时仍回退到下方固定提交，避免无谓访问 GitHub。
    if(NOT WIN32)
        find_package(GTest 1.11 CONFIG QUIET)
    endif()
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
