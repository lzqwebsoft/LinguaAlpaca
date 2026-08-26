#pragma once
#pragma execution_character_set("utf-8")

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Types.hpp"
#include "Config.hpp"
#include "llama/LlamaServer.hpp"
#include "llama/LlamaClient.hpp"
#include "dict/DictEngine.hpp"

namespace LinguaAlpaca {

class ModelManager {
public:
    explicit ModelManager(std::shared_ptr<ConfigManager> configManager);
    ~ModelManager();

    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // 异步确保目标模型服务就绪 (互不影响，支持双模型同时在线)
    void EnsureModelAsync(
        TargetModelType type,
        std::function<void(const std::string& statusMsg)> onProgress = nullptr,
        std::function<void(bool success, const ServerStatusInfo& info)> onComplete = nullptr
    );

    void StopModelAsync(
        TargetModelType type = TargetModelType::None,
        std::function<void()> onComplete = nullptr
    );

    // 同步探针查询指定模型健康状态及端口
    ServerStatusInfo GetHealthStatus(TargetModelType targetType) const;

    bool IsSwitching(TargetModelType type = TargetModelType::None) const;

    // 统一推理调度
    void ExecuteTranslationStream(
        const TranslationTask& task,
        StreamTokenCallback onToken,
        StreamCompleteCallback onComplete
    );

    void ExecuteOcrStream(
        const std::string& imagePath,
        const std::string& taskType,
        OcrTokenCallback onToken,
        OcrCompleteCallback onComplete
    );

    void CancelInference(TargetModelType type = TargetModelType::None);

    // 历史记录操作
    void AddHistory(const HistoryRecord& record);
    std::vector<HistoryRecord> GetHistory() const;
    void ClearHistory();

    std::shared_ptr<LlamaClient> GetClient(TargetModelType type = TargetModelType::Translation) const {
        return (type == TargetModelType::Ocr) ? m_ocrClient : m_transClient;
    }
    std::shared_ptr<LlamaServer> GetServer(TargetModelType type = TargetModelType::Translation) const {
        return (type == TargetModelType::Ocr) ? m_ocrServer : m_transServer;
    }
    std::shared_ptr<LlamaServer> GetTranslationServer() const { return m_transServer; }
    std::shared_ptr<LlamaServer> GetOcrServer() const { return m_ocrServer; }
    std::shared_ptr<LlamaClient> GetTranslationClient() const { return m_transClient; }
    std::shared_ptr<LlamaClient> GetOcrClient() const { return m_ocrClient; }

    std::shared_ptr<ConfigManager> GetConfigManager() const { return m_configManager; }
    std::shared_ptr<DictEngine> GetDictEngine() const { return m_dictEngine; }

private:
    std::shared_ptr<ConfigManager> m_configManager;
    std::shared_ptr<LlamaServer> m_transServer;
    std::shared_ptr<LlamaServer> m_ocrServer;
    std::shared_ptr<LlamaClient> m_transClient;
    std::shared_ptr<LlamaClient> m_ocrClient;
    std::shared_ptr<DictEngine> m_dictEngine;

    std::atomic<bool> m_isTransSwitching{false};
    std::atomic<bool> m_isOcrSwitching{false};
    std::atomic<uint64_t> m_transSessionId{0};
    std::atomic<uint64_t> m_ocrSessionId{0};
    mutable std::mutex m_transSwitchMutex;
    mutable std::mutex m_ocrSwitchMutex;

    mutable std::mutex m_historyMutex;
    std::vector<HistoryRecord> m_history;
};

} // namespace LinguaAlpaca
