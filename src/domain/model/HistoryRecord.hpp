#pragma once
#include <string>
#include <chrono>
#include "Language.hpp"

namespace LinguaAlpaca::Domain::Model {

struct HistoryRecord {
    std::string id;
    std::string sourceText;
    std::string translatedText;
    LanguageCode sourceLang;
    LanguageCode targetLang;
    std::chrono::system_clock::time_point timestamp;
};

} // namespace LinguaAlpaca::Domain::Model
