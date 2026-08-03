# 单元测试 CMake 配置
add_executable(unit_tests
    TranslationServiceTest.cpp
    LlamaCppEngineTest.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/engine/MockTranslationEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/engine/LlamaCppTranslationEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/repository/InMemoryHistoryRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/repository/IniConfigRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/application/service/TranslationService.cpp
    ${CMAKE_SOURCE_DIR}/src/application/service/ConfigurationService.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include
    ${CMAKE_SOURCE_DIR}/third_party/wxWidgets/3rdparty/catch/single_include
)

target_link_libraries(unit_tests PRIVATE
    wx::core
    wx::base
    llama
)

add_test(NAME DomainAndAppTests COMMAND unit_tests)

add_custom_command(TARGET unit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/third_party/wxWidgets/lib/vc_x64_dll"
        "$<TARGET_FILE_DIR:unit_tests>"
)
