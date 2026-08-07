#pragma once
#include <string>
#include <functional>

namespace LinguaAlpaca::Domain::Repository {

using OcrTokenCallback = std::function<void(const std::string& token)>;
using OcrCompleteCallback = std::function<void(const std::string& fullText, bool success, const std::string& error)>;

class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;

    virtual bool LoadModel(const std::string& modelPath, const std::string& mmprojPath) = 0;
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

} // namespace LinguaAlpaca::Domain::Repository
