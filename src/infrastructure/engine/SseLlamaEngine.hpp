#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include "../../domain/repository/ITranslationEngine.hpp"
#include "../../domain/repository/IOcrEngine.hpp"

#include "../server/EmbeddedLlamaServer.hpp"

namespace LinguaAlpaca::Infrastructure::Engine {

class SseLlamaEngine : public Domain::Repository::ITranslationEngine,
                       public Domain::Repository::IOcrEngine {
public:
    explicit SseLlamaEngine(std::shared_ptr<Server::EmbeddedLlamaServer> server = nullptr);
    explicit SseLlamaEngine(std::string baseUrl);
    ~SseLlamaEngine() override;

    void SetServer(std::shared_ptr<Server::EmbeddedLlamaServer> server);

    void SetBaseUrl(const std::string& baseUrl);
    std::string GetBaseUrl() const;

    void SetTranslationModelName(const std::string& modelName);
    std::string GetTranslationModelName() const;

    void SetOcrModelName(const std::string& modelName);
    std::string GetOcrModelName() const;

    void SetOcrMmprojPath(const std::string& mmprojPath);
    std::string GetOcrMmprojPath() const;

    // --- Health & Readiness Check ---
    bool CheckHealth() const;
    bool WaitReady(std::function<bool()> shouldStop = nullptr, int timeoutSec = 30);

    // --- ITranslationEngine implementation ---
    bool IsModelLoaded() const override;

    void TranslateStreamAsync(
        const Domain::Model::TranslationTask& task,
        Domain::Repository::StreamTokenCallback onToken,
        Domain::Repository::StreamCompleteCallback onComplete) override;

    void CancelCurrentTask() override;

    // --- IOcrEngine implementation ---
    std::string GetModelPath() const override;
    std::string GetMmprojPath() const override;

    void RecognizeStream(
        const std::string& imagePath,
        const std::string& taskType,
        const std::string& modelPath,
        const std::string& mmprojPath,
        Domain::Repository::OcrTokenCallback onToken,
        Domain::Repository::OcrCompleteCallback onComplete) override;

    void Cancel() override;
    bool IsRunning() const override;

private:
    std::string FormatHyMt2UserContent(
        const std::string& srcText,
        Domain::Model::LanguageCode srcLang,
        Domain::Model::LanguageCode tgtLang);

private:
    std::shared_ptr<Server::EmbeddedLlamaServer> m_server;
    std::string m_baseUrl;
    std::string m_translationModelName;
    std::string m_ocrModelName;
    std::string m_ocrMmprojPath;

    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_isRunning{false};
    mutable std::mutex m_mutex;
};

} // namespace LinguaAlpaca::Infrastructure::Engine
