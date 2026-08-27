#pragma execution_character_set("utf-8")
#include "LlamaClient.hpp"

#include <algorithm>
#include <base64.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <vector>

#include <http.h>

using json = nlohmann::json;

namespace LinguaAlpaca {

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

    if (imagePath.rfind("data:image/", 0) == 0 ||
        imagePath.rfind("http://", 0) == 0 ||
        imagePath.rfind("https://", 0) == 0) {
        return imagePath;
    }

    std::ifstream file(imagePath, std::ios::binary);
    if (!file) {
        std::cerr << "[LlamaClient] Warning: Could not open local image file: " << imagePath << std::endl;
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

LlamaClient::LlamaClient(std::shared_ptr<LlamaServer> server)
    : m_server(std::move(server)),
      m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {
    if (m_server) {
        m_baseUrl = m_server->GetBaseUrl();
    }
}

LlamaClient::LlamaClient(std::string baseUrl)
    : m_baseUrl(std::move(baseUrl)),
      m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {}

LlamaClient::~LlamaClient() {
    if (m_aliveToken) {
        m_aliveToken->store(false);
    }
    CancelCurrentTask();
}

void LlamaClient::SetServer(std::shared_ptr<LlamaServer> server) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_server = std::move(server);
    if (m_server) {
        m_baseUrl = m_server->GetBaseUrl();
    }
}

void LlamaClient::SetBaseUrl(const std::string& baseUrl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_baseUrl = baseUrl;
}

std::string LlamaClient::GetBaseUrl() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_server) {
        return m_server->GetBaseUrl();
    }
    return m_baseUrl;
}

bool LlamaClient::IsModelLoaded() const {
    if (m_server) {
        ServerStatusInfo info;
        return m_server->QueryHealth(info) && info.state == ServerHealthState::Ready;
    }
    return !GetBaseUrl().empty();
}

std::string LlamaClient::FormatHyMt2UserContent(
    const std::string& srcText,
    LanguageCode srcLang,
    LanguageCode tgtLang) {
    
    std::string targetLangName = LanguageHelper::GetDisplayName(tgtLang);
    std::string srcLangName = LanguageHelper::GetDisplayName(srcLang);

    if (srcLang == LanguageCode::AutoDetect) {
        return "将以下文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
    }
    return "将以下" + srcLangName + "文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
}

void LlamaClient::TranslateStreamAsync(
    const TranslationTask& task,
    StreamTokenCallback onToken,
    StreamCompleteCallback onComplete) {
    
    CancelCurrentTask();
    m_shouldStop.store(false);
    m_isRunning.store(true);
    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, task, onToken, onComplete]() {
        if (!aliveToken->load()) {
            return;
        }

        std::string currentBaseUrl = GetBaseUrl();
        if (currentBaseUrl.empty()) {
            m_isRunning.store(false);
            if (aliveToken->load() && onComplete) onComplete(false, "", "服务地址为空或未启动");
            return;
        }

        std::string userPrompt = FormatHyMt2UserContent(
            task.GetSourceText(),
            task.GetSourceLanguage(),
            task.GetTargetLanguage()
        );

        json body = {
            {"messages", json::array({
                {
                    {"role", "user"},
                    {"content", userPrompt}
                }
            })},
            {"stream", true},
            {"temperature", 0.7},
            {"top_p", 0.6},
            {"top_k", 20},
            {"repetition_penalty", 1.05},
            {"max_tokens", 4096}
        };

        std::string reqBody = body.dump();
        std::string accumulatedText;
        bool hasError = false;
        std::string errorMsg;

        try {
            auto [cli, parts] = common_http_client(currentBaseUrl);
            cli.set_connection_timeout(5, 0);
            cli.set_read_timeout(120, 0);

            std::string path = parts.path.empty() || parts.path == "/" ? "/v1/chat/completions" : parts.path + "/v1/chat/completions";

            std::string buffer;
            httplib::Headers headers;
            auto res = cli.Post(
                path,
                headers,
                reqBody,
                "application/json",
                [&](const char* data, size_t len) {
                    if (!aliveToken->load() || m_shouldStop.load()) {
                        return false;
                    }

                    buffer.append(data, len);
                    size_t pos;
                    while ((pos = buffer.find("\n\n")) != std::string::npos) {
                        std::string_view eventBlock(buffer.data(), pos);

                        size_t lineStart = 0;
                        while (lineStart < eventBlock.size()) {
                            size_t lineEnd = eventBlock.find('\n', lineStart);
                            if (lineEnd == std::string_view::npos) {
                                lineEnd = eventBlock.size();
                            }
                            std::string_view line = eventBlock.substr(lineStart, lineEnd - lineStart);
                            if (!line.empty() && line.back() == '\r') {
                                line.remove_suffix(1);
                            }
                            lineStart = lineEnd + 1;

                            if (line.rfind("data: ", 0) == 0) {
                                std::string_view jsonStr = line.substr(6);
                                if (jsonStr == "[DONE]") {
                                    break;
                                }
                                try {
                                    auto parsed = json::parse(jsonStr);
                                    if (parsed.contains("choices") && !parsed["choices"].empty()) {
                                        auto& choice = parsed["choices"][0];
                                        if (choice.contains("delta") && choice["delta"].contains("content")) {
                                            std::string token = choice["delta"]["content"].get<std::string>();
                                            accumulatedText += token;
                                            if (aliveToken->load() && onToken) {
                                                onToken(token);
                                            }
                                        }
                                    }
                                } catch (...) {
                                    // 忽略格式不完整的临时 SSE 片段
                                }
                            }
                        }
                        buffer.erase(0, pos + 2);
                    }
                    return true;
                }
            );

            if (!res) {
                hasError = true;
                errorMsg = "HTTP 请求失败: 无法连接至嵌入服务";
            } else if (res->status != 200) {
                hasError = true;
                errorMsg = "HTTP 错误: " + std::to_string(res->status);
            }
        } catch (const std::exception& e) {
            hasError = true;
            errorMsg = std::string("异常: ") + e.what();
        } catch (...) {
            hasError = true;
            errorMsg = "未知推理异常";
        }

        m_isRunning.store(false);

        if (!aliveToken->load()) {
            return;
        }

        if (m_shouldStop.load()) {
            if (onComplete) onComplete(false, accumulatedText, "已手动取消");
        } else if (hasError) {
            if (onComplete) onComplete(false, accumulatedText, errorMsg);
        } else {
            if (onComplete) onComplete(true, accumulatedText, "");
        }
    }).detach();
}

