#include "SseLlamaEngine.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <base64.hpp>
#include <nlohmann/json.hpp>

#include "../../domain/model/Language.hpp"
#include "cli-client.h"
#include <http.h>

using json = nlohmann::json;

namespace LinguaAlpaca::Infrastructure::Engine {

static std::string join_path(const common_http_url & parts, const std::string & path) {
    std::string p = parts.path;
    if (!p.empty() && p.back() == '/') {
        p.pop_back();
    }
    if (path.empty() || path.front() != '/') {
        p += '/';
    }
    p += path;
    return p;
}

static std::string GetOcrPromptPrefix(const std::string& taskType) {
    if (taskType == "table")    return "Table Recognition:";
    if (taskType == "formula")  return "Formula Recognition:";
    if (taskType == "chart")    return "Chart Recognition:";
    if (taskType == "spotting") return "Spotting:";
    if (taskType == "seal")     return "Seal Recognition:";
    return "OCR:";
}

static std::string FormatImageUrl(const std::string& imagePath) {
    if (imagePath.empty()) return "";

    // If already data URI or remote HTTP URL, return directly
    if (imagePath.rfind("data:image/", 0) == 0 ||
        imagePath.rfind("http://", 0) == 0 ||
        imagePath.rfind("https://", 0) == 0) {
        return imagePath;
    }

    std::ifstream file(imagePath, std::ios::binary);
    if (!file) {
        std::cerr << "[SseLlamaEngine] Warning: Could not open local image file: " << imagePath << std::endl;
        return imagePath;
    }

    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.empty()) {
        return imagePath;
    }

    std::string encoded = base64::encode(data);

    std::string mimeType = "image/jpeg";
    std::string lowerPath = imagePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    if (lowerPath.rfind(".png") != std::string::npos) {
        mimeType = "image/png";
    } else if (lowerPath.rfind(".webp") != std::string::npos) {
        mimeType = "image/webp";
    } else if (lowerPath.rfind(".bmp") != std::string::npos) {
        mimeType = "image/bmp";
    } else if (lowerPath.rfind(".gif") != std::string::npos) {
        mimeType = "image/gif";
    }

    return "data:" + mimeType + ";base64," + encoded;
}

SseLlamaEngine::SseLlamaEngine(std::shared_ptr<Server::EmbeddedLlamaServer> server)
    : m_server(std::move(server)) {
    if (m_server) {
        m_baseUrl = m_server->GetBaseUrl();
    }
}

SseLlamaEngine::SseLlamaEngine(std::string baseUrl)
    : m_baseUrl(std::move(baseUrl)) {}

SseLlamaEngine::~SseLlamaEngine() {
    CancelCurrentTask();
}

void SseLlamaEngine::SetServer(std::shared_ptr<Server::EmbeddedLlamaServer> server) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_server = std::move(server);
    if (m_server) {
        m_baseUrl = m_server->GetBaseUrl();
    }
}

void SseLlamaEngine::SetBaseUrl(const std::string& baseUrl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_baseUrl = baseUrl;
}

std::string SseLlamaEngine::GetBaseUrl() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_server) {
        return m_server->GetBaseUrl();
    }
    return m_baseUrl;
}

void SseLlamaEngine::SetTranslationModelName(const std::string& modelName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_translationModelName = modelName;
}

std::string SseLlamaEngine::GetTranslationModelName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_translationModelName;
}

void SseLlamaEngine::SetOcrModelName(const std::string& modelName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ocrModelName = modelName;
}

std::string SseLlamaEngine::GetOcrModelName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ocrModelName;
}

void SseLlamaEngine::SetOcrMmprojPath(const std::string& mmprojPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ocrMmprojPath = mmprojPath;
}

std::string SseLlamaEngine::GetOcrMmprojPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ocrMmprojPath;
}

bool SseLlamaEngine::CheckHealth() const {
    std::string baseUrl = GetBaseUrl();
    if (baseUrl.empty()) {
        return false;
    }

    try {
        auto [cli, parts] = common_http_client(baseUrl);
        cli.set_connection_timeout(1, 0);
        cli.set_read_timeout(2, 0);
        auto res = cli.Get(join_path(parts, "/health"));
        return (res && res->status == 200);
    } catch (...) {
        return false;
    }
}

