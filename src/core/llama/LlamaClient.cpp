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

void LlamaClient::TranslateStreamAsync(const TranslationTask& task, StreamTokenCallback onToken, StreamCompleteCallback onComplete,
                                       std::shared_ptr<std::atomic<bool>> taskCancelToken) {

    // 关键：当传入独立的 taskCancelToken 时，仅受该批次任务自身令牌控制，
    // 避免并行翻译时相互调用 CancelCurrentTask() 而取消对方的全局 m_shouldStop 标志。
    if (!taskCancelToken) {
        CancelCurrentTask();
        m_shouldStop.store(false);
    }
    m_activeRequests.fetch_add(1);
    m_isRunning.store(true);
    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, taskCancelToken, task, onToken, onComplete]() {
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
                // 关键修复：传入独立 taskCancelToken 时仅受该任务自身令牌控制，隔离全局历史 m_shouldStop 标志
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

        m_activeRequests.fetch_sub(1);
        if (m_activeRequests.load() <= 0) {
            m_isRunning.store(false);
        }

        if (!aliveToken->load()) {
            return;
        }

        bool isCancelled = taskCancelToken ? taskCancelToken->load() : m_shouldStop.load();
        if (isCancelled) {
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

static bool IsPureSeparatorContent(std::string_view text) {
    if (text.empty()) return true;

    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
            continue;
        }
        if (c == '.' || c == '-' || c == '_' || c == '*' || c == '~' || c == '=' || c == '#') {
            i++;
            continue;
        }
        // UTF-8 multi-byte characters
        if (c == 0xC2 && i + 1 < text.size()) {
            unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
            if (c2 == 0xB7) { // · (middle dot U+00B7)
                i += 2;
                continue;
            }
        }
        if (c == 0xE2 && i + 2 < text.size()) {
            unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c3 = static_cast<unsigned char>(text[i + 2]);
            if (c2 == 0x80) {
                if (c3 == 0xA2 || // • (bullet U+2022)
                    c3 == 0xA6 || // … (ellipsis U+2026)
                    c3 == 0x93 || // – (en dash U+2013)
                    c3 == 0x94) { // — (em dash U+2014)
                    i += 3;
                    continue;
                }
            }
            if (c2 == 0x94) {
                if (c3 >= 0x80 && c3 <= 0x8F) { // Box drawing
                    i += 3;
                    continue;
                }
            }
        }
        if (c == 0xE3 && i + 2 < text.size()) {
            unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c3 = static_cast<unsigned char>(text[i + 2]);
            if (c2 == 0x83 && c3 == 0xBB) { // ・ (katakana middle dot U+30FB)
                i += 3;
                continue;
            }
        }
        return false;
    }
    return true;
}

bool LlamaClient::DetectRunawayRepetition(std::string_view text) {
    if (text.size() < 24) return false;

    const size_t checkLen = (std::min)(text.size(), size_t(160));
    std::string_view tail = text.substr(text.size() - checkLen);
    const size_t tailSize = tail.size();

    for (size_t patLen = 1; patLen <= 32; ++patLen) {
        if (patLen * 3 > tailSize) break;
        std::string_view pat = tail.substr(tailSize - patLen, patLen);

        size_t count = 1;
        size_t offset = patLen;
        while (offset + patLen <= tailSize) {
            if (tail.substr(tailSize - offset - patLen, patLen) == pat) {
                count++;
                offset += patLen;
            } else {
                break;
            }
        }

        if (IsPureSeparatorContent(pat)) {
            if (patLen <= 4 && count >= 8 && patLen * count >= 16) {
                return true;
            }
            if (patLen > 4 && count >= 4 && patLen * count >= 20) {
                return true;
            }
        } else {
            if (patLen <= 3 && count >= 10 && patLen * count >= 20) {
                return true;
            }
            if (patLen >= 4 && count >= 5 && patLen * count >= 30) {
                return true;
            }
        }
    }
    return false;
}

