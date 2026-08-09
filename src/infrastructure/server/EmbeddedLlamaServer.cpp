#include "EmbeddedLlamaServer.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>

#include "common.h"
#include "http.h"
#include "cli-server.h"

namespace LinguaAlpaca::Infrastructure::Server {

static std::string GetFilenameBasename(const std::string& path) {
    if (path.empty()) return "";
    try {
        std::filesystem::path p(path);
        return p.filename().string();
    } catch (...) {
        return path;
    }
}

std::string GenerateModelsPresetFile(const Domain::Model::AppConfig& config, const std::string& outputPath) {
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "[GenerateModelsPresetFile] Error: Failed to create " << outputPath << std::endl;
        return "";
    }

    out << "; Auto-generated model presets configuration for LinguaAlpaca Router Server\n";
    out << "version = 1\n\n";
    out << "[*]\n";
    out << "n-gpu-layers = 99\n\n";

    // 1. 翻译模型配置
    std::string transName = config.translationModelName;
    if (transName.empty() && !config.modelPath.empty()) {
        transName = GetFilenameBasename(config.modelPath);
    }
    if (!config.modelPath.empty() && !transName.empty()) {
        out << "[" << transName << "]\n";
        out << "model = " << config.modelPath << "\n";
        out << "alias = " << transName << "\n\n";
    }

    // 2. OCR 视觉模型配置
    std::string ocrName = config.ocrModelName;
    if (ocrName.empty() && !config.ocrModelPath.empty()) {
        ocrName = GetFilenameBasename(config.ocrModelPath);
    }
    if (!config.ocrModelPath.empty() && !ocrName.empty()) {
        out << "[" << ocrName << "]\n";
        out << "model = " << config.ocrModelPath << "\n";
        if (!config.ocrMmprojPath.empty()) {
            out << "mmproj = " << config.ocrMmprojPath << "\n";
        }
        out << "alias = " << ocrName << "\n\n";
    }

    out.close();
    std::cout << "[GenerateModelsPresetFile] Generated preset INI file at: " << outputPath << std::endl;
    return outputPath;
}

static std::string join_path(const common_http_url & parts, const std::string & path) {
    if (parts.path.empty() || parts.path == "/") {
        return path;
    }
    std::string prefix = parts.path;
    if (prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix + path;
}

EmbeddedLlamaServer::EmbeddedLlamaServer() = default;

EmbeddedLlamaServer::~EmbeddedLlamaServer() {
    Stop();
}

bool EmbeddedLlamaServer::Start(const ServerConfig& config) {
    if (m_isAlive.load(std::memory_order_acquire)) {
        return true; // Already running
    }

    m_config = config;

    // Pick a free port if port == 0
    if (m_config.port <= 0) {
        m_port = common_http_get_free_port();
        if (m_port <= 0) {
            std::cerr << "[EmbeddedLlamaServer] Error: Failed to find an available HTTP port." << std::endl;
            return false;
        }
    } else {
        m_port = m_config.port;
    }

    m_baseUrl = "http://" + m_config.host + ":" + std::to_string(m_port);
    m_isStopping.store(false, std::memory_order_release);
    m_isAlive.store(true, std::memory_order_release);

    common_params server_params;
    server_params.hostname = m_config.host;
    server_params.port = m_port;
    if (!m_config.modelPath.empty()) {
        server_params.model.path = m_config.modelPath;
    }
    if (!m_config.mmprojPath.empty()) {
        server_params.mmproj.path = m_config.mmprojPath;
    }
    server_params.n_gpu_layers = m_config.ngl;
    server_params.models_dir = m_config.modelsDir;
    if (!m_config.modelsPreset.empty()) {
        server_params.models_preset = m_config.modelsPreset;
    }
    server_params.models_max = m_config.maxLoadedModels;
    server_params.models_autoload = true;
    server_params.public_path = ""; // 禁用静态 Web UI 挂载，仅作为纯 Headless API 服务运行

    m_thread = std::thread([this, server_params]() mutable {
        std::cout << "[EmbeddedLlamaServer] Starting server thread at " << m_baseUrl
                  << " with model.path=" << server_params.model.path
                  << ", mmproj=" << server_params.mmproj.path << std::endl;

        int res = llama_server(server_params, 0, nullptr);
        if (res != 0) {
            std::cerr << "[EmbeddedLlamaServer] llama_server exited with status code " << res << std::endl;
        }
        m_isAlive.store(false, std::memory_order_release);
    });

    return true;
}

bool EmbeddedLlamaServer::SwitchModel(const std::string& modelPath, const std::string& mmprojPath) {
    if (modelPath.empty()) return false;

    if (m_config.modelPath == modelPath && m_config.mmprojPath == mmprojPath && IsAlive()) {
        return true; // Model is already running
    }

    std::cout << "[EmbeddedLlamaServer] Switching model to: " << modelPath << " (mmproj: " << mmprojPath << ")" << std::endl;

    Stop();

    ServerConfig newConfig = m_config;
    newConfig.modelPath = modelPath;
    newConfig.mmprojPath = mmprojPath;
    newConfig.port = 0; // Request new free port

    if (!Start(newConfig)) {
        return false;
    }

    return WaitUntilReady(60);
}

void EmbeddedLlamaServer::Stop() {
    if (m_isStopping.exchange(true)) {
        return;
    }

    if (m_isAlive.load(std::memory_order_acquire)) {
        std::cout << "[EmbeddedLlamaServer] Terminating server thread..." << std::endl;
        llama_server_terminate();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_isAlive.store(false, std::memory_order_release);
}

bool EmbeddedLlamaServer::IsAlive() const {
    return m_isAlive.load(std::memory_order_acquire);
}

bool EmbeddedLlamaServer::WaitUntilReady(int timeoutSec, const std::function<bool()>& shouldAbort) {
    if (!IsAlive()) return false;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    while (std::chrono::steady_clock::now() < deadline) {
        if (shouldAbort && shouldAbort()) {
            return false;
        }
        if (!IsAlive()) {
            return false;
        }

        try {
            auto [cli, parts] = common_http_client(m_baseUrl);
            cli.set_connection_timeout(1, 0);
            auto res = cli.Get(join_path(parts, "/health"));
            if (res && res->status == 200) {
                std::cout << "[EmbeddedLlamaServer] Server is ready at " << m_baseUrl << std::endl;
                return true;
            }
        } catch (...) {
            // Ignore connection errors during warmup
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return false;
}

std::string EmbeddedLlamaServer::GetBaseUrl() const {
    return m_baseUrl;
}

} // namespace LinguaAlpaca::Infrastructure::Server
