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

    // 划词翻译相关配置
    bool selectionTranslateEnabled{true};
    int selectionTriggerMode{0}; // 0: 鼠标直接划词, 1: 划词+辅助按键, 2: 双击/三击划词
    int selectionModifierKey{0}; // 0: Ctrl, 1: Alt, 2: Shift
    bool preserveClipboard{true}; // 保护剪贴板 (复制提取后恢复原剪贴板内容)
    std::string selectionTargetLang{"zh"};

    // 日志配置
    bool saveLogToFile{false};

    // 词典配置
    std::string dictDirPath;
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager() = default;

    static std::string GetConfigFilePath();
    static std::string GetDefaultModelDir();
    static std::string GetDefaultLogDir();
    static std::string GetDefaultLogFilePath();
    static std::string GetDefaultDictDir();

    AppConfig GetConfig() const;
    void UpdateConfig(const AppConfig& newConfig);

    void SaveModelPath(const std::string& path);
    void SaveOcrConfig(const std::string& ocrModelPath, const std::string& ocrMmprojPath);
    void SaveThemeMode(const std::string& themeMode);
    void SaveSelectionConfig(bool enabled, int mode, int modifierKey, bool preserveClip);
    void SaveLogConfig(bool saveLogToFile);
    void SaveDictDir(const std::string& path);

    bool Load();
    bool Save();

private:
    mutable std::mutex m_mutex;
    AppConfig m_config;
};

} // namespace LinguaAlpaca