void LlamaClient::RecognizeStream(
    const std::string& imagePath,
    const std::string& taskType,
    const std::string& /*modelPath*/,
    const std::string& /*mmprojPath*/,
    OcrTokenCallback onToken,
    OcrCompleteCallback onComplete) {
    
    CancelCurrentTask();
    m_shouldStop.store(false);
    m_isRunning.store(true);
    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, imagePath, taskType, onToken, onComplete]() {
        if (!aliveToken->load()) {
            return;
        }

        std::string currentBaseUrl = GetBaseUrl();
        if (currentBaseUrl.empty()) {
            m_isRunning.store(false);
            if (aliveToken->load() && onComplete) onComplete("", false, "服务地址为空或未启动");
            return;
        }

        std::string promptPrefix = GetOcrPromptPrefix(taskType);
        std::string imageUrl = FormatImageUrl(imagePath);

        json messageContent = json::array();
        if (!imageUrl.empty()) {
            messageContent.push_back({
                {"type", "image_url"},
                {"image_url", {{"url", imageUrl}}}
            });
        }
        messageContent.push_back({
            {"type", "text"},
            {"text", promptPrefix}
        });

        json body = {
            {"model", "default"},
            {"messages", json::array({
                {
                    {"role", "user"},
                    {"content", messageContent}
                }
            })},
            {"stream", true},
            {"temperature", 0.1},
            {"top_p", 0.9},
            {"max_tokens", 4096},
            {"repetition_penalty", 1.05}
        };

        std::string reqBody = body.dump();
        std::string accumulatedText;
        bool hasError = false;
        std::string errorMsg;

        try {
            auto [cli, parts] = common_http_client(currentBaseUrl);
            cli.set_connection_timeout(5, 0);
            cli.set_read_timeout(180, 0);

            std::string path = parts.path.empty() || parts.path == "/" ? "/v1/chat/completions" : parts.path + "/v1/chat/completions";

            std::string buffer;
            httplib::Headers headers;
            auto res = cli.Post(
                path,
                headers,
                reqBody,
                "application/json",
                [&](const char* data, size_t len) {
                    if (!aliveToken->load() || m_shouldStop.load()) {
                        return false;
                    }

                    buffer.append(data, len);
                    size_t pos;
                    while ((pos = buffer.find("\n\n")) != std::string::npos) {
                        std::string_view eventBlock(buffer.data(), pos);

                        size_t lineStart = 0;
                        while (lineStart < eventBlock.size()) {
                            size_t lineEnd = eventBlock.find('\n', lineStart);
                            if (lineEnd == std::string_view::npos) {
                                lineEnd = eventBlock.size();
                            }
                            std::string_view line = eventBlock.substr(lineStart, lineEnd - lineStart);
                            if (!line.empty() && line.back() == '\r') {
                                line.remove_suffix(1);
                            }
                            lineStart = lineEnd + 1;

                            if (line.rfind("data: ", 0) == 0) {
                                std::string_view jsonStr = line.substr(6);
                                if (jsonStr == "[DONE]") {
                                    break;
                                }
                                try {
                                    auto parsed = json::parse(jsonStr);
                                    if (parsed.contains("choices") && !parsed["choices"].empty()) {
                                        auto& choice = parsed["choices"][0];
                                        if (choice.contains("delta") && choice["delta"].contains("content")) {
                                            std::string token = choice["delta"]["content"].get<std::string>();
                                            accumulatedText += token;
                                            if (aliveToken->load() && onToken) {
                                                onToken(token);
                                            }
                                        }
                                    }
                                } catch (...) {
                                    // 忽略格式不完整的临时 SSE 片段
                                }
                            }
                        }
                        buffer.erase(0, pos + 2);
                    }
                    return true;
                }
            );

            if (!res) {
                hasError = true;
                errorMsg = "HTTP 请求失败: 无法连接至嵌入服务";
            } else if (res->status != 200) {
                hasError = true;
                errorMsg = "HTTP 错误: " + std::to_string(res->status);
            }
        } catch (const std::exception& e) {
            hasError = true;
            errorMsg = std::string("异常: ") + e.what();
        } catch (...) {
            hasError = true;
            errorMsg = "未知 OCR 推理异常";
        }

        m_isRunning.store(false);

        if (!aliveToken->load()) {
            return;
        }

        if (m_shouldStop.load()) {
            if (onComplete) onComplete(accumulatedText, false, "已手动取消");
        } else if (hasError) {
            if (onComplete) onComplete(accumulatedText, false, errorMsg);
        } else {
            if (onComplete) onComplete(accumulatedText, true, "");
        }
    }).detach();
}

void LlamaClient::CancelCurrentTask() {
    m_shouldStop.store(true);
}

bool LlamaClient::IsRunning() const {
    return m_isRunning.load();
}

} // namespace LinguaAlpaca
