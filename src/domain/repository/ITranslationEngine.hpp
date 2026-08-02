#pragma once
#pragma execution_character_set("utf-8")
#include <string>
#include <functional>
#include "../model/TranslationTask.hpp"

namespace LinguaAlpaca::Domain::Repository {

using StreamTokenCallback = std::function<void(const std::string& token)>;
using StreamCompleteCallback = std::function<void(bool success, const std::string& fullText, const std::string& error)>;

class ITranslationEngine {
public:
    virtual ~ITranslationEngine() = default;

    // 模型加载与状态检查
    virtual bool LoadModel(const std::string& modelPath) = 0;
    virtual bool IsModelLoaded() const = 0;

    // 同步翻译
    virtual Model::TranslationTask Translate(const Model::TranslationTask& task) = 0;

    // 快速/即时预览翻译
    virtual std::string QuickTranslate(const std::string& text, Model::LanguageCode sourceLang, Model::LanguageCode targetLang) = 0;

    // 流式异步翻译 (Token 逐字打字效果)
    virtual void TranslateStreamAsync(
        const Model::TranslationTask& task,
        StreamTokenCallback onToken,
        StreamCompleteCallback onComplete) = 0;

    // 取消当前翻译
    virtual void CancelCurrentTask() = 0;
};

} // namespace LinguaAlpaca::Domain::Repository
