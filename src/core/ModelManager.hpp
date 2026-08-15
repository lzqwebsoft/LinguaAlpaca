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
#include "LlamaServer.hpp"
#include "LlamaClient.hpp"

namespace LinguaAlpaca {

class ModelManager {
public:
    explicit ModelManager(std::shared_ptr<ConfigManager> configManager);
    ~ModelManager();

    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // 异步确保/切换目标模型就绪
    void EnsureModelAsync(
        TargetModelType type,
        std::function<void(const std::string& statusMsg)> onProgress = nullptr,
        std::function<void(bool success, const ServerStatusInfo& info)> onComplete = nullptr
    );

    // 同步探针查询模型健康状态
    ServerStatusInfo GetHealthStatus(TargetModelType targetType) const;

    TargetModelType GetActiveModelType() const {
        return m_activeModelType.load(std::memory_order_acquire);
    }

    bool IsSwitching() const {
        return m_isSwitching.load(std::memory_order_acquire);
    }

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

    void CancelInference();

    // 历史记录操作
    void AddHistory(const HistoryRecord& record);
    std::vector<HistoryRecord> GetHistory() const;
    void ClearHistory();

    std::shared_ptr<LlamaClient> GetClient() const { return m_client; }
    std::shared_ptr<LlamaServer> GetServer() const { return m_server; }
    std::shared_ptr<ConfigManager> GetConfigManager() const { return m_configManager; }

private:
    std::shared_ptr<ConfigManager> m_configManager;
    std::shared_ptr<LlamaServer> m_server;
    std::shared_ptr<LlamaClient> m_client;

    std::atomic<TargetModelType> m_activeModelType{TargetModelType::None};
    std::atomic<bool> m_isSwitching{false};
    mutable std::mutex m_switchMutex;

    mutable std::mutex m_historyMutex;
    std::vector<HistoryRecord> m_history;
};

} // namespace LinguaAlpaca
