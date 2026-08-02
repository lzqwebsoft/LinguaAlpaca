cmake_minimum_required(VERSION 3.16)
project(LinguaAlpaca LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
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
add_subdirectory(third_party/llama.cpp)

# 收集项目源代码按层组织
set(DOMAIN_SOURCES
    src/domain/model/Language.hpp
    src/domain/model/TranslationTask.hpp
    src/domain/model/HistoryRecord.hpp
    src/domain/model/AppTheme.hpp
    src/domain/model/AppConfig.hpp
    src/domain/repository/ITranslationEngine.hpp
    src/domain/repository/IHistoryRepository.hpp
)

set(APP_SOURCES
    src/application/dto/TranslationDto.hpp
    src/application/service/TranslationService.hpp
    src/application/service/TranslationService.cpp
    src/application/service/ConfigurationService.hpp
    src/application/service/ConfigurationService.cpp
    src/application/service/ThemeManager.hpp
)

set(INFRA_SOURCES
    src/infrastructure/engine/MockTranslationEngine.hpp
    src/infrastructure/engine/MockTranslationEngine.cpp
    src/infrastructure/engine/LlamaCppTranslationEngine.hpp
    src/infrastructure/engine/LlamaCppTranslationEngine.cpp
    src/infrastructure/downloader/ModelDownloader.hpp
    src/infrastructure/downloader/ModelDownloader.cpp
    src/infrastructure/repository/InMemoryHistoryRepository.hpp
    src/infrastructure/repository/InMemoryHistoryRepository.cpp
    src/infrastructure/repository/IniConfigRepository.hpp
    src/infrastructure/repository/IniConfigRepository.cpp
)

set(UI_SOURCES
    src/presentation/theme/ThemeColors.hpp
    src/presentation/components/CustomButton.hpp
    src/presentation/components/CustomButton.cpp
    src/presentation/components/SidebarNav.hpp
    src/presentation/components/SidebarNav.cpp
    src/presentation/components/CardPanel.hpp
    src/presentation/components/CardPanel.cpp
    src/presentation/components/LanguageSelectorBar.hpp
    src/presentation/components/LanguageSelectorBar.cpp
    src/presentation/components/WelcomeModelDialog.hpp
    src/presentation/components/WelcomeModelDialog.cpp
    src/presentation/views/TextTranslationView.hpp
    src/presentation/views/TextTranslationView.cpp
    src/presentation/views/SettingsView.hpp
    src/presentation/views/SettingsView.cpp
    src/presentation/views/PlaceholderView.hpp
    src/presentation/views/MainFrame.hpp
    src/presentation/views/MainFrame.cpp
)

# 创建主可执行程序 (Windows GUI)
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    ${DOMAIN_SOURCES}
    ${APP_SOURCES}
    ${INFRA_SOURCES}
    ${UI_SOURCES}
)

# 包含头文件路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接 wxWidgets 与核心库
target_link_libraries(${PROJECT_NAME} PRIVATE
    wx::core
    wx::base
    llama
)

# 自动将第三方 DLL 复制到 output 目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/third_party/wxWidgets/lib/vc_x64_dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# 单元测试模块
enable_testing()
add_subdirectory(tests)