#pragma once
#include <memory>
#include "../../domain/repository/IOcrEngine.hpp"

namespace LinguaAlpaca::Application::Service {

class OcrService {
public:
    OcrService(std::shared_ptr<Domain::Repository::IOcrEngine> ocrEngine);

    bool IsModelLoaded() const;
    std::string GetModelPath() const;
    std::string GetMmprojPath() const;

    void RecognizeImageStream(
        const std::string& imagePath,
        const std::string& taskType,
        const std::string& modelPath,
        const std::string& mmprojPath,
        Domain::Repository::OcrTokenCallback onToken,
        Domain::Repository::OcrCompleteCallback onComplete
    );

    void CancelOcr();
    bool IsRunning() const;

private:
    std::shared_ptr<Domain::Repository::IOcrEngine> m_ocrEngine;
};

} // namespace LinguaAlpaca::Application::Service
