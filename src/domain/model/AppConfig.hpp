#pragma once
#include <string>

namespace LinguaAlpaca::Domain::Model {

struct AppConfig {
    std::string modelPath;
    std::string ocrModelPath{"models/PaddleOCR-VL-1.6.gguf"};
    std::string ocrMmprojPath{"models/PaddleOCR-VL-1.6-mmproj.gguf"};
    std::string translationModelName{"Hy-MT2-1.8B-GGUF"};
    std::string ocrModelName{"PaddleOCR-VL-1.6.gguf"};
    std::string themeMode{"Light"}; // "Light" or "Dark"
    bool autoRead{false};
    bool selectionAutoTranslate{true};
    std::string sourceLang{"en"};
    std::string targetLang{"zh"};
};

} // namespace LinguaAlpaca::Domain::Model
