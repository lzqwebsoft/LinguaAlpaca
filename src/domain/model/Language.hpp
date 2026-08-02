#pragma once
#pragma execution_character_set("utf-8")
#include <string>
#include <vector>

namespace LinguaAlpaca::Domain::Model {

enum class LanguageCode {
    AutoDetect,
    English,
    Chinese,
    Japanese,
    Korean,
    French,
    German,
    Spanish
};

struct LanguageInfo {
    LanguageCode code;
    std::string displayName;
    std::string codeName;
};

class LanguageHelper {
public:
    static const std::vector<LanguageInfo>& GetSupportedLanguages() {
        static const std::vector<LanguageInfo> languages = {
            { LanguageCode::AutoDetect, "自动检测", "auto" },
            { LanguageCode::English,    "英语",     "en" },
            { LanguageCode::Chinese,    "中文",     "zh" },
            { LanguageCode::Japanese,   "日语",     "ja" },
            { LanguageCode::Korean,     "韩语",     "ko" },
            { LanguageCode::French,     "法语",     "fr" },
            { LanguageCode::German,     "德语",     "de" },
            { LanguageCode::Spanish,    "西班牙语", "es" }
        };
        return languages;
    }

    static std::string GetDisplayName(LanguageCode code) {
        for (const auto& lang : GetSupportedLanguages()) {
            if (lang.code == code) return lang.displayName;
        }
        return "未知语言";
    }

    static LanguageCode FromDisplayName(const std::string& name) {
        for (const auto& lang : GetSupportedLanguages()) {
            if (lang.displayName == name) return lang.code;
        }
        return LanguageCode::AutoDetect;
    }
};

} // namespace LinguaAlpaca::Domain::Model
