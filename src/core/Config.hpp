#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <mutex>
#include <memory>

namespace LinguaAlpaca {

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
    int gpuLayers{99};
    int ocrGpuLayers{0}; // 默认 OCR 使用 0 层（CPU模式），避免 Vulkan 显存分配超限
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager() = default;

    static std::string GetConfigFilePath();
    static std::string GetDefaultModelDir();

    AppConfig GetConfig() const;
    void UpdateConfig(const AppConfig& newConfig);

    void SaveModelPath(const std::string& path);
    void SaveOcrConfig(const std::string& ocrModelPath, const std::string& ocrMmprojPath);
    void SaveThemeMode(const std::string& themeMode);

    bool Load();
    bool Save();

private:
    mutable std::mutex m_mutex;
    AppConfig m_config;
};

} // namespace LinguaAlpaca
