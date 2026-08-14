#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include "../../domain/repository/ITranslationEngine.hpp"

struct llama_model;
struct llama_context;

namespace LinguaAlpaca::Infrastructure::Engine {

class LlamaCppTranslationEngine : public Domain::Repository::ITranslationEngine {
public:
    LlamaCppTranslationEngine(const std::string& modelPath = "");
    ~LlamaCppTranslationEngine() override;

    bool LoadModel(const std::string& modelPath);
    bool IsModelLoaded() const override { return m_isLoaded; }

    void TranslateStreamAsync(
        const Domain::Model::TranslationTask& task,
        Domain::Repository::StreamTokenCallback onToken,
        Domain::Repository::StreamCompleteCallback onComplete) override;

    void CancelCurrentTask() override;

private:
    llama_model* m_model{nullptr};
    llama_context* m_ctx{nullptr};

    std::string m_modelPath;
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_isProcessing{false};
    bool m_isLoaded{false};
};

} // namespace LinguaAlpaca::Infrastructure::Engine
