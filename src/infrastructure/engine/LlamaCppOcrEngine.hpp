#pragma execution_character_set("utf-8")
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "../../domain/repository/IOcrEngine.hpp"

struct llama_model;
struct llama_context;
struct mtmd_context;

namespace LinguaAlpaca::Infrastructure::Engine {

class LlamaCppOcrEngine : public Domain::Repository::IOcrEngine {
public:
    LlamaCppOcrEngine();
    ~LlamaCppOcrEngine() override;

    bool LoadModel(const std::string& modelPath, const std::string& mmprojPath) override;
    bool IsModelLoaded() const override { return m_isLoaded; }
    std::string GetModelPath() const override { return m_modelPath; }
    std::string GetMmprojPath() const override { return m_mmprojPath; }

    void RecognizeStream(
        const std::string& imagePath,
        const std::string& taskType,
        const std::string& modelPath,
        const std::string& mmprojPath,
        Domain::Repository::OcrTokenCallback onToken,
        Domain::Repository::OcrCompleteCallback onComplete
    ) override;

    void Cancel() override;
    bool IsRunning() const override { return m_isProcessing; }

private:
    void FreeLoadedModels();

    std::string m_modelPath;
    std::string m_mmprojPath;
    bool m_isLoaded{false};

    llama_model* m_model{nullptr};
    mtmd_context* m_mtmdCtx{nullptr};
    std::mutex m_modelMutex;

    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_isProcessing{false};
    std::thread m_workerThread;
};

} // namespace LinguaAlpaca::Infrastructure::Engine
