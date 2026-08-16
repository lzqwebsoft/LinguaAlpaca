#pragma execution_character_set("utf-8")
#include "LlamaServer.hpp"
#include "Logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "common.h"
#include "llama.h"
#include "log.h"
#include "http.h"
#include "cli-server.h"

namespace LinguaAlpaca {

static void InitLlamaLogger() {
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        llama_log_set(
            [](ggml_log_level level, const char* text, void* /*user_data*/) {
                if (!text) return;
                std::string s(text);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
                    s.pop_back();
                }
                if (s.empty()) return;

                LogLevel lvl = LogLevel::Info;
                if (level == GGML_LOG_LEVEL_ERROR) {
                    lvl = LogLevel::Error;
                } else if (level == GGML_LOG_LEVEL_WARN) {
                    lvl = LogLevel::Warning;
                } else if (level == GGML_LOG_LEVEL_DEBUG) {
                    lvl = LogLevel::Debug;
                }

                Logger::GetInstance().Log(lvl, "llama.cpp", s);
            },
            nullptr);
    });
}

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

bool LlamaServer::Start(const ServerConfig& config) {
    if (m_isAlive.load(std::memory_order_acquire)) {
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

    InitLlamaLogger();

    m_baseUrl = "http://" + config.host + ":" + std::to_string(m_port);
    m_isStopping.store(false, std::memory_order_release);
    m_isAlive.store(true, std::memory_order_release);

    common_params server_params;
    server_params.hostname = config.host;
    server_params.port = m_port;
    server_params.n_gpu_layers = config.ngl;
    if (!config.modelPath.empty()) {
        server_params.model.path = config.modelPath;
    }
    if (!config.mmprojPath.empty()) {
        server_params.mmproj.path = config.mmprojPath;
    }
    server_params.use_jinja = true;
    server_params.ui = false; // 禁用 UI

    m_thread = std::thread([this, server_params]() mutable {
        LOG_INFO("LlamaServer", "Starting server thread at " + m_baseUrl +
                 " with model=" + server_params.model.path +
                 ", mmproj=" + server_params.mmproj.path);

        try {
            int res = llama_server(server_params, 0, nullptr);
            if (res != 0) {
                LOG_ERROR("LlamaServer", "llama_server exited with code " + std::to_string(res));
            }
        } catch (const std::exception& e) {
            LOG_ERROR("LlamaServer", std::string("EXCEPTION: ") + e.what());
        } catch (...) {
            LOG_ERROR("LlamaServer", "EXCEPTION: unknown exception.");
        }
        m_isAlive.store(false, std::memory_order_release);
    });

    return true;
}

void LlamaServer::Stop() {
    if (m_isStopping.exchange(true)) {
        return;
    }

    if (m_isAlive.load(std::memory_order_acquire)) {
        LOG_INFO("LlamaServer", "Terminating server thread...");
        llama_server_terminate();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_isAlive.store(false, std::memory_order_release);
}

bool LlamaServer::IsAlive() const {
    return m_isAlive.load(std::memory_order_acquire);
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
        if (!IsAlive()) {
            return false;
        }

        ServerStatusInfo info;
        if (QueryHealth(info) && info.state == ServerHealthState::Ready) {
            LOG_INFO("LlamaServer", "Server is ready at " + m_baseUrl);
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return false;
}

bool LlamaServer::EnsureModelRunning(
    const ServerConfig& config,
    const std::function<void(const std::string& status)>& onStatus) {
    
    bool sameConfig = false;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        sameConfig = (m_config.modelPath == config.modelPath && m_config.mmprojPath == config.mmprojPath);
    }

    if (IsAlive() && sameConfig) {
        ServerStatusInfo info;
        if (QueryHealth(info) && info.state == ServerHealthState::Ready) {
            if (onStatus) onStatus("就绪");
            return true;
        }
    }

    if (onStatus) {
        onStatus("模型加载中...");
    }

    Stop();

    if (!Start(config)) {
        if (onStatus) onStatus("服务启动失败");
        return false;
    }

    bool ready = WaitUntilReady(45, nullptr);
    if (ready) {
        if (onStatus) onStatus("就绪");
    } else {
        if (onStatus) onStatus("模型加载超时或失败");
    }
    return ready;
}

std::string LlamaServer::GetBaseUrl() const {
    return m_baseUrl;
}

} // namespace LinguaAlpaca
