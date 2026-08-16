#pragma once
#pragma execution_character_set("utf-8")

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace LinguaAlpaca {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

struct LogMessage {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level{LogLevel::Info};
    std::string tag;
    std::string message;

    std::string FormattedTime() const;
    std::string FormattedString() const;
    const char* LevelString() const;
};

using LogListener = std::function<void(const LogMessage&)>;

class Logger {
public:
    static Logger& GetInstance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Log(LogLevel level, const std::string& tag, const std::string& message);
    void Debug(const std::string& tag, const std::string& message);
    void Info(const std::string& tag, const std::string& message);
    void Warn(const std::string& tag, const std::string& message);
    void Error(const std::string& tag, const std::string& message);

    // 订阅新日志通知（返回唯一的 listenerId 用于解绑）
    size_t AddListener(LogListener listener);
    void RemoveListener(size_t listenerId);

    // 获取内存中保存的历史日志
    std::vector<LogMessage> GetRecentLogs(size_t maxCount = 1000) const;
    void ClearLogs();

    // 文件输出配置
    void SetFileLogging(bool enable, const std::string& filePath = "");
    bool IsFileLoggingEnabled() const;
    std::string GetCurrentLogFilePath() const;

private:
    Logger() = default;
    ~Logger();

    void WriteToFileUnlocked(const LogMessage& msg);

    mutable std::mutex m_mutex;
    std::vector<LogMessage> m_history;
    size_t m_maxHistorySize{2000};

    std::unordered_map<size_t, LogListener> m_listeners;
    size_t m_nextListenerId{1};

    bool m_fileLoggingEnabled{false};
    std::string m_logFilePath;
    void* m_fileHandle{nullptr};
};

#define LOG_DEBUG(tag, msg) ::LinguaAlpaca::Logger::GetInstance().Debug((tag), (msg))
#define LOG_INFO(tag, msg)  ::LinguaAlpaca::Logger::GetInstance().Info((tag), (msg))
#define LOG_WARN(tag, msg)  ::LinguaAlpaca::Logger::GetInstance().Warn((tag), (msg))
#define LOG_ERROR(tag, msg) ::LinguaAlpaca::Logger::GetInstance().Error((tag), (msg))

} // namespace LinguaAlpaca
