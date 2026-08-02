#include "TranslationService.hpp"
#include "../../domain/model/TranslationTask.hpp"

namespace LinguaAlpaca::Application::Service {

TranslationService::TranslationService(
    std::shared_ptr<Domain::Repository::ITranslationEngine> engine,
    std::shared_ptr<Domain::Repository::IHistoryRepository> historyRepo,
    std::shared_ptr<ConfigurationService> configService)
    : m_engine(std::move(engine))
    , m_historyRepo(std::move(historyRepo))
    , m_configService(std::move(configService)) {}

bool TranslationService::LoadModel(const std::string& modelPath) {
    if (m_engine) {
        bool ok = m_engine->LoadModel(modelPath);
        if (ok && m_configService) {
            m_configService->SaveModelPath(modelPath);
        }
        return ok;
    }
    return false;
}

bool TranslationService::IsModelLoaded() const {
    if (m_engine) {
        return m_engine->IsModelLoaded();
    }
    return false;
}

DTO::TranslationResponseDto TranslationService::ExecuteTranslation(const DTO::TranslationRequestDto& request) {
    DTO::TranslationResponseDto response;
    response.originalText = request.text;
    response.sourceCharCount = request.text.length();

    if (request.text.empty()) {
        response.success = true;
        response.translatedText = "";
        response.targetCharCount = 0;
        return response;
    }

    Domain::Model::TranslationTask task(request.text, request.sourceLanguage, request.targetLanguage);
    auto resultTask = m_engine->Translate(task);

    if (resultTask.GetStatus() == Domain::Model::TaskStatus::Completed) {
        response.success = true;
        response.translatedText = resultTask.GetTranslatedText();
        response.targetCharCount = response.translatedText.length();

        if (m_historyRepo) {
            Domain::Model::HistoryRecord record;
            record.sourceText = request.text;
            record.translatedText = response.translatedText;
            record.sourceLang = request.sourceLanguage;
            record.targetLang = request.targetLanguage;
            record.timestamp = std::chrono::system_clock::now();
            m_historyRepo->AddRecord(record);
        }
    } else {
        response.success = false;
        response.errorMessage = resultTask.GetErrorMessage();
    }

    return response;
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

std::string TranslationService::QuickPreview(const std::string& text, Domain::Model::LanguageCode src, Domain::Model::LanguageCode target) {
    if (text.empty()) return "";
    return m_engine->QuickTranslate(text, src, target);
}

} // namespace LinguaAlpaca::Application::Service
