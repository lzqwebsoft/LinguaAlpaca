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

    // 状态检查
    virtual bool IsModelLoaded() const = 0;

    // 流式异步翻译 (Token 逐字打字效果)
    virtual void TranslateStreamAsync(
        const Model::TranslationTask& task,
        StreamTokenCallback onToken,
        StreamCompleteCallback onComplete) = 0;

    // 取消当前翻译
    virtual void CancelCurrentTask() = 0;
};

} // namespace LinguaAlpaca::Domain::Repository