bool SseLlamaEngine::WaitReady(std::function<bool()> shouldStop, int timeoutSec) {
    if (m_server && !m_server->IsAlive()) {
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    while (shouldStop ? !shouldStop() : true) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        if (CheckHealth()) {
            return true;
        }

        if (m_server && !m_server->IsAlive()) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return false;
}

bool SseLlamaEngine::IsModelLoaded() const {
    std::string baseUrl = GetBaseUrl();
    if (baseUrl.empty()) {
        return false;
    }

    if (m_server && !m_server->IsAlive()) {
        return false;
    }

    try {
        auto [cli, parts] = common_http_client(baseUrl);
        cli.set_connection_timeout(1, 0);
        cli.set_read_timeout(2, 0);

        // 校验 GET /health
        auto healthRes = cli.Get(join_path(parts, "/health"));
        if (healthRes && healthRes->status == 200) {
            return true;
        }
    } catch (...) {
        return !baseUrl.empty();
    }

    return !baseUrl.empty();
}

std::string SseLlamaEngine::FormatHyMt2UserContent(
    const std::string& srcText,
    Domain::Model::LanguageCode srcLang,
    Domain::Model::LanguageCode tgtLang) {

    std::string srcLangName = Domain::Model::LanguageHelper::GetDisplayName(srcLang);
    std::string tgtLangName = Domain::Model::LanguageHelper::GetDisplayName(tgtLang);

    // Rule 2 Prompt alignment for Hy-MT2 Chinese directive
    return "将以下" + srcLangName + "文本翻译为" + tgtLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
}

void SseLlamaEngine::TranslateStreamAsync(
    const Domain::Model::TranslationTask& task,
    Domain::Repository::StreamTokenCallback onToken,
    Domain::Repository::StreamCompleteCallback onComplete) {

    m_shouldStop.store(false, std::memory_order_release);
    m_isRunning.store(true, std::memory_order_release);

    std::string baseUrl;
    std::string modelName;
    std::shared_ptr<Server::EmbeddedLlamaServer> server;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        baseUrl = m_baseUrl;
        modelName = m_translationModelName;
        server = m_server;
    }

    std::thread([this, task, server, baseUrl, modelName, onToken, onComplete]() mutable {
        std::string fullText;
        bool success = false;
        std::string errorMsg;

        try {
            if (server) {
                if (!modelName.empty()) {
                    Server::ServerConfig config;
                    config.modelPath = modelName;
                    if (!server->EnsureModelRunning(config)) {
                        throw std::runtime_error("无法载入翻译模型或服务响应超时。");
                    }
                }
                baseUrl = server->GetBaseUrl();
            }

            if (baseUrl.empty()) {
                throw std::runtime_error("Llama-server 未连接或未能初始化。");
            }

            cli_client client;
            client.server_base = baseUrl;
            client.model = modelName;

            std::string userContent = FormatHyMt2UserContent(task.GetSourceText(), task.GetSourceLanguage(), task.GetTargetLanguage());
            json messages = json::array({
                {
                    {"role", "user"},
                    {"content", userContent}
                }
            });

            json body = {
                {"messages", messages},
                {"stream", true},
                {"temperature", 0.7},
                {"top_p", 0.6},
                {"top_k", 20},
                {"repetition_penalty", 1.05},
                {"repeat_penalty", 1.05},
                {"max_tokens", 4096}
            };
            if (!modelName.empty()) {
                body["model"] = modelName;
            }

            auto shouldStopFunc = [this]() -> bool {
                return m_shouldStop.load(std::memory_order_acquire);
            };

            auto onDataFunc = [&](const std::string& payload) {
                if (payload == "[DONE]") return;
                json chunk = json::parse(payload, nullptr, false);
                if (chunk.is_discarded()) return;

                if (chunk.contains("error")) {
                    errorMsg = chunk["error"].dump();
                    return;
                }

                std::string deltaToken;
                if (chunk.contains("choices") && !chunk["choices"].empty()) {
                    const auto& choice = chunk["choices"][0];
                    if (choice.contains("delta")) {
                        const auto& delta = choice["delta"];
                        if (delta.contains("content") && delta["content"].is_string()) {
                            deltaToken = delta["content"].get<std::string>();
                        }
                    } else if (choice.contains("text") && choice["text"].is_string()) {
                        deltaToken = choice["text"].get<std::string>();
                    }
                }

                if (!deltaToken.empty()) {
                    fullText += deltaToken;
                    if (onToken) {
                        onToken(deltaToken);
                    }
                }
            };

            std::string errStr = client.post_sse("/v1/chat/completions", body.dump(), shouldStopFunc, onDataFunc);
            if (!errStr.empty() && errorMsg.empty()) {
                errorMsg = errStr;
            } else {
                success = true;
            }
        } catch (const std::exception& e) {
            errorMsg = e.what();
        }

        m_isRunning.store(false, std::memory_order_release);
        if (onComplete) {
            onComplete(success && errorMsg.empty(), fullText, errorMsg);
        }
    }).detach();
}

