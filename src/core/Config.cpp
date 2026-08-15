#pragma execution_character_set("utf-8")
#include "Config.hpp"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace LinguaAlpaca {

ConfigManager::ConfigManager() {
    Load();
}

std::string ConfigManager::GetConfigFilePath() {
    wxString userDir = wxStandardPaths::Get().GetUserDataDir();
    if (!wxDirExists(userDir)) {
        wxFileName::Mkdir(userDir, 0777, wxPATH_MKDIR_FULL);
    }
    wxString configPath = userDir + wxFileName::GetPathSeparator() + "config.ini";
    return configPath.ToUTF8().data();
}

std::string ConfigManager::GetDefaultModelDir() {
    wxString userDir = wxStandardPaths::Get().GetUserDataDir();
    wxString modelDir = userDir + wxFileName::GetPathSeparator() + "models";
    if (!wxDirExists(modelDir)) {
        wxFileName::Mkdir(modelDir, 0777, wxPATH_MKDIR_FULL);
    }
    return modelDir.ToUTF8().data();
}

AppConfig ConfigManager::GetConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::UpdateConfig(const AppConfig& newConfig) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = newConfig;
    }
    Save();
}

void ConfigManager::SaveModelPath(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.modelPath = path;
    }
    Save();
}

void ConfigManager::SaveOcrConfig(const std::string& ocrModelPath, const std::string& ocrMmprojPath) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.ocrModelPath = ocrModelPath;
        m_config.ocrMmprojPath = ocrMmprojPath;
    }
    Save();
}

void ConfigManager::SaveThemeMode(const std::string& themeMode) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.themeMode = themeMode;
    }
    Save();
}

bool ConfigManager::Load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    wxString path = wxString::FromUTF8(GetConfigFilePath());
    wxFileConfig fileConfig("LinguaAlpaca", "", path);

    m_config.modelPath = fileConfig.Read("/Model/Path", "").ToUTF8().data();
    m_config.ocrModelPath = fileConfig.Read("/OCRModel/Path", "models/PaddleOCR-VL-1.6.gguf").ToUTF8().data();
    m_config.ocrMmprojPath = fileConfig.Read("/OCRModel/MmprojPath", "models/PaddleOCR-VL-1.6-mmproj.gguf").ToUTF8().data();
    m_config.themeMode = fileConfig.Read("/UI/Theme", "Light").ToUTF8().data();
    m_config.autoRead = fileConfig.ReadBool("/UI/AutoRead", false);
    m_config.selectionAutoTranslate = fileConfig.ReadBool("/UI/SelectionAutoTranslate", true);
    m_config.sourceLang = fileConfig.Read("/Language/SourceLang", "en").ToUTF8().data();
    m_config.targetLang = fileConfig.Read("/Language/TargetLang", "zh").ToUTF8().data();
    m_config.gpuLayers = fileConfig.ReadLong("/Model/GpuLayers", 99);       // 99 表示全部是GPU
    m_config.ocrGpuLayers = fileConfig.ReadLong("/OCRModel/GpuLayers", -1); // 这里改为自动

    return true;
}

bool ConfigManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    wxString path = wxString::FromUTF8(GetConfigFilePath());
    wxFileConfig fileConfig("LinguaAlpaca", "", path);

    fileConfig.Write("/Model/Path", wxString::FromUTF8(m_config.modelPath));
    fileConfig.Write("/OCRModel/Path", wxString::FromUTF8(m_config.ocrModelPath));
    fileConfig.Write("/OCRModel/MmprojPath", wxString::FromUTF8(m_config.ocrMmprojPath));
    fileConfig.Write("/UI/Theme", wxString::FromUTF8(m_config.themeMode));
    fileConfig.Write("/UI/AutoRead", m_config.autoRead);
    fileConfig.Write("/UI/SelectionAutoTranslate", m_config.selectionAutoTranslate);
    fileConfig.Write("/Language/SourceLang", wxString::FromUTF8(m_config.sourceLang));
    fileConfig.Write("/Language/TargetLang", wxString::FromUTF8(m_config.targetLang));
    fileConfig.Write("/Model/GpuLayers", (long)m_config.gpuLayers);
    fileConfig.Write("/OCRModel/GpuLayers", (long)m_config.ocrGpuLayers);

    return fileConfig.Flush();
}

} // namespace LinguaAlpaca
