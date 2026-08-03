#pragma once
#pragma execution_character_set("utf-8")
#include <string>
#include <vector>

namespace LinguaAlpaca::Domain::Model {

enum class LanguageCode {
    AutoDetect,
    Chinese,
    English,
    French,
    Portuguese,
    Spanish,
    Japanese,
    Turkish,
    Russian,
    Arabic,
    Korean,
    Thai,
    Italian,
    German,
    Vietnamese,
    Malay,
    Indonesian,
    Filipino,
    Hindi,
    TraditionalChinese,
    Polish,
    Czech,
    Dutch,
    Khmer,
    Burmese,
    Persian,
    Gujarati,
    Urdu,
    Telugu,
    Marathi,
    Hebrew,
    Bengali,
    Tamil,
    Ukrainian,
    Tibetan,
    Kazakh,
    Mongolian,
    Uyghur,
    Cantonese
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
            { LanguageCode::AutoDetect,         "自动检测", "auto" },
            { LanguageCode::Chinese,            "中文",     "zh" },
            { LanguageCode::TraditionalChinese, "繁体中文", "zh-Hant" },
            { LanguageCode::Cantonese,          "粤语",     "yue" },
            { LanguageCode::English,            "英语",     "en" },
            { LanguageCode::French,             "法语",     "fr" },
            { LanguageCode::Portuguese,         "葡萄牙语", "pt" },
            { LanguageCode::Spanish,            "西班牙语", "es" },
            { LanguageCode::Japanese,           "日语",     "ja" },
            { LanguageCode::Turkish,            "土耳其语", "tr" },
            { LanguageCode::Russian,            "俄语",     "ru" },
            { LanguageCode::Arabic,             "阿拉伯语", "ar" },
            { LanguageCode::Korean,             "韩语",     "ko" },
            { LanguageCode::Thai,               "泰语",     "th" },
            { LanguageCode::Italian,            "意大利语", "it" },
            { LanguageCode::German,             "德语",     "de" },
            { LanguageCode::Vietnamese,         "越南语",   "vi" },
            { LanguageCode::Malay,              "马来语",   "ms" },
            { LanguageCode::Indonesian,         "印尼语",   "id" },
            { LanguageCode::Filipino,           "菲律宾语", "tl" },
            { LanguageCode::Hindi,              "印地语",   "hi" },
            { LanguageCode::Polish,             "波兰语",   "pl" },
            { LanguageCode::Czech,              "捷克语",   "cs" },
            { LanguageCode::Dutch,              "荷兰语",   "nl" },
            { LanguageCode::Khmer,              "高棉语",   "km" },
            { LanguageCode::Burmese,            "缅甸语",   "my" },
            { LanguageCode::Persian,            "波斯语",   "fa" },
            { LanguageCode::Gujarati,           "古吉拉特语", "gu" },
            { LanguageCode::Urdu,               "乌尔都语", "ur" },
            { LanguageCode::Telugu,             "泰卢固语", "te" },
            { LanguageCode::Marathi,            "马拉地语", "mr" },
            { LanguageCode::Hebrew,             "希伯来语", "he" },
            { LanguageCode::Bengali,            "孟加拉语", "bn" },
            { LanguageCode::Tamil,              "泰米尔语", "ta" },
            { LanguageCode::Ukrainian,          "乌克兰语", "uk" },
            { LanguageCode::Tibetan,            "藏语",     "bo" },
            { LanguageCode::Kazakh,             "哈萨克语", "kk" },
            { LanguageCode::Mongolian,          "蒙古语",   "mn" },
            { LanguageCode::Uyghur,             "维吾尔语", "ug" }
        };
        return languages;
    }

    static std::string GetDisplayName(LanguageCode code) {
        for (const auto& lang : GetSupportedLanguages()) {
            if (lang.code == code) return lang.displayName;
        }
        return "未知语言";
    }

    static std::string GetCodeName(LanguageCode code) {
        for (const auto& lang : GetSupportedLanguages()) {
            if (lang.code == code) return lang.codeName;
        }
        return "auto";
    }

    static LanguageCode FromDisplayName(const std::string& name) {
        for (const auto& lang : GetSupportedLanguages()) {
            if (lang.displayName == name) return lang.code;
        }
        return LanguageCode::AutoDetect;
    }

    static LanguageCode FromCodeName(const std::string& codeName) {
        for (const auto& lang : GetSupportedLanguages()) {
            if (lang.codeName == codeName) return lang.code;
        }
        return LanguageCode::AutoDetect;
    }
};

} // namespace LinguaAlpaca::Domain::Model
