cmake_minimum_required(VERSION 3.16)
project(LinguaAlpaca LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一可执行文件与动态库 (DLL) 的输出路径到 build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 禁用 wxWidgets 自带示例/演示，避免额外构建
set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)

# 先构建/引入第三方依赖
add_subdirectory(third_party/wxWidgets)
add_subdirectory(third_party/llama.cpp)

# 创建你的可执行文件（GUI 程序使用 WIN32 子系统）
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
)

# 链接 wxWidgets 和 llama.cpp
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    wx::core
    wx::base
    llama
)

# 自动将第三方 DLL (如 wxWidgets 生成的 DLL) 复制到 .exe 所在目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/third_party/wxWidgets/lib/vc_x64_dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)