# cmake/copy_runtime_deps.cmake
# 自动筛选并复制非同目录的运行时 DLL 依赖，避免原地复制报错

if(NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "DEST_DIR must be defined")
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")
file(TO_CMAKE_PATH "${DEST_DIR}" NORM_DEST_DIR)

foreach(DLL_PATH ${DLLS})
    if(EXISTS "${DLL_PATH}")
        get_filename_component(SRC_DIR "${DLL_PATH}" DIRECTORY)
        file(TO_CMAKE_PATH "${SRC_DIR}" NORM_SRC_DIR)

        # 仅当 DLL 来源目录与目标目录不同时才执行复制
        if(NOT NORM_SRC_DIR STREQUAL NORM_DEST_DIR)
            get_filename_component(DLL_NAME "${DLL_PATH}" NAME)
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL_PATH}" "${DEST_DIR}/${DLL_NAME}"
            )
        endif()
    endif()
endforeach()
