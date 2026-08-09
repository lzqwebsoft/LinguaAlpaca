#include "SseLlamaEngine.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "../../domain/model/Language.hpp"
#include "cli-client.h"

using json = nlohmann::json;

namespace LinguaAlpaca::Infrastructure::Engine {

static std::string Base64Encode(const std::vector<unsigned char>& data) {
    static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    int val = 0;
    int valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(lookup[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

static std::string FormatImageUrl(const std::string& imagePath) {
    if (imagePath.empty()) return "";

    // If already data URI or remote HTTP URL, return directly
    if (imagePath.rfind("data:image/", 0) == 0 ||
        imagePath.rfind("http://", 0) == 0 ||
        imagePath.rfind("https://", 0) == 0) {
        return imagePath;
    }

    // Read binary file from local disk
    std::ifstream file(imagePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[SseLlamaEngine] Warning: Could not open local image file: " << imagePath << std::endl;
        return imagePath; // Fallback
    }

    std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) {
        return imagePath;
    }

    std::string base64Data = Base64Encode(buffer);

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

    return "data:" + mimeType + ";base64," + base64Data;
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

bool SseLlamaEngine::LoadModel(const std::string& modelPath) {
    SetTranslationModelName(modelPath);
    return true;
}

bool SseLlamaEngine::IsModelLoaded() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_server) {
        return m_server->IsAlive();
    }
    return !m_baseUrl.empty();
}

std::string SseLlamaEngine::CleanTextTokens(const std::string& rawText) {
    std::string text = rawText;
    static const std::vector<std::string> controlTags = {
        "<|im_end|>", "<|im_start|>", "<|endoftext|>", "<|im_"
    };
    for (const auto& tag : controlTags) {
        size_t pos = 0;
        while ((pos = text.find(tag, pos)) != std::string::npos) {
            text.erase(pos, tag.length());
        }
    }
    return text;
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

Domain::Model::TranslationTask SseLlamaEngine::Translate(const Domain::Model::TranslationTask& task) {
    Domain::Model::TranslationTask result = task;
    result.SetStatus(Domain::Model::TaskStatus::Processing);

    std::string baseUrl;
    std::string modelName;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        baseUrl = m_baseUrl;
        modelName = m_translationModelName;
    }

    if (baseUrl.empty()) {
        result.SetErrorMessage("Llama-server 服务未联摊/未启动。");
        result.SetStatus(Domain::Model::TaskStatus::Failed);
        return result;
    }

    try {
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
            {"stream", false},
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

        std::string resStr = client.post("/v1/chat/completions", body.dump());
        json resJson = json::parse(resStr);

        std::string text;
        if (resJson.contains("choices") && !resJson["choices"].empty()) {
            auto choice = resJson["choices"][0];
            if (choice.contains("message")) {
                const auto& msg = choice["message"];
                if (msg.contains("content") && msg["content"].is_string()) {
                    text = msg["content"].get<std::string>();
                }
            } else if (choice.contains("text") && choice["text"].is_string()) {
                text = choice["text"].get<std::string>();
            }
        }

        result.SetTranslatedText(CleanTextTokens(text));
        result.SetStatus(Domain::Model::TaskStatus::Completed);
    } catch (const std::exception& e) {
        result.SetErrorMessage(e.what());
        result.SetStatus(Domain::Model::TaskStatus::Failed);
    }

    return result;
}

std::string SseLlamaEngine::QuickTranslate(
    const std::string& text,
    Domain::Model::LanguageCode sourceLang,
    Domain::Model::LanguageCode targetLang) {
    Domain::Model::TranslationTask task(text, sourceLang, targetLang);
    auto res = Translate(task);
    return res.GetTranslatedText();
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
            if (server && !modelName.empty()) {
                if (server->SwitchModel(modelName)) {
                    baseUrl = server->GetBaseUrl();
                } else {
                    throw std::runtime_error("无法切换/启动模型: " + modelName);
                }
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
                    std::string cleanToken = CleanTextTokens(deltaToken);
                    if (!cleanToken.empty()) {
                        fullText += cleanToken;
                        if (onToken) {
                            onToken(cleanToken);
                        }
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
            onComplete(success && errorMsg.empty(), CleanTextTokens(fullText), errorMsg);
        }
    }).detach();
}

void SseLlamaEngine::CancelCurrentTask() {
    m_shouldStop.store(true, std::memory_order_release);
}

// IOcrEngine implementation
bool SseLlamaEngine::LoadModel(const std::string& modelPath, const std::string& mmprojPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ocrModelName = modelPath;
    m_ocrMmprojPath = mmprojPath;
    return true;
}

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
            if (server && !modelName.empty()) {
                if (server->SwitchModel(modelName, targetMmproj)) {
                    baseUrl = server->GetBaseUrl();
                } else {
                    throw std::runtime_error("无法切换/启动 OCR 模型: " + modelName);
                }
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
                {"text", "请提取并识别此图片中的所有文字内容，保持原始排版分行。"}
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
                {"stream", true}
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
                    std::string cleanToken = CleanTextTokens(deltaToken);
                    if (!cleanToken.empty()) {
                        fullText += cleanToken;
                        if (onToken) {
                            onToken(cleanToken);
                        }
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
            onComplete(CleanTextTokens(fullText), success && errorMsg.empty(), errorMsg);
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
