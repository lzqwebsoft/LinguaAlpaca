#pragma execution_character_set("utf-8")
#include "IniConfigRepository.hpp"
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>

namespace LinguaAlpaca::Infrastructure::Repository {

IniConfigRepository::IniConfigRepository() {}

std::string IniConfigRepository::GetConfigFilePath() {
    wxString userDir = wxStandardPaths::Get().GetUserDataDir();
    if (!wxDirExists(userDir)) {
        wxFileName::Mkdir(userDir, 0777, wxPATH_MKDIR_FULL);
    }
    wxString configPath = userDir + wxFileName::GetPathSeparator() + "config.ini";
    return configPath.ToUTF8().data();
}

Domain::Model::AppConfig IniConfigRepository::LoadConfig() {
    Domain::Model::AppConfig cfg;
    wxString path = wxString::FromUTF8(GetConfigFilePath());
    wxFileConfig fileConfig("LinguaAlpaca", "", path);

    cfg.modelPath = fileConfig.Read("/Model/Path", "").ToUTF8().data();
    cfg.ocrModelPath = fileConfig.Read("/OCRModel/Path", "models/PaddleOCR-VL-1.6.gguf").ToUTF8().data();
    cfg.ocrMmprojPath = fileConfig.Read("/OCRModel/MmprojPath", "models/PaddleOCR-VL-1.6-mmproj.gguf").ToUTF8().data();
    cfg.themeMode = fileConfig.Read("/UI/Theme", "Light").ToUTF8().data();
    cfg.autoRead = fileConfig.ReadBool("/UI/AutoRead", false);
    cfg.selectionAutoTranslate = fileConfig.ReadBool("/UI/SelectionAutoTranslate", true);
    cfg.sourceLang = fileConfig.Read("/Language/SourceLang", "en").ToUTF8().data();
    cfg.targetLang = fileConfig.Read("/Language/TargetLang", "zh").ToUTF8().data();

    return cfg;
}

bool IniConfigRepository::SaveConfig(const Domain::Model::AppConfig& config) {
    wxString path = wxString::FromUTF8(GetConfigFilePath());
    wxFileConfig fileConfig("LinguaAlpaca", "", path);

    fileConfig.Write("/Model/Path", wxString::FromUTF8(config.modelPath));
    fileConfig.Write("/OCRModel/Path", wxString::FromUTF8(config.ocrModelPath));
    fileConfig.Write("/OCRModel/MmprojPath", wxString::FromUTF8(config.ocrMmprojPath));
    fileConfig.Write("/UI/Theme", wxString::FromUTF8(config.themeMode));
    fileConfig.Write("/UI/AutoRead", config.autoRead);
    fileConfig.Write("/UI/SelectionAutoTranslate", config.selectionAutoTranslate);
    fileConfig.Write("/Language/SourceLang", wxString::FromUTF8(config.sourceLang));
    fileConfig.Write("/Language/TargetLang", wxString::FromUTF8(config.targetLang));

    return fileConfig.Flush();
}

} // namespace LinguaAlpaca::Infrastructure::Repository
