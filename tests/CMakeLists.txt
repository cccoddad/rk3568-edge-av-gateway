# 文件作用：把全部单元/集成测试编译成一个可执行文件，并让 CTest 自动发现 TEST。
# 主要知识点：GoogleTest target、gtest_discover_tests 和测试依赖链接。
include(GoogleTest)

add_executable(rkav_tests
    unit/core_test.cpp
    unit/cpu_overlay_test.cpp
    unit/h264_elementary_sink_test.cpp
    unit/queue_test.cpp
    unit/config_test.cpp
    unit/mock_components_test.cpp
    integration/application_test.cpp
)
if(RKAV_WITH_JPEG)
    target_sources(rkav_tests PRIVATE unit/jpeg_video_decoder_test.cpp)
endif()
target_link_libraries(rkav_tests
    PRIVATE
        GTest::gtest_main
        rkav_app
        rkav_project_options
        rkav_project_warnings
)

# 交叉编译时构建机不能直接运行 ARM64 测试程序，因此从源码登记测试；原生构建仍使用
# 可执行文件发现，保证模板化测试和测试名完全准确。
if(CMAKE_CROSSCOMPILING)
    gtest_add_tests(TARGET rkav_tests)
else()
    gtest_discover_tests(rkav_tests DISCOVERY_TIMEOUT 30)
endif()
