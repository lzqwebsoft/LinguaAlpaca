#pragma once
#include <unordered_map>
#include <thread>
#include <atomic>
#include "../../domain/repository/ITranslationEngine.hpp"

namespace LinguaAlpaca::Infrastructure::Engine {

class MockTranslationEngine : public Domain::Repository::ITranslationEngine {
public:
    MockTranslationEngine();
    ~MockTranslationEngine() override;

    bool LoadModel(const std::string& modelPath) override;
    bool IsModelLoaded() const override { return m_modelLoaded; }

    Domain::Model::TranslationTask Translate(const Domain::Model::TranslationTask& task) override;
    std::string QuickTranslate(const std::string& text, Domain::Model::LanguageCode sourceLang, Domain::Model::LanguageCode targetLang) override;

    void TranslateStreamAsync(
        const Domain::Model::TranslationTask& task,
        Domain::Repository::StreamTokenCallback onToken,
        Domain::Repository::StreamCompleteCallback onComplete) override;

    void CancelCurrentTask() override;

private:
    std::unordered_map<std::string, std::string> m_translationDict;
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_isProcessing{false};
    bool m_modelLoaded{true};
};

} // namespace LinguaAlpaca::Infrastructure::Engine
