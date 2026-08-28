# 文件作用：集中定义可选后端、Sanitizer 和严格告警，并用接口库向目标传播编译选项。
# 主要知识点：CMake option、INTERFACE library、生成表达式和编译/链接参数传播。
option(RKAV_BUILD_TESTS "Build automated tests" ON)
option(RKAV_ENABLE_MOCK "Build deterministic mock backends" ON)
option(RKAV_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(RKAV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(RKAV_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
option(RKAV_WITH_V4L2 "Build the V4L2 capture backend" OFF)
option(RKAV_WITH_ALSA "Build the ALSA capture backend" OFF)
option(RKAV_WITH_JPEG "Build the portable MJPEG/JPEG decoder" ON)
option(RKAV_WITH_FFMPEG "Build FFmpeg H.264/AAC encoders and MP4 output" OFF)
option(RKAV_WITH_RKNN "Build the RKNN inference backend" OFF)
option(RKAV_WITH_RGA "Build the RGA preprocessing backend" OFF)
option(RKAV_WITH_MPP "Build the MPP video encoder backend" OFF)

# options 传播功能选项；warnings 传播编译器告警，二者分开便于按 target 控制。
add_library(rkav_project_options INTERFACE)
add_library(rkav_project_warnings INTERFACE)

# MSVC 与 GCC/Clang 使用不同告警参数，但都要求较高告警级别。
if(MSVC)
    target_compile_options(rkav_project_warnings INTERFACE /W4 /permissive-)
    if(RKAV_WARNINGS_AS_ERRORS)
        target_compile_options(rkav_project_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(rkav_project_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
    )
    if(RKAV_WARNINGS_AS_ERRORS)
        target_compile_options(rkav_project_warnings INTERFACE -Werror)
    endif()
endif()

# Sanitizer 参数必须同时加到编译和链接阶段，否则运行库不会正确接入。
if(RKAV_ENABLE_ASAN OR RKAV_ENABLE_UBSAN)
    if(MSVC)
        message(FATAL_ERROR "This project currently configures sanitizers only for GCC/Clang")
    endif()

    set(RKAV_SANITIZERS "")
    if(RKAV_ENABLE_ASAN)
        list(APPEND RKAV_SANITIZERS "address")
    endif()
    if(RKAV_ENABLE_UBSAN)
        list(APPEND RKAV_SANITIZERS "undefined")
    endif()
    list(JOIN RKAV_SANITIZERS "," RKAV_SANITIZER_LIST)

    target_compile_options(rkav_project_options INTERFACE
        -fsanitize=${RKAV_SANITIZER_LIST}
        -fno-omit-frame-pointer
    )
    target_link_options(rkav_project_options INTERFACE
        -fsanitize=${RKAV_SANITIZER_LIST}
    )
endif()
