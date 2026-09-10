cmake_minimum_required(VERSION 3.16)
project(LinguaAlpaca LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /FS)
    # Release 模式编译优化、字符串合并与函数级链接，配合链接器死代码消除
    add_compile_options("$<$<CONFIG:Release>:/Gy;/GF;/O2>")
    add_link_options("$<$<CONFIG:Release>:/OPT:REF;/OPT:ICF>")
endif()

# 统一二进制与库输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 禁用 wxWidgets 示例与测试
set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)

# 引入第三方依赖
add_subdirectory(third_party/wxWidgets)

# 自动为 third_party/llama.cpp 应用本地补丁 (patches/*.patch)
find_package(Git QUIET)
file(GLOB LLAMA_PATCH_FILES "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch")

if(GIT_FOUND)
    foreach(PATCH_FILE ${LLAMA_PATCH_FILES})
        get_filename_component(PATCH_NAME "${PATCH_FILE}" NAME)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} apply --check "${PATCH_FILE}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp"
            RESULT_VARIABLE PATCH_CHECK_RES
            OUTPUT_QUIET ERROR_QUIET
        )

        if(PATCH_CHECK_RES EQUAL 0)
            message(STATUS "[LinguaAlpaca] Applying llama.cpp patch: ${PATCH_NAME}...")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} apply "${PATCH_FILE}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp"
                RESULT_VARIABLE PATCH_APPLY_RES
            )
            if(NOT PATCH_APPLY_RES EQUAL 0)
                message(WARNING "[LinguaAlpaca] Failed to apply ${PATCH_NAME}!")
            endif()
        endif()
    endforeach()
endif()

set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXTRA_TOOLS OFF CACHE BOOL "" FORCE) # 仅构建必要的 server 和 mtmd，精简跳过 18+ 个 CLI 压测与量化工具
set(LLAMA_BUILD_UI OFF CACHE BOOL "" FORCE)
set(LLAMA_USE_PREBUILT_UI OFF CACHE BOOL "" FORCE)
if(APPLE)
    # macOS 平台默认启用 Apple Silicon Metal 原生 GPU 加速，关闭 Vulkan
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
else()
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)  # Windows / Linux 默认开启Vulkan SDK加速
endif()
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
    src/core/SelectionContext.hpp
    src/core/SelectionService.hpp
    src/core/SelectionService.cpp
    src/core/llama/LlamaServer.hpp
    src/core/llama/LlamaServer.cpp
    src/core/llama/LlamaClient.hpp
    src/core/llama/LlamaClient.cpp
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
    src/core/markdown/MarkdownFormatter.hpp
    src/core/markdown/MarkdownFormatter.cpp
    src/core/table/TableParser.hpp
    src/core/table/TableParser.cpp
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
    src/ui/theme/PlatformThemeHelper.hpp
    src/ui/theme/PlatformThemeHelper.cpp
    src/ui/widgets/CustomButton.hpp
    src/ui/widgets/CustomButton.cpp
    src/ui/widgets/CustomChoice.hpp
    src/ui/widgets/CustomChoice.cpp
    src/ui/widgets/CustomInputBox.hpp
    src/ui/widgets/CustomInputBox.cpp
    src/ui/widgets/LeftAlignedRadioBox.hpp
    src/ui/widgets/StatusBadge.hpp
    src/ui/widgets/StatusBadge.cpp
    src/ui/widgets/SidebarNav.hpp
    src/ui/widgets/SidebarNav.cpp
    src/ui/widgets/CardPanel.hpp
    src/ui/widgets/CardPanel.cpp
    src/ui/widgets/CustomTableView.hpp
    src/ui/widgets/CustomTableView.cpp
    src/ui/widgets/LanguageBar.hpp
    src/ui/widgets/LanguageBar.cpp
    src/ui/widgets/WelcomeModelDialog.hpp
    src/ui/widgets/WelcomeModelDialog.cpp
    src/ui/widgets/AboutDialog.hpp
    src/ui/widgets/AboutDialog.cpp
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
    src/ui/widgets/SuggestListBox.hpp
    src/ui/widgets/SuggestListBox.cpp
    src/ui/widgets/SplashScreen.hpp
    src/ui/widgets/SplashScreen.cpp
    src/ui/widgets/AppTaskBarIcon.hpp
    src/ui/widgets/AppTaskBarIcon.cpp
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
    src/ui/MainFrame.hpp
    src/ui/MainFrame.cpp
)

# 平台资源文件
set(APP_RESOURCES "")
if(WIN32)
    set(APP_RESOURCES resources/app.rc)
elseif(APPLE)
    set(APP_RESOURCES resources/app_icon.icns)
    set_source_files_properties(resources/app_icon.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

# 创建主可执行程序 (Windows GUI / macOS App Bundle)
add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
    src/main.cpp
    ${CORE_SOURCES}
    ${ENGINE_SOURCES}
    ${UI_SOURCES}
    ${APP_RESOURCES}
)

# 确保编译主程序前已构建 llama-server 可执行程序
add_dependencies(${PROJECT_NAME} llama-server)

# 包含头文件路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/common
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd
)

# 链接 wxWidgets 与核心库
target_link_libraries(${PROJECT_NAME} PRIVATE
    wx::core
    wx::base
    wx::adv
    llama
    llama-common
    cpp-httplib
    mtmd
)

if(TARGET wxzlib)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wxWidgets/src/zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE wxzlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ole32 oleaut32 windowsapp)
    # 自动仅复制 Windows 程序实际依赖的动态链接库 (Runtime DLLs) 及 llama-server
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DDEST_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
            "-DDLLS=$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>;$<TARGET_RUNTIME_DLLS:llama-server>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_runtime_deps.cmake"
    )
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "LinguaAlpaca"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.2"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.2"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.zqluo.linguaalpaca"
        MACOSX_BUNDLE_ICON_FILE "app_icon.icns"
    )
    set_source_files_properties(
        src/core/SelectionService.cpp
        src/core/ClipboardHelper.cpp
        src/core/WinUIAutomationHelper.cpp
        src/ui/widgets/FloatingIconFrame.cpp
        src/ui/widgets/TranslationBubbleFrame.cpp
        src/ui/widgets/TextCtrl.cpp
        src/ui/theme/IconManager.cpp
        src/ui/theme/PlatformThemeHelper.cpp
        src/ui/widgets/CustomInputBox.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
    set_source_files_properties(
        src/core/WinTtsHelper.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework ApplicationServices"
        "-framework Carbon"
        "-framework AVFoundation"
    )
endif()

# 复制 resources 资源目录与 macOS App Bundle 整合
if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.icns"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/app_icon.icns"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../../../resources"
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../../../LinguaAlpaca"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "LinguaAlpaca.app/Contents/MacOS/LinguaAlpaca"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../../../LinguaAlpaca"
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/llama-server"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "$<TARGET_FILE_DIR:llama-server>/llama-server"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/llama-server"
        COMMAND /usr/bin/touch
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../.."
    )

    # macOS 独立发布包打包 Target (包含 dylib 内嵌、rpath 修正、签名与 DMG 制作)
    add_custom_target(package_mac
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_mac.sh" "${CMAKE_BINARY_DIR}"
        DEPENDS ${PROJECT_NAME} llama-server
        COMMENT "[LinguaAlpaca] Packaging standalone macOS Application Bundle & DMG..."
        VERBATIM
    )
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
    )
endif()

# 单元测试模块
enable_testing()
add_subdirectory(tests)