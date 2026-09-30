#pragma once
#pragma execution_character_set("utf-8")

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace LinguaAlpaca {

// ============================================================================
// 语言定义与辅助工具
// ============================================================================

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

// ============================================================================
// 翻译任务与状态
// ============================================================================

enum class TaskStatus {
    Pending,
    Processing,
    Completed,
    Failed,
    Cancelled
};

class TranslationTask {
public:
    TranslationTask() = default;

    TranslationTask(
        std::string sourceText,
        LanguageCode sourceLang,
        LanguageCode targetLang)
        : m_sourceText(std::move(sourceText))
        , m_sourceLang(sourceLang)
        , m_targetLang(targetLang)
        , m_status(TaskStatus::Pending)
        , m_timestamp(std::chrono::system_clock::now()) {}

    TranslationTask(
        std::string id,
        std::string sourceText,
        LanguageCode sourceLang,
        LanguageCode targetLang)
        : m_id(std::move(id))
        , m_sourceText(std::move(sourceText))
        , m_sourceLang(sourceLang)
        , m_targetLang(targetLang)
        , m_status(TaskStatus::Pending)
        , m_timestamp(std::chrono::system_clock::now()) {}

    const std::string& GetId() const { return m_id; }
    const std::string& GetSourceText() const { return m_sourceText; }
    const std::string& GetTranslatedText() const { return m_translatedText; }
    LanguageCode GetSourceLanguage() const { return m_sourceLang; }
    LanguageCode GetTargetLanguage() const { return m_targetLang; }
    TaskStatus GetStatus() const { return m_status; }
    const std::string& GetErrorMessage() const { return m_errorMessage; }
    std::chrono::system_clock::time_point GetTimestamp() const { return m_timestamp; }

    void SetId(std::string id) { m_id = std::move(id); }
    void SetSourceText(std::string text) { m_sourceText = std::move(text); }
    void SetTranslatedText(std::string text) {
        m_translatedText = std::move(text);
        m_status = TaskStatus::Completed;
    }
    void SetSourceLanguage(LanguageCode lang) { m_sourceLang = lang; }
    void SetTargetLanguage(LanguageCode lang) { m_targetLang = lang; }
    void SetStatus(TaskStatus status) { m_status = status; }
    void SetErrorMessage(std::string msg) {
        m_errorMessage = std::move(msg);
        m_status = TaskStatus::Failed;
    }
    void SetTimestamp(std::chrono::system_clock::time_point tp) { m_timestamp = tp; }

private:
    std::string m_id;
    std::string m_sourceText;
    std::string m_translatedText;
    LanguageCode m_sourceLang{LanguageCode::AutoDetect};
    LanguageCode m_targetLang{LanguageCode::Chinese};
    TaskStatus m_status{TaskStatus::Pending};
    std::string m_errorMessage;
    std::chrono::system_clock::time_point m_timestamp{std::chrono::system_clock::now()};
};

// ============================================================================
// 模型与服务器状态
// ============================================================================

enum class TargetModelType {
    None,
    Translation,
    Ocr,
    DocLayout
};

enum class ServerHealthState {
    Unconfigured, // 模型未配置路径或文件不存在
    Offline,      // 服务未运行 / 权重未加载
    Loading,      // 正在启动或正在装载权重 (/health 503)
    Ready,        // 模型就绪且响应正常 (/health 200)
    Error         // 运行异常或探针报错 (/health 500)
};

struct ServerStatusInfo {
    ServerHealthState state{ServerHealthState::Offline};
    std::string message;
    std::string currentModel;
    TargetModelType activeType{TargetModelType::None};
    int port{0};
    std::string baseUrl;
};

// ============================================================================
// 文档版面分析数据结构
// ============================================================================

enum class LayoutElementType {
    Unknown,
    Text,        // 普通段落
    Title,       // 文档/章节标题
    Header,      // 页眉
    Footer,      // 页脚
    Table,       // 表格区域
    Formula,     // 公式区域
    Image,       // 图像/插图
    Chart        // 图表
};

struct LayoutElement {
    int id{0};
    LayoutElementType type{LayoutElementType::Unknown};
    std::string labelName;
    float score{0.0f};
    int x1{0};
    int y1{0};
    int x2{0};
    int y2{0};
    int readingOrder{0};
};

struct DocumentLayoutResult {
    int pageIndex{0};
    int imageWidth{0};
    int imageHeight{0};
    std::vector<LayoutElement> elements;
};

// ============================================================================
// 文档版面过滤与阅读顺序恢复配置 (对齐 PaddleX 官方产线规范)
// ============================================================================

struct DocLayoutFilterConfig {
    // 1. 基础类别过滤开关
    bool filterHeader{true};             // 是否过滤页眉 (header / header_image)
    bool filterFooter{true};             // 是否过滤页脚 (footer / footer_image)
    bool filterPageNumber{true};         // 是否过滤独立页码 (number)

    // 2. 几何坐标辅助参数 (相对原图高度比例，0.0f ~ 1.0f)
    float headerMarginRatio{0.08f};      // 顶部页眉敏感区高度比例 (默认顶部 8%)
    float footerMarginRatio{0.08f};      // 底部页脚敏感区高度比例 (默认底部 8%)

    // 3. 几何兜底过滤保护参数 (针对被误判为 Text/Title 的漏检页眉页脚)
    bool enableGeometricFallback{true};  // 是否启用几何空间兜底过滤
    float maxHeaderHeightRatio{0.05f};   // 允许认定为页眉的最大单框高度比例 (超过 5% 的多行大段落不予误删)
    float maxFooterHeightRatio{0.05f};   // 允许认定为页脚的最大单框高度比例

    // 4. 置信度门限
    float scoreThreshold{0.35f};         // 检测框置信度过滤门限
};


// ============================================================================
// 历史记录
// ============================================================================

struct HistoryRecord {
    std::string id;
    std::string sourceText;
    std::string translatedText;
    LanguageCode sourceLang{LanguageCode::AutoDetect};
    LanguageCode targetLang{LanguageCode::Chinese};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

// ============================================================================
// 回调定义
// ============================================================================

using StreamTokenCallback = std::function<void(const std::string& token)>;
using StreamCompleteCallback = std::function<void(bool success, const std::string& fullText, const std::string& error)>;
using OcrTokenCallback = std::function<void(const std::string& token)>;
using OcrCompleteCallback = std::function<void(const std::string& fullText, bool success, const std::string& error)>;

} // namespace LinguaAlpaca
