#pragma once
#include <string>

namespace LinguaAlpaca::Domain::Model {

struct AppConfig {
    std::string modelPath;
    std::string themeMode{"Light"}; // "Light" or "Dark"
    bool autoRead{false};
    bool selectionAutoTranslate{true};
    std::string sourceLang{"en"};
    std::string targetLang{"zh"};
};

} // namespace LinguaAlpaca::Domain::Model
