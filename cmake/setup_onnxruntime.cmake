# cmake/setup_onnxruntime.cmake
# 自动检测操作系统架构，并通过代理动态探测并下载安装最新官方 ONNX Runtime 预编译二进制库

set(ORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
set(ORT_INCLUDE_DIR "${ORT_ROOT}/include")
set(ORT_LIB_DIR "${ORT_ROOT}/lib")

# ==============================================================================
# 1. 代理设置：优先使用环境变量或指定代理 (默认 http://192.168.2.21:6081)
# ==============================================================================
if(NOT DEFINED ORT_PROXY)
    if(DEFINED ENV{https_proxy})
        set(ORT_PROXY "$ENV{https_proxy}")
    elseif(DEFINED ENV{http_proxy})
        set(ORT_PROXY "$ENV{http_proxy}")
    else()
        set(ORT_PROXY "http://192.168.2.21:6081")
    endif()
endif()

if(ORT_PROXY)
    set(ENV{http_proxy} "${ORT_PROXY}")
    set(ENV{https_proxy} "${ORT_PROXY}")
    set(ENV{HTTP_PROXY} "${ORT_PROXY}")
    set(ENV{HTTPS_PROXY} "${ORT_PROXY}")
endif()

# ==============================================================================
# 2. 动态检测 GitHub 官方最新发布版本 (不固定版本)
# ==============================================================================
if(NOT DEFINED ORT_VERSION OR ORT_VERSION STREQUAL "" OR ORT_VERSION STREQUAL "latest")
    find_program(CURL_EXECUTABLE curl)
    set(DETECTED_ORT_VERSION "")

    if(CURL_EXECUTABLE)
        set(CURL_HEADER_ARGS -s -I "https://github.com/microsoft/onnxruntime/releases/latest")
        if(ORT_PROXY)
            list(PREPEND CURL_HEADER_ARGS --proxy "${ORT_PROXY}")
        endif()
        execute_process(
            COMMAND "${CURL_EXECUTABLE}" ${CURL_HEADER_ARGS}
            OUTPUT_VARIABLE LATEST_REDIRECT_HEADER
            ERROR_QUIET
            RESULT_VARIABLE CURL_HDR_RES
        )
        if(CURL_HDR_RES EQUAL 0 AND LATEST_REDIRECT_HEADER MATCHES "[Ll]ocation:[ \t]*https://github.com/microsoft/onnxruntime/releases/tag/v([0-9]+\\.[0-9]+\\.[0-9]+)")
            set(DETECTED_ORT_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()

    if(NOT DETECTED_ORT_VERSION)
        # 备选：通过 GitHub REST API 探测最新 release tag
        set(GITHUB_API_URL "https://api.github.com/repos/microsoft/onnxruntime/releases/latest")
        file(DOWNLOAD "${GITHUB_API_URL}" "${CMAKE_BINARY_DIR}/_ort_latest.json"
            STATUS API_STATUS
            LOG API_LOG
            TIMEOUT 10
            USER_AGENT "LinguaAlpaca-CMake"
        )
        list(GET API_STATUS 0 API_CODE)
        if(API_CODE EQUAL 0 AND EXISTS "${CMAKE_BINARY_DIR}/_ort_latest.json")
            file(READ "${CMAKE_BINARY_DIR}/_ort_latest.json" API_JSON_CONTENT)
            if(API_JSON_CONTENT MATCHES "\"tag_name\":[ \t]*\"v([0-9]+\\.[0-9]+\\.[0-9]+)\"")
                set(DETECTED_ORT_VERSION "${CMAKE_MATCH_1}")
            endif()
            file(REMOVE "${CMAKE_BINARY_DIR}/_ort_latest.json")
        endif()
    endif()

    if(DETECTED_ORT_VERSION)
        set(ORT_VERSION "${DETECTED_ORT_VERSION}")
        message(STATUS "[LinguaAlpaca] Detected latest ONNX Runtime release from GitHub: v${ORT_VERSION}")
    else()
        if(EXISTS "${ORT_ROOT}/version.txt")
            file(READ "${ORT_ROOT}/version.txt" LOCAL_SAVED_VER)
            string(STRIP "${LOCAL_SAVED_VER}" LOCAL_SAVED_VER)
            set(ORT_VERSION "${LOCAL_SAVED_VER}")
        else()
            set(ORT_VERSION "1.30.0")
        endif()
        message(STATUS "[LinguaAlpaca] Could not query latest version online, using fallback: v${ORT_VERSION}")
    endif()
endif()

# ==============================================================================
# 3. 平台资源资产映射
# ==============================================================================
if(WIN32)
    set(ORT_ASSET "onnxruntime-win-x64-${ORT_VERSION}")
    set(ORT_EXT "zip")
    set(ORT_IMPLIB "${ORT_LIB_DIR}/onnxruntime.lib")
    set(ORT_BIN "${ORT_LIB_DIR}/onnxruntime.dll")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR "${CMAKE_OSX_ARCHITECTURES}" MATCHES "arm64")
        set(ORT_ASSET "onnxruntime-osx-arm64-${ORT_VERSION}")
    else()
        set(ORT_ASSET "onnxruntime-osx-universal2-${ORT_VERSION}")
    endif()
    set(ORT_EXT "tgz")
    set(ORT_BIN "${ORT_LIB_DIR}/libonnxruntime.dylib")
else()
    set(ORT_ASSET "onnxruntime-linux-x64-${ORT_VERSION}")
    set(ORT_EXT "tgz")
    set(ORT_BIN "${ORT_LIB_DIR}/libonnxruntime.so")
endif()

# 校验本地是否已经存在预编译头文件与二进制库，并对比版本
set(ORT_ALREADY_READY FALSE)
set(INSTALLED_ORT_VERSION "")
if(EXISTS "${ORT_ROOT}/version.txt")
    file(READ "${ORT_ROOT}/version.txt" INSTALLED_ORT_VERSION)
    string(STRIP "${INSTALLED_ORT_VERSION}" INSTALLED_ORT_VERSION)
endif()

if(EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    if(WIN32 AND EXISTS "${ORT_IMPLIB}" AND EXISTS "${ORT_BIN}")
        set(ORT_ALREADY_READY TRUE)
    elseif(EXISTS "${ORT_BIN}")
        set(ORT_ALREADY_READY TRUE)
    endif()

    # 如果检测到本地已安装版本落后于最新版本，则触发升级
    if(ORT_ALREADY_READY AND NOT INSTALLED_ORT_VERSION STREQUAL "" AND NOT INSTALLED_ORT_VERSION STREQUAL ORT_VERSION)
        message(STATUS "[LinguaAlpaca] Installed ONNX Runtime (v${INSTALLED_ORT_VERSION}) is older than latest (v${ORT_VERSION}), upgrading...")
        set(ORT_ALREADY_READY FALSE)
    endif()
endif()

if(NOT ORT_ALREADY_READY)
    message(STATUS "[LinguaAlpaca] ONNX Runtime prebuilt binaries not found or outdated.")
    message(STATUS "[LinguaAlpaca] Downloading latest official ONNX Runtime ${ORT_VERSION} (${ORT_ASSET})...")
    if(ORT_PROXY)
        message(STATUS "[LinguaAlpaca] Using proxy: ${ORT_PROXY}")
    endif()

    set(ORT_DOWNLOAD_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_ASSET}.${ORT_EXT}")
    set(ORT_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_ort_download")
    set(ORT_ARCHIVE_FILE "${ORT_DOWNLOAD_DIR}/${ORT_ASSET}.${ORT_EXT}")

    file(MAKE_DIRECTORY "${ORT_DOWNLOAD_DIR}")

    # 优先采用 curl 命令行工具 (带 --proxy 参数，下载更稳定且支持代理重试)
    find_program(CURL_EXECUTABLE curl)
    set(DOWNLOAD_SUCCESS FALSE)

    if(CURL_EXECUTABLE)
        message(STATUS "[LinguaAlpaca] Downloading via curl with proxy...")
        set(CURL_ARGS -L -f -o "${ORT_ARCHIVE_FILE}" "${ORT_DOWNLOAD_URL}")
        if(ORT_PROXY)
            list(PREPEND CURL_ARGS --proxy "${ORT_PROXY}")
        endif()
        execute_process(
            COMMAND "${CURL_EXECUTABLE}" ${CURL_ARGS}
            RESULT_VARIABLE CURL_RES
        )
        if(CURL_RES EQUAL 0)
            set(DOWNLOAD_SUCCESS TRUE)
        else()
            message(WARNING "[LinguaAlpaca] curl download returned code ${CURL_RES}, falling back to CMake file(DOWNLOAD)...")
        endif()
    endif()

    if(NOT DOWNLOAD_SUCCESS)
        file(DOWNLOAD "${ORT_DOWNLOAD_URL}" "${ORT_ARCHIVE_FILE}"
            SHOW_PROGRESS
            STATUS ORT_DOWNLOAD_STATUS
            LOG ORT_DOWNLOAD_LOG
        )
        list(GET ORT_DOWNLOAD_STATUS 0 ORT_STATUS_CODE)
        list(GET ORT_DOWNLOAD_STATUS 1 ORT_STATUS_MSG)
        if(NOT ORT_STATUS_CODE EQUAL 0)
            message(FATAL_ERROR "[LinguaAlpaca] Failed to download ONNX Runtime from ${ORT_DOWNLOAD_URL}: ${ORT_STATUS_MSG}\nLog: ${ORT_DOWNLOAD_LOG}")
        endif()
    endif()

    message(STATUS "[LinguaAlpaca] Extracting ONNX Runtime archive...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf "${ORT_ARCHIVE_FILE}"
        WORKING_DIRECTORY "${ORT_DOWNLOAD_DIR}"
        RESULT_VARIABLE ORT_TAR_RES
    )

    if(NOT ORT_TAR_RES EQUAL 0)
        message(FATAL_ERROR "[LinguaAlpaca] Failed to extract ${ORT_ARCHIVE_FILE}")
    endif()

    # 将解压后的 include 与 lib 归整放置到 third_party/onnxruntime
    file(MAKE_DIRECTORY "${ORT_INCLUDE_DIR}")
    file(MAKE_DIRECTORY "${ORT_LIB_DIR}")

    set(EXTRACTED_ROOT "${ORT_DOWNLOAD_DIR}/${ORT_ASSET}")

    file(GLOB ORT_EXTRACTED_HEADERS "${EXTRACTED_ROOT}/include/*")
    foreach(HEADER_FILE ${ORT_EXTRACTED_HEADERS})
        file(COPY "${HEADER_FILE}" DESTINATION "${ORT_INCLUDE_DIR}")
    endforeach()

    file(GLOB ORT_EXTRACTED_LIBS "${EXTRACTED_ROOT}/lib/*")
    foreach(LIB_FILE ${ORT_EXTRACTED_LIBS})
        file(COPY "${LIB_FILE}" DESTINATION "${ORT_LIB_DIR}")
    endforeach()

    # 记录当前已安装的版本号
    file(WRITE "${ORT_ROOT}/version.txt" "${ORT_VERSION}")

    # 清理下载临时目录
    file(REMOVE_RECURSE "${ORT_DOWNLOAD_DIR}")
    message(STATUS "[LinguaAlpaca] ONNX Runtime ${ORT_VERSION} installed successfully to ${ORT_ROOT}")
else()
    if(NOT EXISTS "${ORT_ROOT}/version.txt")
        file(WRITE "${ORT_ROOT}/version.txt" "${ORT_VERSION}")
    endif()
    message(STATUS "[LinguaAlpaca] ONNX Runtime v${ORT_VERSION} prebuilt binaries found in ${ORT_ROOT}")
endif()

# ==============================================================================
# 4. 注册全局 CMake IMPORTED 目标
# ==============================================================================
add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE_DIR}"
)

if(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${ORT_IMPLIB}"
        IMPORTED_LOCATION "${ORT_BIN}"
    )
elseif(APPLE)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_BIN}"
    )
else()
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_BIN}"
    )
endif()
