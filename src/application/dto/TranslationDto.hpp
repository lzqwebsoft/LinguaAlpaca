#pragma once
#pragma execution_character_set("utf-8")
#include <string>
#include "../../domain/model/Language.hpp"

namespace LinguaAlpaca::Application::DTO {

struct TranslationRequestDto {
    std::string text;
    Domain::Model::LanguageCode sourceLanguage;
    Domain::Model::LanguageCode targetLanguage;
};

struct TranslationResponseDto {
    bool success{false};
    std::string originalText;
    std::string translatedText;
    std::string errorMessage;
    size_t sourceCharCount{0};
    size_t targetCharCount{0};
};

} // namespace LinguaAlpaca::Application::DTO
