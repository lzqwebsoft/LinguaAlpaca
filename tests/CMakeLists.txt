# 单元测试 CMake 配置
add_executable(unit_tests
    ModelManagerTest.cpp
    LlamaCppEngineTest.cpp
    LlamaSseClientTest.cpp
    SelectionTranslateTest.cpp
    DictEngineTest.cpp
    DictFormatterTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ClipboardHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/WinUIAutomationHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/WinMediaOcrHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ScreenTextExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LlamaClient.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LlamaServer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Downloader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/WinTtsHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/dict/DictFormatter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/dict/DictEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/LlamaCppTranslationEngine.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/common
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/server
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/cli
    ${CMAKE_SOURCE_DIR}/third_party/wxWidgets/src/zlib
    ${CMAKE_SOURCE_DIR}/third_party/wxWidgets/3rdparty/catch/single_include
)

target_link_libraries(unit_tests PRIVATE
    wx::core
    wx::base
    wxzlib
    llama
    llama-common
    llama-server-impl
    llama-cli-impl
)

if(WIN32)
    target_link_libraries(unit_tests PRIVATE ole32 oleaut32 windowsapp)
endif()

add_test(NAME CoreAndEngineTests COMMAND unit_tests)

add_custom_command(TARGET unit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/third_party/wxWidgets/lib/vc_x64_dll"
        "$<TARGET_FILE_DIR:unit_tests>"
)
