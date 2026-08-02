#include "ConfigurationService.hpp"

namespace LinguaAlpaca::Application::Service {

ConfigurationService::ConfigurationService(std::shared_ptr<Infrastructure::Repository::IniConfigRepository> repo)
    : m_repo(std::move(repo)) {
    if (m_repo) {
        m_config = m_repo->LoadConfig();
    }
}

void ConfigurationService::UpdateConfig(const Domain::Model::AppConfig& newConfig) {
    m_config = newConfig;
    if (m_repo) {
        m_repo->SaveConfig(m_config);
    }
}

void ConfigurationService::SaveModelPath(const std::string& path) {
    m_config.modelPath = path;
    if (m_repo) {
        m_repo->SaveConfig(m_config);
    }
}

void ConfigurationService::SaveThemeMode(const std::string& themeMode) {
    m_config.themeMode = themeMode;
    if (m_repo) {
        m_repo->SaveConfig(m_config);
    }
}

} // namespace LinguaAlpaca::Application::Service
