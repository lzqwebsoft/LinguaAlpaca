#pragma once
#pragma execution_character_set("utf-8")

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Types.hpp"

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
        const std::function<void(const std::string& status)>& onStatus = nullptr
    );

    std::string GetBaseUrl() const;
    std::string GetCurrentModelPath() const;
    std::string GetCurrentMmprojPath() const;

private:
    std::atomic<bool> m_isAlive{false};
    std::atomic<bool> m_isStopping{false};
    std::thread m_thread;

    mutable std::mutex m_configMutex;
    ServerConfig m_config;
    int m_port{0};
    std::string m_baseUrl;
};

} // namespace LinguaAlpaca