void SseLlamaEngine::CancelCurrentTask() {
    m_shouldStop.store(true, std::memory_order_release);
}

// IOcrEngine implementation
std::string SseLlamaEngine::GetModelPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ocrModelName;
}

std::string SseLlamaEngine::GetMmprojPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ocrMmprojPath;
}

void SseLlamaEngine::RecognizeStream(
    const std::string& imagePath,
    const std::string& taskType,
    const std::string& modelPath,
    const std::string& mmprojPath,
    Domain::Repository::OcrTokenCallback onToken,
    Domain::Repository::OcrCompleteCallback onComplete) {

    m_shouldStop.store(false, std::memory_order_release);
    m_isRunning.store(true, std::memory_order_release);

    std::string baseUrl;
    std::string modelName;
    std::string targetMmproj;
    std::shared_ptr<Server::EmbeddedLlamaServer> server;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        baseUrl = m_baseUrl;
        modelName = !modelPath.empty() ? modelPath : m_ocrModelName;
        targetMmproj = !mmprojPath.empty() ? mmprojPath : m_ocrMmprojPath;
        server = m_server;
    }

    std::thread([this, imagePath, taskType, server, baseUrl, modelName, targetMmproj, onToken, onComplete]() mutable {
        std::string fullText;
        bool success = false;
        std::string errorMsg;

        try {
            if (server) {
                if (!modelName.empty()) {
                    Server::ServerConfig config;
                    config.modelPath = modelName;
                    config.mmprojPath = targetMmproj;
                    if (!server->EnsureModelRunning(config)) {
                        throw std::runtime_error("无法载入 OCR 视觉模型或服务响应超时。");
                    }
                }
                baseUrl = server->GetBaseUrl();
            }

            if (baseUrl.empty()) {
                throw std::runtime_error("Llama-server 未连接或未能初始化。");
            }

            cli_client client;
            client.server_base = baseUrl;
            client.model = modelName;

            json contentArray = json::array();
            contentArray.push_back({
                {"type", "text"},
                {"text", GetOcrPromptPrefix(taskType)}
            });
            contentArray.push_back({
                {"type", "image_url"},
                {"image_url", {{"url", FormatImageUrl(imagePath)}}}
            });

            json messages = json::array();
            messages.push_back({
                {"role", "user"},
                {"content", contentArray}
            });

            json body = {
                {"messages", messages},
                {"stream", true},
                {"temperature", 0.0}
            };
            if (!modelName.empty()) {
                body["model"] = modelName;
            }

            auto shouldStopFunc = [this]() -> bool {
                return m_shouldStop.load(std::memory_order_acquire);
            };

            auto onDataFunc = [&](const std::string& payload) {
                if (payload == "[DONE]") return;
                json chunk = json::parse(payload, nullptr, false);
                if (chunk.is_discarded()) return;

                if (chunk.contains("error")) {
                    errorMsg = chunk["error"].dump();
                    return;
                }

                std::string deltaToken;
                if (chunk.contains("choices") && !chunk["choices"].empty()) {
                    const auto& choice = chunk["choices"][0];
                    if (choice.contains("delta")) {
                        const auto& delta = choice["delta"];
                        if (delta.contains("content") && delta["content"].is_string()) {
                            deltaToken = delta["content"].get<std::string>();
                        }
                    }
                }

                if (!deltaToken.empty()) {
                    fullText += deltaToken;
                    if (onToken) {
                        onToken(deltaToken);
                    }
                }
            };

            std::string errStr = client.post_sse("/v1/chat/completions", body.dump(), shouldStopFunc, onDataFunc);
            if (!errStr.empty() && errorMsg.empty()) {
                errorMsg = errStr;
            } else {
                success = true;
            }
        } catch (const std::exception& e) {
            errorMsg = e.what();
        }

        m_isRunning.store(false, std::memory_order_release);
        if (onComplete) {
            onComplete(fullText, success && errorMsg.empty(), errorMsg);
        }
    }).detach();
}

void SseLlamaEngine::Cancel() {
    CancelCurrentTask();
}

bool SseLlamaEngine::IsRunning() const {
    return m_isRunning.load(std::memory_order_acquire);
}

} // namespace LinguaAlpaca::Infrastructure::Engine
