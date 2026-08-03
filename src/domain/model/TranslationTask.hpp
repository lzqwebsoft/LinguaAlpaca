#pragma once
#pragma execution_character_set("utf-8")
#include <string>
#include <chrono>
#include "Language.hpp"

namespace LinguaAlpaca::Domain::Model {

enum class TaskStatus {
    Pending,
    Processing,
    Completed,
    Failed,
    Cancelled
};

class TranslationTask {
public:
    TranslationTask() = default;

    TranslationTask(
        std::string sourceText,
        LanguageCode sourceLang,
        LanguageCode targetLang)
        : m_sourceText(std::move(sourceText))
        , m_sourceLang(sourceLang)
        , m_targetLang(targetLang)
        , m_status(TaskStatus::Pending)
        , m_timestamp(std::chrono::system_clock::now()) {}

    TranslationTask(
        std::string id,
        std::string sourceText,
        LanguageCode sourceLang,
        LanguageCode targetLang)
        : m_id(std::move(id))
        , m_sourceText(std::move(sourceText))
        , m_sourceLang(sourceLang)
        , m_targetLang(targetLang)
        , m_status(TaskStatus::Pending)
        , m_timestamp(std::chrono::system_clock::now()) {}

    // Getters
    const std::string& GetId() const { return m_id; }
    const std::string& GetSourceText() const { return m_sourceText; }
    const std::string& GetTranslatedText() const { return m_translatedText; }
    LanguageCode GetSourceLanguage() const { return m_sourceLang; }
    LanguageCode GetTargetLanguage() const { return m_targetLang; }
    TaskStatus GetStatus() const { return m_status; }
    const std::string& GetErrorMessage() const { return m_errorMessage; }
    std::chrono::system_clock::time_point GetTimestamp() const { return m_timestamp; }

    // Setters
    void SetId(std::string id) { m_id = std::move(id); }
    void SetSourceText(std::string text) { m_sourceText = std::move(text); }
    void SetTranslatedText(std::string text) {
        m_translatedText = std::move(text);
        m_status = TaskStatus::Completed;
    }
    void SetSourceLanguage(LanguageCode lang) { m_sourceLang = lang; }
    void SetTargetLanguage(LanguageCode lang) { m_targetLang = lang; }
    void SetStatus(TaskStatus status) { m_status = status; }
    void SetErrorMessage(std::string msg) {
        m_errorMessage = std::move(msg);
        m_status = TaskStatus::Failed;
    }
    void SetTimestamp(std::chrono::system_clock::time_point tp) { m_timestamp = tp; }

private:
    std::string m_id;
    std::string m_sourceText;
    std::string m_translatedText;
    LanguageCode m_sourceLang{LanguageCode::AutoDetect};
    LanguageCode m_targetLang{LanguageCode::Chinese};
    TaskStatus m_status{TaskStatus::Pending};
    std::string m_errorMessage;
    std::chrono::system_clock::time_point m_timestamp{std::chrono::system_clock::now()};
};

} // namespace LinguaAlpaca::Domain::Model
