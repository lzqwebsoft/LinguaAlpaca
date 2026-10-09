#pragma execution_character_set("utf-8")
#include "LlamaClient.hpp"

#include <algorithm>
#include <base64.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

#include <http.h>
#include "core/Logger.hpp"

using json = nlohmann::json;

namespace LinguaAlpaca {

static std::string GetOcrPromptPrefix(const std::string& taskType) {
    if (taskType == "table")
        return "Table Recognition:";
    if (taskType == "formula")
        return "Formula Recognition:";
    if (taskType == "chart")
        return "Chart Recognition:";
    if (taskType == "spotting")
        return "Spotting:";
    if (taskType == "seal")
        return "Seal Recognition:";
    return "OCR:";
}

static std::string FormatImageUrl(const std::string& imagePath) {
    if (imagePath.empty())
        return "";

    if (imagePath.rfind("data:image/", 0) == 0 || imagePath.rfind("http://", 0) == 0 || imagePath.rfind("https://", 0) == 0) {
        return imagePath;
    }

#ifdef _WIN32
    std::filesystem::path p = std::filesystem::u8path(imagePath);
    std::ifstream file(p, std::ios::binary);
#else
    std::ifstream file(imagePath, std::ios::binary);
#endif
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
    : m_server(std::move(server))
    , m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {
    if (m_server) {
        m_baseUrl = m_server->GetBaseUrl();
    }
}

LlamaClient::LlamaClient(std::string baseUrl)
    : m_baseUrl(std::move(baseUrl))
    , m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {}

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

std::string LlamaClient::FormatHyMt2UserContent(const std::string& srcText, LanguageCode srcLang, LanguageCode tgtLang) {

    std::string targetLangName = LanguageHelper::GetDisplayName(tgtLang);
    std::string srcLangName = LanguageHelper::GetDisplayName(srcLang);

    if (srcLang == LanguageCode::AutoDetect) {
        return "将以下文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
    }
    return "将以下" + srcLangName + "文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
}

void LlamaClient::TranslateStreamAsync(const TranslationTask& task, StreamTokenCallback onToken, StreamCompleteCallback onComplete) {

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
            if (aliveToken->load() && onComplete)
                onComplete(false, "", "服务地址为空或未启动");
            return;
        }

        std::string userPrompt = FormatHyMt2UserContent(task.GetSourceText(), task.GetSourceLanguage(), task.GetTargetLanguage());

        json body = {{"messages", json::array({{{"role", "user"}, {"content", userPrompt}}})},
                     {"stream", true},
                     {"temperature", 0.7},
                     {"top_p", 0.6},
                     {"top_k", 20},
                     {"repetition_penalty", 1.05},
                     {"max_tokens", 4096}};

        std::string reqBody = body.dump(-1, ' ', false, json::error_handler_t::replace);
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
            auto res = cli.Post(path, headers, reqBody, "application/json", [&](const char* data, size_t len) {
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
            });

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
            if (onComplete)
                onComplete(false, accumulatedText, "已手动取消");
        } else if (hasError) {
            if (onComplete)
                onComplete(false, accumulatedText, errorMsg);
        } else {
            if (onComplete)
                onComplete(true, accumulatedText, "");
        }
    }).detach();
}

std::string LlamaClient::SanitizeOcrToken(const std::string& token) {
    std::string clean;
    clean.reserve(token.size());
    for (size_t i = 0; i < token.size(); ++i) {
        unsigned char uch = static_cast<unsigned char>(token[i]);
        // 过滤不可打印控制字符 (0x00 - 0x1F，但保留合法换行、回车与制表符: \n, \r, \t)
        if (uch < 0x20 && uch != '\n' && uch != '\r' && uch != '\t') {
            continue;
        }
        // 过滤 Unicode 替换字符 U+FFFD (\xEF\xBF\xBD)
        if (uch == 0xEF && i + 2 < token.size() && static_cast<unsigned char>(token[i + 1]) == 0xBF && static_cast<unsigned char>(token[i + 2]) == 0xBD) {
            i += 2;
            continue;
        }
        clean.push_back(token[i]);
    }
    return clean;
}

