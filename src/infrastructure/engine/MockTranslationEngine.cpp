#pragma execution_character_set("utf-8")
#include "MockTranslationEngine.hpp"
#include <wx/wx.h>
#include <algorithm>
#include <vector>
#include <chrono>

namespace LinguaAlpaca::Infrastructure::Engine {

MockTranslationEngine::MockTranslationEngine() {
    m_translationDict["Hello, welcome to Lingo Translator!"] = "你好，欢迎使用轻译翻译器！";
    m_translationDict["Hello, welcome to LinguaAlpaca!"] = "你好，欢迎使用灵驼译翻译器！";
    m_translationDict["Hello World"] = "你好，世界";
    m_translationDict["Hello"] = "你好，欢迎使用灵驼译！";
    m_translationDict["LinguaAlpaca"] = "灵驼译";
    m_translationDict["Text Translation"] = "文本翻译";
    m_translationDict["Domain Driven Design"] = "领域驱动设计";
    m_translationDict["Artificial Intelligence"] = "人工智能";
}

MockTranslationEngine::~MockTranslationEngine() {
    CancelCurrentTask();
}

void MockTranslationEngine::TranslateStreamAsync(
    const Domain::Model::TranslationTask& task,
    Domain::Repository::StreamTokenCallback onToken,
    Domain::Repository::StreamCompleteCallback onComplete) {
    
    CancelCurrentTask();
    m_cancelRequested = false;
    m_isProcessing = true;

    std::thread([this, task, onToken, onComplete]() {
        const std::string& src = task.GetSourceText();
        std::string fullResult;
        auto it = m_translationDict.find(src);
        if (it != m_translationDict.end()) {
            fullResult = it->second;
        } else {
            fullResult = "[译文] " + src + " (已由灵驼引擎翻译)";
        }
        std::string accumulated = "";

        std::vector<std::string> tokens;
        for (size_t i = 0; i < fullResult.size();) {
            unsigned char c = fullResult[i];
            size_t len = 1;
            if ((c & 0x80) == 0) len = 1;
            else if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            
            if (i + len <= fullResult.size()) {
                tokens.push_back(fullResult.substr(i, len));
            }
            i += len;
        }

        for (const auto& token : tokens) {
            if (m_cancelRequested) break;

            accumulated += token;

            if (onToken) {
                if (wxTheApp) {
                    wxTheApp->CallAfter([onToken, token]() {
                        onToken(token);
                    });
                } else {
                    onToken(token);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(35));
        }

        bool wasCancelled = m_cancelRequested.load();
        m_isProcessing = false;

        if (onComplete) {
            if (wxTheApp) {
                wxTheApp->CallAfter([onComplete, wasCancelled, accumulated]() {
                    onComplete(!wasCancelled, accumulated, wasCancelled ? "已取消" : "");
                });
            } else {
                onComplete(!wasCancelled, accumulated, wasCancelled ? "已取消" : "");
            }
        }
    }).detach();
}

void MockTranslationEngine::CancelCurrentTask() {
    m_cancelRequested = true;
}

} // namespace LinguaAlpaca::Infrastructure::Engine