std::string LlamaClient::TruncateRunawayRepetitions(std::string_view s) {
    if (s.size() < 16) return std::string(s);

    size_t cutPos = std::string::npos;
    const size_t len = s.size();
    size_t matchedPatLen = 0;
    size_t matchedCount = 0;
    bool isSeparatorPat = false;

    for (size_t patLen = 1; patLen <= 120; ++patLen) {
        if (patLen * 3 > len) break;
        bool found = false;
        for (size_t i = 0; i + patLen * 3 <= len; ++i) {
            std::string_view pat = s.substr(i, patLen);
            size_t count = 1;
            size_t pos = i + patLen;
            while (pos + patLen <= len && s.substr(pos, patLen) == pat) {
                count++;
                pos += patLen;
            }

            if (IsPureSeparatorContent(pat)) {
                if (patLen <= 4 && count >= 6 && patLen * count >= 12) {
                    cutPos = i;
                    matchedPatLen = patLen;
                    matchedCount = count;
                    isSeparatorPat = true;
                    found = true;
                    break;
                }
                if (patLen > 4 && count >= 3 && patLen * count >= 20) {
                    cutPos = i;
                    matchedPatLen = patLen;
                    matchedCount = count;
                    isSeparatorPat = true;
                    found = true;
                    break;
                }
            } else {
                if (patLen <= 3 && count >= 8 && patLen * count >= 16) {
                    cutPos = i;
                    found = true;
                    break;
                }
                if (patLen >= 4 && ((count >= 3 && patLen * count >= 24) || count >= 5)) {
                    cutPos = i;
                    found = true;
                    break;
                }
            }
        }
        if (found) break;
    }

    if (cutPos == std::string::npos) {
        return std::string(s);
    }

    // 若从首字符起（或前导空白后）即为纯分隔符死循环 (例如自回归失控连续输出数百个 · · · 或 .....)
    // 根据需求不彻底丢弃，而是保留精简适量的原版分隔符号 (保留至多 5 次模式重复，如 · · · · · 或 .....)
    bool isLeadingWhitespace = (s.substr(0, cutPos).find_first_not_of(" \t\r\n") == std::string_view::npos);
    if (isLeadingWhitespace && isSeparatorPat && matchedPatLen > 0) {
        size_t keepCount = (std::min)(matchedCount, size_t(5));
        std::string kept;
        std::string_view pat = s.substr(cutPos, matchedPatLen);
        for (size_t k = 0; k < keepCount; ++k) {
            kept.append(pat);
        }
        size_t last = kept.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) {
            kept.erase(last + 1);
        }
        return kept;
    }

    std::string result(s.substr(0, cutPos));

    static const std::regex s_trailingNoiseRegex(
        R"((?:\\mathrm|\\text)\s*\{\s*~?\s*(?:a\s*n\s*d|o\s*r|b\s*a\s*s\s*s|and|or|to|the|is|in|of|where|with)\b[\s\S]*$)",
        std::regex::icase
    );
    result = std::regex_replace(result, s_trailingNoiseRegex, "");

    static const std::regex s_trailingIncompleteCmd(R"(\\[a-zA-Z]*$)");
    result = std::regex_replace(result, s_trailingIncompleteCmd, "");

    size_t last = result.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) {
        result.erase(last + 1);
    } else {
        result.clear();
    }

    return result;
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
        bool runawayDetected = false;
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
                                            // 关键防护：检测自回归失控重复，提前中止流式生成，防止空耗数千 Token
                                            if (DetectRunawayRepetition(accumulatedText)) {
                                                runawayDetected = true;
                                                return false;
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

            if (!res && !runawayDetected) {
                hasError = true;
                errorMsg = "HTTP 请求失败: 无法连接至嵌入服务 (" + currentBaseUrl + ")";
                LOG_ERROR("LlamaClient", errorMsg);
            } else if (res && res->status != 200) {
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

        std::string finalCleanText = TruncateRunawayRepetitions(accumulatedText);
        finalCleanText = SanitizeOcrToken(finalCleanText);
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
            } else if (hasError && !runawayDetected) {
                if (onComplete)
                    onComplete(finalCleanText, false, errorMsg);
            } else {
                // 正常完成（包含模型返回空文本/图像无文字/失控早停等合法情况，非服务端异常）
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
