#pragma execution_character_set("utf-8")
#include "Logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace LinguaAlpaca {

const char* LogMessage::LevelString() const {
    switch (level) {
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    default:                return "INFO";
    }
}

std::string LogMessage::FormattedTime() const {
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(timestamp.time_since_epoch()) % 1000;
    std::time_t tt = system_clock::to_time_t(timestamp);
    std::tm tmVal;
#ifdef _WIN32
    localtime_s(&tmVal, &tt);
#else
    localtime_r(&tt, &tmVal);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmVal, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogMessage::FormattedString() const {
    std::ostringstream oss;
    oss << "[" << FormattedTime() << "] ["
        << LevelString() << "] ["
        << tag << "] "
        << message;
    return oss.str();
}

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_fileHandle) {
        std::ofstream* ofs = static_cast<std::ofstream*>(m_fileHandle);
        if (ofs->is_open()) {
            ofs->flush();
            ofs->close();
        }
        delete ofs;
        m_fileHandle = nullptr;
    }
}

void Logger::Log(LogLevel level, const std::string& tag, const std::string& message) {
    LogMessage msg;
    msg.timestamp = std::chrono::system_clock::now();
    msg.level = level;
    msg.tag = tag;
    msg.message = message;

    std::string formatted = msg.FormattedString();

    // 1. 控制台与调试器输出
    if (level == LogLevel::Error) {
        std::cerr << formatted << std::endl;
    } else {
        std::cout << formatted << std::endl;
    }

#ifdef _WIN32
    OutputDebugStringA((formatted + "\n").c_str());
#endif

    // 2. 线程安全分发与存储
    std::vector<LogListener> listenersToNotify;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 写入内存历史列表
        m_history.push_back(msg);
        if (m_history.size() > m_maxHistorySize) {
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - m_maxHistorySize));
        }

        // 如果启用了文件日志，写入本地文件
        if (m_fileLoggingEnabled) {
            WriteToFileUnlocked(msg);
        }

        // 收集监听者
        for (const auto& kv : m_listeners) {
            if (kv.second) {
                listenersToNotify.push_back(kv.second);
            }
        }
    }

    // 3. 在锁外分发给监听者，防止死锁
    for (const auto& listener : listenersToNotify) {
        listener(msg);
    }
}

void Logger::Debug(const std::string& tag, const std::string& message) {
    Log(LogLevel::Debug, tag, message);
}

void Logger::Info(const std::string& tag, const std::string& message) {
    Log(LogLevel::Info, tag, message);
}

void Logger::Warn(const std::string& tag, const std::string& message) {
    Log(LogLevel::Warning, tag, message);
}

void Logger::Error(const std::string& tag, const std::string& message) {
    Log(LogLevel::Error, tag, message);
}

size_t Logger::AddListener(LogListener listener) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t id = m_nextListenerId++;
    m_listeners[id] = std::move(listener);
    return id;
}

void Logger::RemoveListener(size_t listenerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.erase(listenerId);
}

std::vector<LogMessage> Logger::GetRecentLogs(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_history.size() <= maxCount) {
        return m_history;
    }
    return std::vector<LogMessage>(m_history.end() - maxCount, m_history.end());
}

void Logger::ClearLogs() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
}

void Logger::SetFileLogging(bool enable, const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fileLoggingEnabled = enable;
    if (!filePath.empty()) {
        m_logFilePath = filePath;
    }

    if (!m_fileLoggingEnabled && m_fileHandle) {
        std::ofstream* ofs = static_cast<std::ofstream*>(m_fileHandle);
        if (ofs->is_open()) {
            ofs->flush();
            ofs->close();
        }
        delete ofs;
        m_fileHandle = nullptr;
    }
}

bool Logger::IsFileLoggingEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fileLoggingEnabled;
}

std::string Logger::GetCurrentLogFilePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_logFilePath;
}

void Logger::WriteToFileUnlocked(const LogMessage& msg) {
    if (m_logFilePath.empty()) {
        return;
    }

    std::ofstream* ofs = static_cast<std::ofstream*>(m_fileHandle);
    if (!ofs) {
        ofs = new std::ofstream(m_logFilePath, std::ios::app);
        m_fileHandle = ofs;
    }

    if (ofs && ofs->is_open()) {
        *ofs << msg.FormattedString() << "\n";
        ofs->flush();
    }
}

} // namespace LinguaAlpaca
