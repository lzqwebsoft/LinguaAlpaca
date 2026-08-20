cmake_minimum_required(VERSION 3.16)
project(LinguaAlpaca LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /FS)
endif()

# 统一二进制与库输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 禁用 wxWidgets 自动构建示例
set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)

# 引入第三方依赖
add_subdirectory(third_party/wxWidgets)

# 自动为 third_party/llama.cpp 应用本地补丁 (PR #19357)
find_package(Git QUIET)
set(LLAMA_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/llama-pr-19357.patch")

if(GIT_FOUND AND EXISTS "${LLAMA_PATCH_FILE}")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} apply --check "${LLAMA_PATCH_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp"
        RESULT_VARIABLE PATCH_CHECK_RES
        OUTPUT_QUIET ERROR_QUIET
    )

    if(PATCH_CHECK_RES EQUAL 0)
        message(STATUS "[LinguaAlpaca] Applying llama.cpp PR #19357 patch...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} apply "${LLAMA_PATCH_FILE}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp"
            RESULT_VARIABLE PATCH_APPLY_RES
        )
        if(NOT PATCH_APPLY_RES EQUAL 0)
            message(WARNING "[LinguaAlpaca] Failed to apply llama.cpp patch!")
        endif()
    endif()
endif()

set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_UI OFF CACHE BOOL "" FORCE)
set(LLAMA_USE_PREBUILT_UI OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN ON CACHE BOOL "" FORCE)  # 默认开始Vulkan SDK加速
add_subdirectory(third_party/llama.cpp)

# 模块化源码组织
set(CORE_SOURCES
    src/core/Types.hpp
    src/core/Logger.hpp
    src/core/Logger.cpp
    src/core/Config.hpp
    src/core/Config.cpp
    src/core/ClipboardHelper.hpp
    src/core/ClipboardHelper.cpp
    src/core/WinUIAutomationHelper.hpp
    src/core/WinUIAutomationHelper.cpp
    src/core/WinMediaOcrHelper.hpp
    src/core/WinMediaOcrHelper.cpp
    src/core/ScreenTextExtractor.hpp
    src/core/ScreenTextExtractor.cpp
    src/core/SelectionService.hpp
    src/core/SelectionService.cpp
    src/core/LlamaServer.hpp
    src/core/LlamaServer.cpp
    src/core/LlamaClient.hpp
    src/core/LlamaClient.cpp
    src/core/ModelManager.hpp
    src/core/ModelManager.cpp
    src/core/Downloader.hpp
    src/core/Downloader.cpp
    src/core/WinTtsHelper.hpp
    src/core/WinTtsHelper.cpp
    src/core/dict/DictFormatter.hpp
    src/core/dict/DictFormatter.cpp
    src/core/dict/DictEngine.hpp
    src/core/dict/DictEngine.cpp
)

set(ENGINE_SOURCES
    src/engine/IEngine.hpp
    src/engine/LlamaCppTranslationEngine.hpp
    src/engine/LlamaCppTranslationEngine.cpp
    src/engine/LlamaCppOcrEngine.hpp
    src/engine/LlamaCppOcrEngine.cpp
)

set(UI_SOURCES
    src/ui/theme/Theme.hpp
    src/ui/theme/AppIcons.hpp
    src/ui/theme/IconManager.hpp
    src/ui/theme/IconManager.cpp
    src/ui/widgets/CustomButton.hpp
    src/ui/widgets/CustomButton.cpp
    src/ui/widgets/StatusBadge.hpp
    src/ui/widgets/StatusBadge.cpp
    src/ui/widgets/SidebarNav.hpp
    src/ui/widgets/SidebarNav.cpp
    src/ui/widgets/CardPanel.hpp
    src/ui/widgets/CardPanel.cpp
    src/ui/widgets/LanguageBar.hpp
    src/ui/widgets/LanguageBar.cpp
    src/ui/widgets/WelcomeModelDialog.hpp
    src/ui/widgets/WelcomeModelDialog.cpp
    src/ui/widgets/ImagePreviewDialog.hpp
    src/ui/widgets/ImagePreviewDialog.cpp
    src/ui/widgets/FloatingIconFrame.hpp
    src/ui/widgets/FloatingIconFrame.cpp
    src/ui/widgets/TranslationBubbleFrame.hpp
    src/ui/widgets/TranslationBubbleFrame.cpp
    src/ui/widgets/SplitterWindow.hpp
    src/ui/widgets/SplitterWindow.cpp
    src/ui/widgets/TextCtrl.hpp
    src/ui/widgets/TextCtrl.cpp
    src/ui/widgets/ScrollBar.hpp
    src/ui/widgets/ScrollBar.cpp
    src/ui/widgets/SearchInputBox.hpp
    src/ui/widgets/SearchInputBox.cpp
    src/ui/widgets/SuggestListBox.hpp
    src/ui/widgets/SuggestListBox.cpp
    src/ui/TextView.hpp
    src/ui/TextView.cpp
    src/ui/OcrView.hpp
    src/ui/OcrView.cpp
    src/ui/DictView.hpp
    src/ui/DictView.cpp
    src/ui/LogView.hpp
    src/ui/LogView.cpp
    src/ui/SettingsView.hpp
    src/ui/SettingsView.cpp
    src/ui/PlaceholderView.hpp
    src/ui/MainFrame.hpp
    src/ui/MainFrame.cpp
)

# 平台资源文件
set(APP_RESOURCES "")
if(WIN32)
    set(APP_RESOURCES resources/app.rc)
endif()

# 创建主可执行程序 (Windows GUI)
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    ${CORE_SOURCES}
    ${ENGINE_SOURCES}
    ${UI_SOURCES}
    ${APP_RESOURCES}
)

# 包含头文件路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/common
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/tools/server
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/tools/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wxWidgets/src/zlib
)

# 链接 wxWidgets 与核心库
target_link_libraries(${PROJECT_NAME} PRIVATE
    wx::core
    wx::base
    wxzlib
    llama
    llama-common
    llama-server-impl
    llama-cli-impl
    mtmd
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ole32 oleaut32 windowsapp)
endif()

# 自动将第三方 DLL 与 resources 资源目录复制到 output 目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/third_party/wxWidgets/lib/vc_x64_dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
)

# 单元测试模块
enable_testing()
add_subdirectory(tests)