#pragma execution_character_set("utf-8")
#include "LlamaServer.hpp"
#include "core/Logger.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#include <wx/stdpaths.h>
#include <wx/filename.h>

#include "http.h"

namespace LinguaAlpaca {

static std::string join_path(const common_http_url& parts, const std::string& path) {
    if (parts.path.empty() || parts.path == "/") {
        return path;
    }
    std::string prefix = parts.path;
    if (prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix + path;
}

std::string LlamaServer::FindLlamaServerBinary() {
    try {
        wxString appPath = wxStandardPaths::Get().GetExecutablePath();
        wxFileName fn(appPath);
        std::filesystem::path appDir = fn.GetPath().ToStdWstring();

        std::vector<std::filesystem::path> candidates = {
            appDir / "llama-server.exe",
            appDir / "llama-server",
            appDir / ".." / "bin" / "Debug" / "llama-server.exe",
            appDir / ".." / "bin" / "Release" / "llama-server.exe",
            appDir / "bin" / "Debug" / "llama-server.exe",
            appDir / "bin" / "Release" / "llama-server.exe"
        };

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec)) {
                return std::filesystem::canonical(candidate, ec).string();
            }
        }
    } catch (...) {
        // Ignore resolution exception and fallback
    }

    return "llama-server.exe";
}

LlamaServer::LlamaServer() = default;

LlamaServer::~LlamaServer() {
    Stop();
}

std::string LlamaServer::GetCurrentModelPath() const {
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_config.modelPath;
}

std::string LlamaServer::GetCurrentMmprojPath() const {
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_config.mmprojPath;
}

void LlamaServer::StartLogReader(HANDLE hReadPipe) {
    m_logThread = std::thread([hReadPipe]() {
        char buffer[2048];
        DWORD bytesRead = 0;
        std::string lineBuffer;

        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            lineBuffer.append(buffer, bytesRead);

            size_t pos;
            while ((pos = lineBuffer.find('\n')) != std::string::npos) {
                std::string line = lineBuffer.substr(0, pos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    LogLevel lvl = LogLevel::Info;
                    if (line.find("error") != std::string::npos || line.find("ERR") != std::string::npos) {
                        lvl = LogLevel::Error;
                    } else if (line.find("warn") != std::string::npos || line.find("WARN") != std::string::npos) {
                        lvl = LogLevel::Warning;
                    }
                    Logger::GetInstance().Log(lvl, "llama.cpp", line);
                }
                lineBuffer.erase(0, pos + 1);
            }
        }

        if (!lineBuffer.empty()) {
            if (lineBuffer.back() == '\r') lineBuffer.pop_back();
            if (!lineBuffer.empty()) {
                Logger::GetInstance().Log(LogLevel::Info, "llama.cpp", lineBuffer);
            }
        }

        CloseHandle(hReadPipe);
    });
}

void LlamaServer::CleanupProcess() {
#ifdef _WIN32
    if (m_hProcess != NULL) {
        TerminateProcess(m_hProcess, 0);
        WaitForSingleObject(m_hProcess, 1500);
        CloseHandle(m_hProcess);
        m_hProcess = NULL;
    }
    if (m_hJob != NULL) {
        CloseHandle(m_hJob);
        m_hJob = NULL;
    }
    m_processId = 0;
#endif

    if (m_logThread.joinable()) {
        if (std::this_thread::get_id() != m_logThread.get_id()) {
            m_logThread.join();
        } else {
            m_logThread.detach();
        }
    }
}

bool LlamaServer::Start(const ServerConfig& config) {
    if (IsAlive()) {
        return true; // Already running
    }

    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config = config;
    }

    // Pick a free port if port <= 0
    if (config.port <= 0) {
        m_port = common_http_get_free_port();
        if (m_port <= 0) {
            LOG_ERROR("LlamaServer", "Failed to find an available HTTP port.");
            return false;
        }
    } else {
        m_port = config.port;
    }

    std::string serverExe = FindLlamaServerBinary();
    if (!std::filesystem::exists(serverExe)) {
        LOG_ERROR("LlamaServer", "llama-server executable not found: " + serverExe);
        return false;
    }

    m_baseUrl = "http://" + config.host + ":" + std::to_string(m_port);
    m_isStopping.store(false, std::memory_order_release);

    std::ostringstream cmd;
    cmd << "\"" << serverExe << "\""
        << " --host " << config.host
        << " --port " << m_port
        << " -m \"" << config.modelPath << "\""
        << " -ngl " << config.ngl
        << " --no-webui --jinja";

    if (!config.mmprojPath.empty()) {
        cmd << " --mmproj \"" << config.mmprojPath << "\"";
    }

    std::string cmdStr = cmd.str();
    LOG_INFO("LlamaServer", "Launching child process: " + cmdStr);

