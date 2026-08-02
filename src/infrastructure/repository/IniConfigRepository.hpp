#pragma once
#include <string>
#include "../../domain/model/AppConfig.hpp"

namespace LinguaAlpaca::Infrastructure::Repository {

class IniConfigRepository {
public:
    IniConfigRepository();
    ~IniConfigRepository() = default;

    static std::string GetConfigFilePath();

    Domain::Model::AppConfig LoadConfig();
    bool SaveConfig(const Domain::Model::AppConfig& config);
};

} // namespace LinguaAlpaca::Infrastructure::Repository
