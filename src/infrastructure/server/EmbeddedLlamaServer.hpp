#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "../../domain/model/AppConfig.hpp"

namespace LinguaAlpaca::Infrastructure::Server {

struct ServerConfig {
    std::string modelPath = "";
    std::string mmprojPath = "";
    std::string modelsDir = "./models";
    std::string modelsPreset = "";
    int maxLoadedModels = 1;
    int port = 0; // 0 = auto-allocated free port
    std::string host = "127.0.0.1";
    int ngl = 99;
};

// Helper function to generate an INI preset file from AppConfig
std::string GenerateModelsPresetFile(const Domain::Model::AppConfig& config, const std::string& outputPath = "models_preset.ini");

class EmbeddedLlamaServer {
public:
    EmbeddedLlamaServer();
    ~EmbeddedLlamaServer();

    // Disable copy
    EmbeddedLlamaServer(const EmbeddedLlamaServer&) = delete;
    EmbeddedLlamaServer& operator=(const EmbeddedLlamaServer&) = delete;

    // Start background thread running llama_server API
    bool Start(const ServerConfig& config = ServerConfig());

    // Stop background server thread
    void Stop();

    // Is thread active
    bool IsAlive() const;

    // Poll /health until server responds HTTP 200 or times out
    bool WaitUntilReady(int timeoutSec = 30, const std::function<bool()>& shouldAbort = nullptr);

    // Address e.g. "http://127.0.0.1:8080"
    std::string GetBaseUrl() const;

    int GetPort() const { return m_port; }
    std::string GetCurrentModelPath() const { return m_config.modelPath; }
    std::string GetCurrentMmprojPath() const { return m_config.mmprojPath; }

    // Ensure the server is running the specified model; stops and restarts if model config differs
    bool EnsureModelRunning(const ServerConfig& config, const std::function<void(const std::string& status)>& onStatus = nullptr);

private:
    std::thread m_thread;
    int m_port = -1;
    std::atomic<bool> m_isAlive{false};
    std::atomic<bool> m_isStopping{false};
    std::string m_baseUrl;
    ServerConfig m_config;
};

} // namespace LinguaAlpaca::Infrastructure::Server
