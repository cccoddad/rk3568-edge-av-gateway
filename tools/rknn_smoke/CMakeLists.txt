cmake_minimum_required(VERSION 3.20)
project(rknn_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_RPATH TRUE)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(FATAL_ERROR "rknn-smoke must be cross-compiled for Linux/aarch64")
endif()

set(RKNN_SDK_ROOT "" CACHE PATH "RK356X Linux librknn_api directory")
if(NOT RKNN_SDK_ROOT)
    message(FATAL_ERROR "Set RKNN_SDK_ROOT to runtime/RK356X/Linux/librknn_api")
endif()

set(RKNN_INCLUDE_DIR "${RKNN_SDK_ROOT}/include")
set(RKNN_RUNTIME_LIBRARY "${RKNN_SDK_ROOT}/aarch64/librknnrt.so")
if(NOT EXISTS "${RKNN_INCLUDE_DIR}/rknn_api.h")
    message(FATAL_ERROR "RKNN header not found: ${RKNN_INCLUDE_DIR}/rknn_api.h")
endif()
if(NOT EXISTS "${RKNN_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "RKNN runtime library not found: ${RKNN_RUNTIME_LIBRARY}")
endif()

find_package(Threads REQUIRED)

add_executable(rknn-smoke main.cpp)
target_include_directories(rknn-smoke PRIVATE "${RKNN_INCLUDE_DIR}")
target_link_libraries(rknn-smoke PRIVATE
    "${RKNN_RUNTIME_LIBRARY}"
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
target_compile_options(rknn-smoke PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Werror
)
# Keep the C++ and GCC runtimes self-contained while loading librknnrt.so from the board rootfs.
target_link_options(rknn-smoke PRIVATE -static-libstdc++ -static-libgcc)

install(TARGETS rknn-smoke RUNTIME DESTINATION bin)