#ifdef _WIN32
    // Setup anonymous pipe for stdout/stderr redirection
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdOutRead = NULL;
    HANDLE hStdOutWrite = NULL;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        LOG_ERROR("LlamaServer", "CreatePipe failed with error: " + std::to_string(GetLastError()));
        return false;
    }
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmdW = wxString::FromUTF8(cmdStr).ToStdWstring();
    std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
    cmdBuf.push_back(L'\0');

    BOOL success = CreateProcessW(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    // Parent must close its copy of the write handle so EOF works when child exits
    CloseHandle(hStdOutWrite);

    if (!success) {
        DWORD err = GetLastError();
        LOG_ERROR("LlamaServer", "CreateProcessW failed with error: " + std::to_string(err));
        CloseHandle(hStdOutRead);
        return false;
    }

    CloseHandle(pi.hThread);

    // Windows Job Object to guarantee child cleanup when parent closes/crashes
    m_hJob = CreateJobObjectW(NULL, NULL);
    if (m_hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = { 0 };
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(m_hJob, pi.hProcess);
    }

    m_hProcess = pi.hProcess;
    m_processId = pi.dwProcessId;
    m_isAlive.store(true, std::memory_order_release);

    StartLogReader(hStdOutRead);
    return true;
#else
    return false;
#endif
}

void LlamaServer::Stop() {
    if (m_isStopping.exchange(true)) {
        return;
    }

    LOG_INFO("LlamaServer", "Terminating llama-server child process...");
    CleanupProcess();
    m_isAlive.store(false, std::memory_order_release);
    m_isStopping.store(false, std::memory_order_release);
}

bool LlamaServer::IsAlive() const {
    if (!m_isAlive.load(std::memory_order_acquire)) {
        return false;
    }

#ifdef _WIN32
    if (m_hProcess != NULL) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(m_hProcess, &exitCode)) {
            if (exitCode != STILL_ACTIVE) {
                return false;
            }
        }
    }
#endif
    return true;
}

bool LlamaServer::QueryHealth(ServerStatusInfo& outInfo) const {
    if (!IsAlive() || m_baseUrl.empty()) {
        outInfo.state = ServerHealthState::Offline;
        outInfo.message = "服务未启动";
        return false;
    }

    try {
        auto [cli, parts] = common_http_client(m_baseUrl);
        cli.set_connection_timeout(1, 0);
        cli.set_read_timeout(1, 500);

        auto res = cli.Get(join_path(parts, "/health"));
        if (!res) {
            outInfo.state = ServerHealthState::Loading;
            outInfo.message = "服务正在启动...";
            return false;
        }

        if (res->status == 200) {
            outInfo.state = ServerHealthState::Ready;
            outInfo.message = "服务已就绪";
            return true;
        } else if (res->status == 503) {
            outInfo.state = ServerHealthState::Loading;
            outInfo.message = "正在加载模型权重...";
            return false;
        } else {
            outInfo.state = ServerHealthState::Error;
            outInfo.message = "服务返回错误代码: " + std::to_string(res->status);
            return false;
        }
    } catch (const std::exception& e) {
        outInfo.state = ServerHealthState::Loading;
        outInfo.message = std::string("连接探针中: ") + e.what();
        return false;
    } catch (...) {
        outInfo.state = ServerHealthState::Loading;
        outInfo.message = "等待服务响应...";
        return false;
    }
}

bool LlamaServer::WaitUntilReady(int timeoutSec, const std::function<bool()>& shouldAbort) {
    if (!IsAlive()) return false;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    while (std::chrono::steady_clock::now() < deadline) {
        if (shouldAbort && shouldAbort()) {
            return false;
        }
        if (!IsAlive() || m_isStopping.load(std::memory_order_acquire)) {
            LOG_ERROR("LlamaServer", "llama-server process exited unexpectedly or was terminated.");
            return false;
        }

        ServerStatusInfo info;
        if (QueryHealth(info) && info.state == ServerHealthState::Ready) {
            LOG_INFO("LlamaServer", "Server is ready at " + m_baseUrl);
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return false;
}

bool LlamaServer::EnsureModelRunning(
    const ServerConfig& config,
    const std::function<void(const std::string& status)>& onStatus,
    const std::function<bool()>& shouldAbort) {
    
    bool sameConfig = false;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        sameConfig = (m_config.modelPath == config.modelPath &&
                      m_config.mmprojPath == config.mmprojPath &&
                      m_config.ngl == config.ngl);
    }

    if (IsAlive() && sameConfig) {
        ServerStatusInfo info;
        if (QueryHealth(info) && info.state == ServerHealthState::Ready) {
            if (onStatus) onStatus("就绪");
            return true;
        }
    }

    if (shouldAbort && shouldAbort()) {
        return false;
    }

    if (onStatus) {
        onStatus("模型加载中...");
    }

    Stop();

    if (shouldAbort && shouldAbort()) {
        return false;
    }

    if (!Start(config)) {
        if (onStatus) onStatus("服务启动失败");
        return false;
    }

    bool ready = WaitUntilReady(45, shouldAbort);
    if (ready) {
        if (onStatus) onStatus("就绪");
    } else {
        if (onStatus && (!shouldAbort || !shouldAbort())) {
            onStatus("模型加载超时或失败");
        }
    }
    return ready;
}

std::string LlamaServer::GetBaseUrl() const {
    return m_baseUrl;
}

} // namespace LinguaAlpaca
