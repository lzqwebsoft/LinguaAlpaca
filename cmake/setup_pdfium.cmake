# cmake/setup_pdfium.cmake
# 自动检测操作系统架构，并通过代理动态探测并下载安装最新 Google PDFium 官方预编译二进制库

set(PDFIUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium")
set(PDFIUM_INCLUDE_DIR "${PDFIUM_ROOT}/include")
set(PDFIUM_LIB_DIR "${PDFIUM_ROOT}/lib")
set(PDFIUM_BIN_DIR "${PDFIUM_ROOT}/bin")

# ==============================================================================
# 1. 代理设置：优先使用环境变量或指定代理
# ==============================================================================
if(NOT DEFINED PDFIUM_PROXY)
    if(DEFINED ENV{https_proxy})
        set(PDFIUM_PROXY "$ENV{https_proxy}")
    elseif(DEFINED ENV{http_proxy})
        set(PDFIUM_PROXY "$ENV{http_proxy}")
    else()
        set(PDFIUM_PROXY "http://127.0.0.1:7088")
    endif()
endif()

if(PDFIUM_PROXY)
    set(ENV{http_proxy} "${PDFIUM_PROXY}")
    set(ENV{https_proxy} "${PDFIUM_PROXY}")
    set(ENV{HTTP_PROXY} "${PDFIUM_PROXY}")
    set(ENV{HTTPS_PROXY} "${PDFIUM_PROXY}")
endif()

# ==============================================================================
# 2. 动态检测 GitHub 官方最新发布版本 (不固定版本)
# ==============================================================================
if(NOT DEFINED PDFIUM_VERSION OR PDFIUM_VERSION STREQUAL "" OR PDFIUM_VERSION STREQUAL "latest")
    find_program(CURL_EXECUTABLE curl)
    set(DETECTED_PDFIUM_VERSION "")

    if(CURL_EXECUTABLE)
        set(CURL_HEADER_ARGS -s -I "https://github.com/bblanchon/pdfium-binaries/releases/latest")
        if(PDFIUM_PROXY)
            list(PREPEND CURL_HEADER_ARGS --proxy "${PDFIUM_PROXY}")
        endif()
        execute_process(
            COMMAND "${CURL_EXECUTABLE}" ${CURL_HEADER_ARGS}
            OUTPUT_VARIABLE LATEST_REDIRECT_HEADER
            ERROR_QUIET
            RESULT_VARIABLE CURL_HDR_RES
        )
        if(CURL_HDR_RES EQUAL 0 AND LATEST_REDIRECT_HEADER MATCHES "[Ll]ocation:[ \t]*https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium/([0-9]+)")
            set(DETECTED_PDFIUM_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()

    if(NOT DETECTED_PDFIUM_VERSION)
        # 备选：通过 GitHub REST API 探测最新 release tag
        set(GITHUB_API_URL "https://api.github.com/repos/bblanchon/pdfium-binaries/releases/latest")
        file(DOWNLOAD "${GITHUB_API_URL}" "${CMAKE_BINARY_DIR}/_pdfium_latest.json"
            STATUS API_STATUS
            LOG API_LOG
            TIMEOUT 10
            USER_AGENT "LinguaAlpaca-CMake"
        )
        list(GET API_STATUS 0 API_CODE)
        if(API_CODE EQUAL 0 AND EXISTS "${CMAKE_BINARY_DIR}/_pdfium_latest.json")
            file(READ "${CMAKE_BINARY_DIR}/_pdfium_latest.json" API_JSON_CONTENT)
            if(API_JSON_CONTENT MATCHES "\"tag_name\":[ \t]*\"chromium/([0-9]+)\"")
                set(DETECTED_PDFIUM_VERSION "${CMAKE_MATCH_1}")
            endif()
            file(REMOVE "${CMAKE_BINARY_DIR}/_pdfium_latest.json")
        endif()
    endif()

    if(DETECTED_PDFIUM_VERSION)
        set(PDFIUM_VERSION "${DETECTED_PDFIUM_VERSION}")
        message(STATUS "[LinguaAlpaca] Detected latest Google PDFium release from GitHub: chromium/${PDFIUM_VERSION}")
    else()
        if(EXISTS "${PDFIUM_ROOT}/version.txt")
            file(READ "${PDFIUM_ROOT}/version.txt" LOCAL_SAVED_VER)
            string(STRIP "${LOCAL_SAVED_VER}" LOCAL_SAVED_VER)
            if(LOCAL_SAVED_VER MATCHES "chromium/([0-9]+)")
                set(LOCAL_SAVED_VER "${CMAKE_MATCH_1}")
            endif()
            set(PDFIUM_VERSION "${LOCAL_SAVED_VER}")
        else()
            set(PDFIUM_VERSION "8100")
        endif()
        message(STATUS "[LinguaAlpaca] Could not query latest version online, using fallback: chromium/${PDFIUM_VERSION}")
    endif()
endif()

# ==============================================================================
# 3. 平台资源资产映射
# ==============================================================================
if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
        set(PDFIUM_ASSET "pdfium-win-arm64")
    else()
        set(PDFIUM_ASSET "pdfium-win-x64")
    endif()
    set(PDFIUM_EXT "tgz")
    set(PDFIUM_IMPLIB "${PDFIUM_LIB_DIR}/pdfium.dll.lib")
    if(EXISTS "${PDFIUM_BIN_DIR}/pdfium.dll")
        set(PDFIUM_BIN "${PDFIUM_BIN_DIR}/pdfium.dll")
    else()
        set(PDFIUM_BIN "${PDFIUM_LIB_DIR}/pdfium.dll")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR "${CMAKE_OSX_ARCHITECTURES}" MATCHES "arm64")
        set(PDFIUM_ASSET "pdfium-mac-arm64")
    else()
        set(PDFIUM_ASSET "pdfium-mac-univ")
    endif()
    set(PDFIUM_EXT "tgz")
    set(PDFIUM_BIN "${PDFIUM_LIB_DIR}/libpdfium.dylib")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(PDFIUM_ASSET "pdfium-linux-arm64")
    else()
        set(PDFIUM_ASSET "pdfium-linux-x64")
    endif()
    set(PDFIUM_EXT "tgz")
    set(PDFIUM_BIN "${PDFIUM_LIB_DIR}/libpdfium.so")
endif()

# 校验本地是否已经存在预编译头文件与二进制库，并对比版本
set(PDFIUM_ALREADY_READY FALSE)
set(INSTALLED_PDFIUM_VERSION "")
if(EXISTS "${PDFIUM_ROOT}/version.txt")
    file(READ "${PDFIUM_ROOT}/version.txt" INSTALLED_PDFIUM_VERSION)
    string(STRIP "${INSTALLED_PDFIUM_VERSION}" INSTALLED_PDFIUM_VERSION)
    if(INSTALLED_PDFIUM_VERSION MATCHES "chromium/([0-9]+)")
        set(INSTALLED_PDFIUM_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

if(EXISTS "${PDFIUM_INCLUDE_DIR}/fpdfview.h")
    if(WIN32 AND EXISTS "${PDFIUM_IMPLIB}" AND EXISTS "${PDFIUM_BIN}")
        set(PDFIUM_ALREADY_READY TRUE)
    elseif(EXISTS "${PDFIUM_BIN}")
        set(PDFIUM_ALREADY_READY TRUE)
    endif()

    # 如果检测到本地已安装版本落后于最新版本，则触发自动升级
    if(PDFIUM_ALREADY_READY AND NOT INSTALLED_PDFIUM_VERSION STREQUAL "" AND NOT INSTALLED_PDFIUM_VERSION STREQUAL PDFIUM_VERSION)
        message(STATUS "[LinguaAlpaca] Installed PDFium (chromium/${INSTALLED_PDFIUM_VERSION}) is older than latest (chromium/${PDFIUM_VERSION}), upgrading...")
        set(PDFIUM_ALREADY_READY FALSE)
    endif()
endif()

if(NOT PDFIUM_ALREADY_READY)
    message(STATUS "[LinguaAlpaca] PDFium prebuilt binaries not found.")
    message(STATUS "[LinguaAlpaca] Downloading PDFium ${PDFIUM_VERSION} (${PDFIUM_ASSET})...")
    if(PDFIUM_PROXY)
        message(STATUS "[LinguaAlpaca] Using proxy: ${PDFIUM_PROXY}")
    endif()

    set(PDFIUM_DOWNLOAD_URL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}/${PDFIUM_ASSET}.${PDFIUM_EXT}")
    set(PDFIUM_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_pdfium_download")
    set(PDFIUM_ARCHIVE_FILE "${PDFIUM_DOWNLOAD_DIR}/${PDFIUM_ASSET}.${PDFIUM_EXT}")

    file(MAKE_DIRECTORY "${PDFIUM_DOWNLOAD_DIR}")

    find_program(CURL_EXECUTABLE curl)
    set(DOWNLOAD_SUCCESS FALSE)

    if(CURL_EXECUTABLE)
        message(STATUS "[LinguaAlpaca] Downloading via curl with proxy...")
        set(CURL_ARGS -L -f -o "${PDFIUM_ARCHIVE_FILE}" "${PDFIUM_DOWNLOAD_URL}")
        if(PDFIUM_PROXY)
            list(PREPEND CURL_ARGS --proxy "${PDFIUM_PROXY}")
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
        file(DOWNLOAD "${PDFIUM_DOWNLOAD_URL}" "${PDFIUM_ARCHIVE_FILE}"
            SHOW_PROGRESS
            STATUS PDFIUM_DOWNLOAD_STATUS
            LOG PDFIUM_DOWNLOAD_LOG
        )
        list(GET PDFIUM_DOWNLOAD_STATUS 0 PDFIUM_STATUS_CODE)
        list(GET PDFIUM_DOWNLOAD_STATUS 1 PDFIUM_STATUS_MSG)
        if(NOT PDFIUM_STATUS_CODE EQUAL 0)
            message(FATAL_ERROR "[LinguaAlpaca] Failed to download PDFium from ${PDFIUM_DOWNLOAD_URL}: ${PDFIUM_STATUS_MSG}\nLog: ${PDFIUM_DOWNLOAD_LOG}")
        endif()
    endif()

    message(STATUS "[LinguaAlpaca] Extracting PDFium archive...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf "${PDFIUM_ARCHIVE_FILE}"
        WORKING_DIRECTORY "${PDFIUM_DOWNLOAD_DIR}"
        RESULT_VARIABLE PDFIUM_TAR_RES
    )

    if(NOT PDFIUM_TAR_RES EQUAL 0)
        message(FATAL_ERROR "[LinguaAlpaca] Failed to extract ${PDFIUM_ARCHIVE_FILE}")
    endif()

    file(MAKE_DIRECTORY "${PDFIUM_INCLUDE_DIR}")
    file(MAKE_DIRECTORY "${PDFIUM_LIB_DIR}")
    if(WIN32)
        file(MAKE_DIRECTORY "${PDFIUM_BIN_DIR}")
        file(GLOB PDFIUM_EXTRACTED_BINS "${PDFIUM_DOWNLOAD_DIR}/bin/*")
        foreach(BIN_FILE ${PDFIUM_EXTRACTED_BINS})
            file(COPY "${BIN_FILE}" DESTINATION "${PDFIUM_BIN_DIR}")
            file(COPY "${BIN_FILE}" DESTINATION "${PDFIUM_LIB_DIR}")
        endforeach()
        if(EXISTS "${PDFIUM_BIN_DIR}/pdfium.dll")
            set(PDFIUM_BIN "${PDFIUM_BIN_DIR}/pdfium.dll")
        endif()
    endif()

    file(GLOB PDFIUM_EXTRACTED_HEADERS "${PDFIUM_DOWNLOAD_DIR}/include/*")
    foreach(HEADER_FILE ${PDFIUM_EXTRACTED_HEADERS})
        file(COPY "${HEADER_FILE}" DESTINATION "${PDFIUM_INCLUDE_DIR}")
    endforeach()

    file(GLOB PDFIUM_EXTRACTED_LIBS "${PDFIUM_DOWNLOAD_DIR}/lib/*")
    foreach(LIB_FILE ${PDFIUM_EXTRACTED_LIBS})
        file(COPY "${LIB_FILE}" DESTINATION "${PDFIUM_LIB_DIR}")
    endforeach()

    if(APPLE AND EXISTS "${PDFIUM_BIN}")
        find_program(INSTALL_NAME_TOOL_BIN install_name_tool)
        if(INSTALL_NAME_TOOL_BIN)
            execute_process(
                COMMAND "${INSTALL_NAME_TOOL_BIN}" -id @rpath/libpdfium.dylib "${PDFIUM_BIN}"
            )
        endif()
    endif()

    file(WRITE "${PDFIUM_ROOT}/version.txt" "chromium/${PDFIUM_VERSION}")
    file(REMOVE_RECURSE "${PDFIUM_DOWNLOAD_DIR}")
    message(STATUS "[LinguaAlpaca] Google PDFium chromium/${PDFIUM_VERSION} installed successfully to ${PDFIUM_ROOT}")
else()
    if(APPLE AND EXISTS "${PDFIUM_BIN}")
        find_program(INSTALL_NAME_TOOL_BIN install_name_tool)
        if(INSTALL_NAME_TOOL_BIN)
            execute_process(
                COMMAND "${INSTALL_NAME_TOOL_BIN}" -id @rpath/libpdfium.dylib "${PDFIUM_BIN}"
            )
        endif()
    endif()
    message(STATUS "[LinguaAlpaca] Google PDFium (chromium/${PDFIUM_VERSION}) prebuilt binaries found in ${PDFIUM_ROOT}")
endif()

# ==============================================================================
# 4. 注册全局 CMake IMPORTED 目标
# ==============================================================================
add_library(pdfium SHARED IMPORTED GLOBAL)
set_target_properties(pdfium PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${PDFIUM_INCLUDE_DIR}"
)

if(WIN32)
    set_target_properties(pdfium PROPERTIES
        IMPORTED_IMPLIB "${PDFIUM_IMPLIB}"
        IMPORTED_LOCATION "${PDFIUM_BIN}"
    )
else()
    set_target_properties(pdfium PROPERTIES
        IMPORTED_LOCATION "${PDFIUM_BIN}"
    )
endif()
