#pragma once
#include <memory>
#include "../../domain/model/AppConfig.hpp"
#include "../../infrastructure/repository/IniConfigRepository.hpp"

namespace LinguaAlpaca::Application::Service {

class ConfigurationService {
public:
    ConfigurationService(std::shared_ptr<Infrastructure::Repository::IniConfigRepository> repo);

    Domain::Model::AppConfig GetConfig() const { return m_config; }
    void UpdateConfig(const Domain::Model::AppConfig& newConfig);
    void SaveModelPath(const std::string& path);
    void SaveThemeMode(const std::string& themeMode);

private:
    std::shared_ptr<Infrastructure::Repository::IniConfigRepository> m_repo;
    Domain::Model::AppConfig m_config;
};

} // namespace LinguaAlpaca::Application::Service
