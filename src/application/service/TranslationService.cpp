#include "TranslationService.hpp"
#include "../../domain/model/TranslationTask.hpp"
#include <chrono>

namespace LinguaAlpaca::Application::Service {

TranslationService::TranslationService(
    std::shared_ptr<Domain::Repository::ITranslationEngine> engine,
    std::shared_ptr<Domain::Repository::IHistoryRepository> historyRepo,
    std::shared_ptr<ConfigurationService> configService)
    : m_engine(std::move(engine))
    , m_historyRepo(std::move(historyRepo))
    , m_configService(std::move(configService)) {}

bool TranslationService::IsModelLoaded() const {
    if (m_engine) {
        return m_engine->IsModelLoaded();
    }
    return false;
}

void TranslationService::ExecuteStreamTranslation(
    const DTO::TranslationRequestDto& request,
    Domain::Repository::StreamTokenCallback onToken,
    Domain::Repository::StreamCompleteCallback onComplete) {
    
    if (request.text.empty()) {
        if (onComplete) onComplete(true, "", "");
        return;
    }

    Domain::Model::TranslationTask task(request.text, request.sourceLanguage, request.targetLanguage);

    auto historyRepo = m_historyRepo;
    m_engine->TranslateStreamAsync(task, onToken, [historyRepo, request, onComplete](bool success, const std::string& fullText, const std::string& error) {
        if (success && historyRepo && !fullText.empty()) {
            Domain::Model::HistoryRecord record;
            record.sourceText = request.text;
            record.translatedText = fullText;
            record.sourceLang = request.sourceLanguage;
            record.targetLang = request.targetLanguage;
            record.timestamp = std::chrono::system_clock::now();
            historyRepo->AddRecord(record);
        }

        if (onComplete) {
            onComplete(success, fullText, error);
        }
    });
}

void TranslationService::CancelTranslation() {
    if (m_engine) {
        m_engine->CancelCurrentTask();
    }
}

} // namespace LinguaAlpaca::Application::Service
