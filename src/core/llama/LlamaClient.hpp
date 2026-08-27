#pragma once
#pragma execution_character_set("utf-8")

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Types.hpp"
#include "LlamaServer.hpp"

namespace LinguaAlpaca {

class LlamaClient {
public:
    explicit LlamaClient(std::shared_ptr<LlamaServer> server = nullptr);
    explicit LlamaClient(std::string baseUrl);
    ~LlamaClient();

    void SetServer(std::shared_ptr<LlamaServer> server);
    void SetBaseUrl(const std::string& baseUrl);
    std::string GetBaseUrl() const;

    bool IsModelLoaded() const;

    void TranslateStreamAsync(
        const TranslationTask& task,
        StreamTokenCallback onToken,
        StreamCompleteCallback onComplete
    );

    void RecognizeStream(
        const std::string& imagePath,
        const std::string& taskType,
        const std::string& modelPath,
        const std::string& mmprojPath,
        OcrTokenCallback onToken,
        OcrCompleteCallback onComplete
    );

    void CancelCurrentTask();
    void Cancel() { CancelCurrentTask(); }
    bool IsRunning() const;

private:
    std::string FormatHyMt2UserContent(
        const std::string& srcText,
        LanguageCode srcLang,
        LanguageCode tgtLang
    );

    std::shared_ptr<LlamaServer> m_server;
    std::string m_baseUrl;

    std::shared_ptr<std::atomic<bool>> m_aliveToken;
    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_isRunning{false};
    mutable std::mutex m_mutex;
};

} // namespace LinguaAlpaca