void LlamaClient::RecognizeStream(const std::string& imagePath, const std::string& taskType, const std::string& /*modelPath*/, const std::string& /*mmprojPath*/, OcrTokenCallback onToken,
                                  OcrCompleteCallback onComplete, std::shared_ptr<std::atomic<bool>> taskCancelToken) {

    if (!taskCancelToken) {
        CancelCurrentTask();
        m_shouldStop.store(false);
    }
    m_activeRequests.fetch_add(1);
    m_isRunning.store(true);
    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, taskCancelToken, imagePath, taskType, onToken, onComplete]() {
        if (!aliveToken->load()) {
            m_activeRequests.fetch_sub(1);
            if (m_activeRequests.load() <= 0) m_isRunning.store(false);
            return;
        }

        std::string currentBaseUrl = GetBaseUrl();
        if (currentBaseUrl.empty()) {
            m_activeRequests.fetch_sub(1);
            if (m_activeRequests.load() <= 0) m_isRunning.store(false);
            if (aliveToken->load() && onComplete)
                onComplete("", false, "服务地址为空或未启动");
            return;
        }

        std::string promptPrefix = GetOcrPromptPrefix(taskType);
        std::string imageUrl = FormatImageUrl(imagePath);

        json messageContent = json::array();
        if (!imageUrl.empty()) {
            messageContent.push_back({{"type", "image_url"}, {"image_url", {{"url", imageUrl}}}});
        }
        messageContent.push_back({{"type", "text"}, {"text", promptPrefix}});

        json body = {
            {"model", "default"},        {"messages", json::array({{{"role", "user"}, {"content", messageContent}}})}, {"stream", true}, {"temperature", 0.1}, {"top_p", 0.9}, {"max_tokens", 4096},
            {"repetition_penalty", 1.05}};

        std::string reqBody = body.dump(-1, ' ', false, json::error_handler_t::replace);
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
            auto res = cli.Post(path, headers, reqBody, "application/json", [&](const char* data, size_t len) {
                // 关键修复：当传入独立的 taskCancelToken 时，仅受该批次任务自身令牌控制，彻底隔离全局历史 m_shouldStop 标志
                bool isCancelled = taskCancelToken ? taskCancelToken->load() : m_shouldStop.load();
                if (!aliveToken->load() || isCancelled) {
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
                                        std::string cleanToken = SanitizeOcrToken(token);
                                        if (!cleanToken.empty()) {
                                            accumulatedText += cleanToken;
                                            if (aliveToken->load() && onToken) {
                                                onToken(cleanToken);
                                            }
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
            });

            if (!res) {
                hasError = true;
                errorMsg = "HTTP 请求失败: 无法连接至嵌入服务 (" + currentBaseUrl + ")";
                LOG_ERROR("LlamaClient", errorMsg);
            } else if (res->status != 200) {
                hasError = true;
                errorMsg = "HTTP 错误 " + std::to_string(res->status) + ": " + res->body;
                LOG_ERROR("LlamaClient", errorMsg);
            }
        } catch (const std::exception& e) {
            hasError = true;
            errorMsg = std::string("异常: ") + e.what();
            LOG_ERROR("LlamaClient", errorMsg);
        } catch (...) {
            hasError = true;
            errorMsg = "未知 OCR 推理异常";
            LOG_ERROR("LlamaClient", errorMsg);
        }

        m_activeRequests.fetch_sub(1);
        if (m_activeRequests.load() <= 0) {
            m_isRunning.store(false);
        }

        if (!aliveToken->load()) {
            return;
        }

        std::string finalCleanText = SanitizeOcrToken(accumulatedText);
        while (!finalCleanText.empty() && (finalCleanText.front() == ' ' || finalCleanText.front() == '\n' || finalCleanText.front() == '\r' || finalCleanText.front() == '\t')) {
            finalCleanText.erase(finalCleanText.begin());
        }
        while (!finalCleanText.empty() && (finalCleanText.back() == ' ' || finalCleanText.back() == '\n' || finalCleanText.back() == '\r' || finalCleanText.back() == '\t')) {
            finalCleanText.pop_back();
        }

        bool isCancelled = taskCancelToken ? taskCancelToken->load() : m_shouldStop.load();
        try {
            if (isCancelled) {
                if (onComplete)
                    onComplete(finalCleanText, false, "已手动取消");
            } else if (hasError) {
                if (onComplete)
                    onComplete(finalCleanText, false, errorMsg);
            } else {
                // 正常完成（包含模型返回空文本/图像无文字等合法情况，非服务端异常）
                if (onComplete)
                    onComplete(finalCleanText, true, "");
            }
        } catch (...) {}
    }).detach();
}

void LlamaClient::CancelCurrentTask() {
    m_shouldStop.store(true);
}

bool LlamaClient::IsRunning() const {
    return m_activeRequests.load() > 0 || m_isRunning.load();
}

} // namespace LinguaAlpaca
