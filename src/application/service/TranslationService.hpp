#pragma once
#include <memory>
#include "../dto/TranslationDto.hpp"
#include "../../domain/repository/ITranslationEngine.hpp"
#include "../../domain/repository/IHistoryRepository.hpp"
#include "ConfigurationService.hpp"

namespace LinguaAlpaca::Application::Service {

class TranslationService {
public:
    TranslationService(
        std::shared_ptr<Domain::Repository::ITranslationEngine> engine,
        std::shared_ptr<Domain::Repository::IHistoryRepository> historyRepo,
        std::shared_ptr<ConfigurationService> configService = nullptr
    );

    bool LoadModel(const std::string& modelPath);
    bool IsModelLoaded() const;

    std::shared_ptr<ConfigurationService> GetConfigService() const { return m_configService; }

    DTO::TranslationResponseDto ExecuteTranslation(const DTO::TranslationRequestDto& request);
    
    void ExecuteStreamTranslation(
        const DTO::TranslationRequestDto& request,
        Domain::Repository::StreamTokenCallback onToken,
        Domain::Repository::StreamCompleteCallback onComplete
    );

    void CancelTranslation();

    std::string QuickPreview(const std::string& text, Domain::Model::LanguageCode src, Domain::Model::LanguageCode target);

private:
    std::shared_ptr<Domain::Repository::ITranslationEngine> m_engine;
    std::shared_ptr<Domain::Repository::IHistoryRepository> m_historyRepo;
    std::shared_ptr<ConfigurationService> m_configService;
};

} // namespace LinguaAlpaca::Application::Service
