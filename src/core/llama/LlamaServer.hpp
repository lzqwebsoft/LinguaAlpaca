#pragma once
#pragma execution_character_set("utf-8")

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Types.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LinguaAlpaca {

struct ServerConfig {
    std::string host = "127.0.0.1";
    int port = 0; // 0 表示自动查找可用端口
    std::string modelPath;
    std::string mmprojPath;
    int ngl = 99;
};

class LlamaServer {
public:
    LlamaServer();
    ~LlamaServer();

    LlamaServer(const LlamaServer&) = delete;
    LlamaServer& operator=(const LlamaServer&) = delete;

    bool Start(const ServerConfig& config);
    void Stop();

    bool IsAlive() const;
    bool QueryHealth(ServerStatusInfo& outInfo) const;

    bool WaitUntilReady(int timeoutSec = 45, const std::function<bool()>& shouldAbort = nullptr);

    bool EnsureModelRunning(
        const ServerConfig& config,
        const std::function<void(const std::string& status)>& onStatus = nullptr,
        const std::function<bool()>& shouldAbort = nullptr
    );

    std::string GetBaseUrl() const;
    std::string GetCurrentModelPath() const;
    std::string GetCurrentMmprojPath() const;

    static std::string FindLlamaServerBinary();

private:
    void CleanupProcess();
    void StartLogReader(HANDLE hReadPipe);

    std::atomic<bool> m_isAlive{false};
    std::atomic<bool> m_isStopping{false};

    mutable std::mutex m_configMutex;
    ServerConfig m_config;
    int m_port{0};
    std::string m_baseUrl;

#ifdef _WIN32
    HANDLE m_hProcess{NULL};
    HANDLE m_hJob{NULL};
    DWORD m_processId{0};
#endif
    std::thread m_logThread;
};

} // namespace LinguaAlpaca
