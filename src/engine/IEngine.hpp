#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <functional>
#include "core/Types.hpp"

namespace LinguaAlpaca::Engine {

class ITranslationEngine {
public:
    virtual ~ITranslationEngine() = default;

    virtual bool IsModelLoaded() const = 0;

    virtual void TranslateStreamAsync(
        const TranslationTask& task,
        StreamTokenCallback onToken,
        StreamCompleteCallback onComplete) = 0;

    virtual void CancelCurrentTask() = 0;
};

class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;

    virtual bool IsModelLoaded() const = 0;
    virtual std::string GetModelPath() const = 0;
    virtual std::string GetMmprojPath() const = 0;

    virtual void RecognizeStream(
        const std::string& imagePath,
        const std::string& taskType,
        const std::string& modelPath,
        const std::string& mmprojPath,
        OcrTokenCallback onToken,
        OcrCompleteCallback onComplete
    ) = 0;

    virtual void Cancel() = 0;
    virtual bool IsRunning() const = 0;
};

} // namespace LinguaAlpaca::Engine
